/*
** $Id: lparser.c $
** Lua Parser
** See Copyright Notice in lua.h
*/

#define lparser_c
#define LUA_CORE

#include "lprefix.h"


#include <limits.h>
#include <string.h>

#include "lua.h"

#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "llex.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"



/* maximum number of variable declarations per function (must be
   smaller than 250, due to the bytecode format) */
#define MAXVARS		200


inline bool hasmultret(expkind k) noexcept {
	return (k) == VCALL || (k) == VVARARG;
}


/* because all strings are unified by the scanner, the parser
   can use pointer equality for string equality */
inline bool eqstr(const TString* a, const TString* b) noexcept {
	return (a) == (b);
}


/*
** nodes for block list (list of active blocks)
*/
typedef struct BlockCnt {
  struct BlockCnt *previous;  /* chain */
  int firstlabel;  /* index of first label in this block */
  int firstgoto;  /* index of first pending goto in this block */
  short nactvar;  /* number of active declarations at block entry */
  lu_byte upval;  /* true if some variable in the block is an upvalue */
  lu_byte isloop;  /* 1 if 'block' is a loop; 2 if it has pending breaks */
  lu_byte insidetbc;  /* true if inside the scope of a to-be-closed var. */
} BlockCnt;



/*
** prototypes for recursive non-terminal functions
*/
static void statement (LexState *ls);
static void expr (LexState *ls, expdesc *v);


l_noret LexState::error_expected(int token) {
  syntaxError(
      luaO_pushfstring(getLuaState(), "%s expected", tokenToStr(token)));
}


l_noret FuncState::errorlimit(int limit, const char *what) {
  lua_State *L = getLexState()->getLuaState();
  const char *msg;
  int line = getProto()->getLineDefined();
  const char *where = (line == 0)
                      ? "main function"
                      : luaO_pushfstring(L, "function at line %d", line);
  msg = luaO_pushfstring(L, "too many %s (limit is %d) in %s",
                             what, limit, where);
  getLexState()->syntaxError(msg);
}


void FuncState::checklimit(int v, int l, const char *what) {
  if (l_unlikely(v > l)) errorlimit(l, what);
}

/* External API wrapper */
void luaY_checklimit (FuncState *fs, int v, int l, const char *what) {
  fs->checklimit(v, l, what);
}


/*
** Test whether next token is 'c'; if so, skip it.
*/
int LexState::testnext(int c) {
  if (getCurrentToken().token == c) {
    nextToken();
    return 1;
  }
  else return 0;
}


/*
** Check that next token is 'c'.
*/
void LexState::check(int c) {
  if (getCurrentToken().token != c)
    error_expected(c);
}


/*
** Check that next token is 'c' and skip it.
*/
void LexState::checknext(int c) {
  check(c);
  nextToken();
}


#define check_condition(ls,c,msg)	{ if (!(c)) ls->syntaxError( msg); }


/*
** Check that next token is 'what' and skip it. In case of error,
** raise an error that the expected 'what' should match a 'who'
** in line 'where' (if that is not the current line).
*/
void LexState::check_match(int what, int who, int where) {
  if (l_unlikely(!testnext(what))) {
    if (where == getLineNumber())  /* all in the same line? */
      error_expected(what);  /* do not need a complex message */
    else {
      syntaxError(luaO_pushfstring(getLuaState(),
             "%s expected (to close %s at line %d)",
              tokenToStr(what), tokenToStr(who), where));
    }
  }
}


TString *LexState::str_checkname() {
  TString *ts;
  check(TK_NAME);
  ts = getCurrentToken().seminfo.ts;
  nextToken();
  return ts;
}


void expdesc::init(expkind kind, int i) {
  setFalseList(NO_JUMP);
  setTrueList(NO_JUMP);
  setKind(kind);
  setInfo(i);
}


void expdesc::initString(TString *s) {
  setFalseList(NO_JUMP);
  setTrueList(NO_JUMP);
  setKind(VKSTR);
  setStringValue(s);
}


void LexState::codename(expdesc *e) {
  e->initString(str_checkname());
}


/*
** Register a new local variable in the active 'Proto' (for debug
** information).
*/
short FuncState::registerlocalvar(TString *varname) {
  Proto *proto = getProto();
  int oldsize = proto->getLocVarsSize();
  luaM_growvector(getLexState()->getLuaState(), proto->getLocVarsRef(), getNumDebugVars(), proto->getLocVarsSizeRef(),
                  LocVar, SHRT_MAX, "local variables");
  while (oldsize < proto->getLocVarsSize())
    proto->getLocVars()[oldsize++].setVarName(NULL);
  proto->getLocVars()[getNumDebugVars()].setVarName(varname);
  proto->getLocVars()[getNumDebugVars()].setStartPC(getPC());
  luaC_objbarrier(getLexState()->getLuaState(), proto, varname);
  return postIncrementNumDebugVars();
}


/*
** Create a new variable with the given 'name' and given 'kind'.
** Return its index in the function.
*/
int LexState::new_varkind(TString *name, lu_byte kind) {
  lua_State *state = getLuaState();
  FuncState *funcState = getFuncState();
  Dyndata *dynData = getDyndata();
  Vardesc *var;
  luaM_growvector(state, dynData->actvar.arr, dynData->actvar.n + 1,
             dynData->actvar.size, Vardesc, SHRT_MAX, "variable declarations");
  var = &dynData->actvar.arr[dynData->actvar.n++];
  var->vd.kind = kind;  /* default */
  var->vd.name = name;
  return dynData->actvar.n - 1 - funcState->getFirstLocal();
}


/*
** Create a new local variable with the given 'name' and regular kind.
*/
int LexState::new_localvar(TString *name) {
  return new_varkind(name, VDKREG);
}

#define new_localvarliteral(ls,v) \
    ls->new_localvar(  \
      ls->newString( "" v, (sizeof(v)/sizeof(char)) - 1));



/*
** Return the "variable description" (Vardesc) of a given variable.
** (Unless noted otherwise, all variables are referred to by their
** compiler indices.)
*/
Vardesc *FuncState::getlocalvardesc(int vidx) {
  return &getLexState()->getDyndata()->actvar.arr[getFirstLocal() + vidx];
}


/*
** Convert 'nvar', a compiler index level, to its corresponding
** register. For that, search for the highest variable below that level
** that is in a register and uses its register index ('ridx') plus one.
*/
lu_byte FuncState::reglevel(int nvar) {
  while (nvar-- > 0) {
    Vardesc *vd = getlocalvardesc(nvar);  /* get previous variable */
    if (vd->isInReg())  /* is in a register? */
      return cast_byte(vd->vd.ridx + 1);
  }
  return 0;  /* no variables in registers */
}


/*
** Return the number of variables in the register stack for the given
** function.
*/
lu_byte FuncState::nvarstack() {
  return reglevel(getNumActiveVars());
}

/* External API wrapper */
lu_byte luaY_nvarstack (FuncState *fs) {
  return fs->nvarstack();
}


/*
** Get the debug-information entry for current variable 'vidx'.
*/
LocVar *FuncState::localdebuginfo(int vidx) {
  Vardesc *vd = getlocalvardesc(vidx);
  if (!vd->isInReg())
    return NULL;  /* no debug info. for constants */
  else {
    int idx = vd->vd.pidx;
    lua_assert(idx < getNumDebugVars());
    return &getProto()->getLocVars()[idx];
  }
}


/*
** Create an expression representing variable 'vidx'
*/
void FuncState::init_var(expdesc *e, int vidx) {
  e->setFalseList(NO_JUMP);
  e->setTrueList(NO_JUMP);
  e->setKind(VLOCAL);
  e->setLocalVarIndex(cast_short(vidx));
  e->setLocalRegister(getlocalvardesc(vidx)->vd.ridx);
}


/*
** Raises an error if variable described by 'e' is read only
*/
void LexState::check_readonly(expdesc *e) {
  FuncState *funcState = getFuncState();
  TString *varname = NULL;  /* to be set if variable is const */
  switch (e->getKind()) {
    case VCONST: {
      varname = getDyndata()->actvar.arr[e->getInfo()].vd.name;
      break;
    }
    case VLOCAL: {
      Vardesc *vardesc = funcState->getlocalvardesc(e->getLocalVarIndex());
      if (vardesc->vd.kind != VDKREG)  /* not a regular variable? */
        varname = vardesc->vd.name;
      break;
    }
    case VUPVAL: {
      Upvaldesc *up = &funcState->getProto()->getUpvalues()[e->getInfo()];
      if (up->getKind() != VDKREG)
        varname = up->getName();
      break;
    }
    case VINDEXUP: case VINDEXSTR: case VINDEXED: {  /* global variable */
      if (e->isIndexedReadOnly())  /* read-only? */
        varname = tsvalue(&funcState->getProto()->getConstants()[e->getIndexedStringKeyIndex()]);
      break;
    }
    default:
      lua_assert(e->getKind() == VINDEXI);  /* this one doesn't need any check */
      return;  /* integer index cannot be read-only */
  }
  if (varname)
    semerror("attempt to assign to const variable '%s'", getstr(varname));
}


/*
** Start the scope for the last 'nvars' created variables.
*/
void LexState::adjustlocalvars(int nvars) {
  FuncState *funcState = getFuncState();
  int regLevel = funcState->nvarstack();
  int i;
  for (i = 0; i < nvars; i++) {
    int vidx = funcState->getNumActiveVarsRef()++;
    Vardesc *var = funcState->getlocalvardesc(vidx);
    var->vd.ridx = cast_byte(regLevel++);
    var->vd.pidx = funcState->registerlocalvar(var->vd.name);
    funcState->checklimit(regLevel, MAXVARS, "local variables");
  }
}


/*
** Close the scope for all variables up to level 'tolevel'.
** (debug info.)
*/
void FuncState::removevars(int tolevel) {
  getLexState()->getDyndata()->actvar.n -= (getNumActiveVars() - tolevel);
  while (getNumActiveVars() > tolevel) {
    LocVar *var = localdebuginfo(--getNumActiveVarsRef());
    if (var)  /* does it have debug information? */
      var->setEndPC(getPC());
  }
}


/*
** Search the upvalues of the function for one
** with the given 'name'.
*/
int FuncState::searchupvalue(TString *name) {
  int i;
  Upvaldesc *up = getProto()->getUpvalues();
  for (i = 0; i < getNumUpvalues(); i++) {
    if (eqstr(up[i].getName(), name)) return i;
  }
  return -1;  /* not found */
}


Upvaldesc *FuncState::allocupvalue() {
  Proto *proto = getProto();
  int oldsize = proto->getUpvaluesSize();
  checklimit(getNumUpvalues() + 1, MAXUPVAL, "upvalues");
  luaM_growvector(getLexState()->getLuaState(), proto->getUpvaluesRef(), getNumUpvalues(), proto->getUpvaluesSizeRef(),
                  Upvaldesc, MAXUPVAL, "upvalues");
  while (oldsize < proto->getUpvaluesSize())
    proto->getUpvalues()[oldsize++].setName(NULL);
  return &proto->getUpvalues()[getNumUpvaluesRef()++];
}


int FuncState::newupvalue(TString *name, expdesc *v) {
  Upvaldesc *up = allocupvalue();
  FuncState *prevFunc = getPrev();
  if (v->getKind() == VLOCAL) {
    up->setInStack(1);
    up->setIndex(v->getLocalRegister());
    up->setKind(prevFunc->getlocalvardesc(v->getLocalVarIndex())->vd.kind);
    lua_assert(eqstr(name, prevFunc->getlocalvardesc(v->getLocalVarIndex())->vd.name));
  }
  else {
    up->setInStack(0);
    up->setIndex(cast_byte(v->getInfo()));
    up->setKind(prevFunc->getProto()->getUpvalues()[v->getInfo()].getKind());
    lua_assert(eqstr(name, prevFunc->getProto()->getUpvalues()[v->getInfo()].getName()));
  }
  up->setName(name);
  luaC_objbarrier(getLexState()->getLuaState(), getProto(), name);
  return getNumUpvalues() - 1;
}


/*
** Look for an active variable with the name 'n' in the function.
** If found, initialize 'var' with it and return its expression kind;
** otherwise return -1. While searching, var->u.info==-1 means that
** the preambular global declaration is active (the default while
** there is no other global declaration); var->u.info==-2 means there
** is no active collective declaration (some previous global declaration
** but no collective declaration); and var->u.info>=0 points to the
** inner-most (the first one found) collective declaration, if there is one.
*/
int FuncState::searchvar(TString *n, expdesc *var) {
  int i;
  for (i = cast_int(getNumActiveVars()) - 1; i >= 0; i--) {
    Vardesc *vd = getlocalvardesc(i);
    if (vd->isGlobal()) {  /* global declaration? */
      if (vd->vd.name == NULL) {  /* collective declaration? */
        if (var->getInfo() < 0)  /* no previous collective declaration? */
          var->setInfo(getFirstLocal() + i);  /* this is the first one */
      }
      else {  /* global name */
        if (eqstr(n, vd->vd.name)) {  /* found? */
          var->init(VGLOBAL, getFirstLocal() + i);
          return VGLOBAL;
        }
        else if (var->getInfo() == -1)  /* active preambular declaration? */
          var->setInfo(-2);  /* invalidate preambular declaration */
      }
    }
    else if (eqstr(n, vd->vd.name)) {  /* found? */
      if (vd->vd.kind == RDKCTC)  /* compile-time constant? */
        var->init(VCONST, getFirstLocal() + i);
      else  /* local variable */
        init_var(var, i);
      return cast_int(var->getKind());
    }
  }
  return -1;  /* not found */
}


/*
** Mark block where variable at given level was defined
** (to emit close instructions later).
*/
void FuncState::markupval(int level) {
  BlockCnt *block = getBlock();
  while (block->nactvar > level)
    block = block->previous;
  block->upval = 1;
  setNeedClose(1);
}


/*
** Mark that current block has a to-be-closed variable.
*/
void FuncState::marktobeclosed() {
  BlockCnt *block = getBlock();
  block->upval = 1;
  block->insidetbc = 1;
  setNeedClose(1);
}


/*
** Find a variable with the given name 'n'. If it is an upvalue, add
** this upvalue into all intermediate functions. If it is a global, set
** 'var' as 'void' as a flag.
*/
void FuncState::singlevaraux(TString *n, expdesc *var, int base) {
  int v = searchvar(n, var);  /* look up variables at current level */
  if (v >= 0) {  /* found? */
    if (v == VLOCAL && !base)
      markupval(var->getLocalVarIndex());  /* local will be used as an upval */
  }
  else {  /* not found at current level; try upvalues */
    int idx = searchupvalue(n);  /* try existing upvalues */
    if (idx < 0) {  /* not found? */
      if (getPrev() != NULL)  /* more levels? */
        getPrev()->singlevaraux(n, var, 0);  /* try upper levels */
      if (var->getKind() == VLOCAL || var->getKind() == VUPVAL)  /* local or upvalue? */
        idx = newupvalue(n, var);  /* will be a new upvalue */
      else  /* it is a global or a constant */
        return;  /* don't need to do anything at this level */
    }
    var->init(VUPVAL, idx);  /* new or old upvalue */
  }
}


void LexState::buildglobal(TString *varname, expdesc *var) {
  FuncState *funcState = getFuncState();
  expdesc key;
  var->init(VGLOBAL, -1);  /* global by default */
  funcState->singlevaraux(getEnvName(), var, 1);  /* get environment variable */
  if (var->getKind() == VGLOBAL)
    semerror("_ENV is global when accessing variable '%s'", getstr(varname));
  funcState->exp2anyregup(var);  /* _ENV could be a constant */
  key.initString(varname);  /* key is variable name */
  funcState->indexed(var, &key);  /* 'var' represents _ENV[varname] */
}


/*
** Find a variable with the given name, handling global variables too.
*/
void LexState::buildvar(TString *varname, expdesc *var) {
  FuncState *funcState = getFuncState();
  var->init(VGLOBAL, -1);  /* global by default */
  funcState->singlevaraux(varname, var, 1);
  if (var->getKind() == VGLOBAL) {  /* global name? */
    int info = var->getInfo();
    /* global by default in the scope of a global declaration? */
    if (info == -2)
      semerror("variable '%s' not declared", getstr(varname));
    buildglobal(varname, var);
    if (info != -1 && getDyndata()->actvar.arr[info].vd.kind == GDKCONST)
      var->setIndexedReadOnly(1);  /* mark variable as read-only */
    else  /* anyway must be a global */
      lua_assert(info == -1 || getDyndata()->actvar.arr[info].vd.kind == GDKREG);
  }
}


void LexState::singlevar(expdesc *var) {
  buildvar(str_checkname(), var);
}


/*
** Adjust the number of results from an expression list 'e' with 'nexps'
** expressions to 'nvars' values.
*/
void LexState::adjust_assign(int nvars, int nexps, expdesc *e) {
  FuncState *funcState = getFuncState();
  int needed = nvars - nexps;  /* extra values needed */
  if (hasmultret(e->getKind())) {  /* last expression has multiple returns? */
    int extra = needed + 1;  /* discount last expression itself */
    if (extra < 0)
      extra = 0;
    funcState->setreturns(e, extra);  /* last exp. provides the difference */
  }
  else {
    if (e->getKind() != VVOID)  /* at least one expression? */
      funcState->exp2nextreg(e);  /* close last expression */
    if (needed > 0)  /* missing values? */
      funcState->nil(funcState->getFreeReg(), needed);  /* complete with nils */
  }
  if (needed > 0)
    funcState->reserveregs(needed);  /* registers for extra values */
  else  /* adding 'needed' is actually a subtraction */
    funcState->setFreeReg(cast_byte(funcState->getFreeReg() + needed));  /* remove extra values */
}


inline void enterlevel(LexState* ls) {
	luaE_incCstack(ls->getLuaState());
}

inline void leavelevel(LexState* ls) noexcept {
	ls->getLuaState()->getNCcallsRef()--;
}


/*
** Generates an error that a goto jumps into the scope of some
** variable declaration.
*/
l_noret LexState::jumpscopeerror(Labeldesc *gt) {
  TString *tsname = getFuncState()->getlocalvardesc(gt->nactvar)->vd.name;
  const char *varname = (tsname != NULL) ? getstr(tsname) : "*";
  semerror("<goto %s> at line %d jumps into the scope of '%s'",
           getstr(gt->name), gt->line, varname);  /* raise the error */
}


/*
** Closes the goto at index 'g' to given 'label' and removes it
** from the list of pending gotos.
** If it jumps into the scope of some variable, raises an error.
** The goto needs a CLOSE if it jumps out of a block with upvalues,
** or out of the scope of some variable and the block has upvalues
** (signaled by parameter 'bup').
*/
void LexState::closegoto(int g, Labeldesc *label, int bup) {
  int i;
  FuncState *funcState = getFuncState();
  Labellist *gl = &getDyndata()->gt;  /* list of gotos */
  Labeldesc *gt = &gl->arr[g];  /* goto to be resolved */
  lua_assert(eqstr(gt->name, label->name));
  if (l_unlikely(gt->nactvar < label->nactvar))  /* enter some scope? */
    jumpscopeerror(gt);
  if (gt->close ||
      (label->nactvar < gt->nactvar && bup)) {  /* needs close? */
    lu_byte stklevel = funcState->reglevel(label->nactvar);
    /* move jump to CLOSE position */
    funcState->getProto()->getCode()[gt->pc + 1] = funcState->getProto()->getCode()[gt->pc];
    /* put CLOSE instruction at original position */
    funcState->getProto()->getCode()[gt->pc] = CREATE_ABCk(OP_CLOSE, stklevel, 0, 0, 0);
    gt->pc++;  /* must point to jump instruction */
  }
  getFuncState()->patchlist(gt->pc, label->pc);  /* goto jumps to label */
  for (i = g; i < gl->n - 1; i++)  /* remove goto from pending list */
    gl->arr[i] = gl->arr[i + 1];
  gl->n--;
}


/*
** Search for an active label with the given name, starting at
** index 'ilb' (so that it can search for all labels in current block
** or all labels in current function).
*/
Labeldesc *LexState::findlabel(TString *name, int ilb) {
  Dyndata *dynData = getDyndata();
  for (; ilb < dynData->label.n; ilb++) {
    Labeldesc *lb = &dynData->label.arr[ilb];
    if (eqstr(lb->name, name))  /* correct label? */
      return lb;
  }
  return NULL;  /* label not found */
}


/*
** Adds a new label/goto in the corresponding list.
*/
int LexState::newlabelentry(Labellist *l, TString *name, int line, int pc) {
  int n = l->n;
  luaM_growvector(getLuaState(), l->arr, n, l->size,
                  Labeldesc, SHRT_MAX, "labels/gotos");
  l->arr[n].name = name;
  l->arr[n].line = line;
  l->arr[n].nactvar = getFuncState()->getNumActiveVars();
  l->arr[n].close = 0;
  l->arr[n].pc = pc;
  l->n = n + 1;
  return n;
}


/*
** Create an entry for the goto and the code for it. As it is not known
** at this point whether the goto may need a CLOSE, the code has a jump
** followed by an CLOSE. (As the CLOSE comes after the jump, it is a
** dead instruction; it works as a placeholder.) When the goto is closed
** against a label, if it needs a CLOSE, the two instructions swap
** positions, so that the CLOSE comes before the jump.
*/
int LexState::newgotoentry(TString *name, int line) {
  FuncState *funcState = getFuncState();
  int pc = funcState->jump();  /* create jump */
  funcState->codeABC(OP_CLOSE, 0, 1, 0);  /* spaceholder, marked as dead */
  return newlabelentry(&getDyndata()->gt, name, line, pc);
}


/*
** Create a new label with the given 'name' at the given 'line'.
** 'last' tells whether label is the last non-op statement in its
** block. Solves all pending gotos to this new label and adds
** a close instruction if necessary.
** Returns true iff it added a close instruction.
*/
void LexState::createlabel(TString *name, int line, int last) {
  FuncState *funcState = getFuncState();
  Labellist *ll = &getDyndata()->label;
  int l = newlabelentry(ll, name, line, funcState->getlabel());
  if (last) {  /* label is last no-op statement in the block? */
    /* assume that locals are already out of scope */
    ll->arr[l].nactvar = funcState->getBlock()->nactvar;
  }
}


/*
** Traverse the pending gotos of the finishing block checking whether
** each match some label of that block. Those that do not match are
** "exported" to the outer block, to be solved there. In particular,
** its 'nactvar' is updated with the level of the inner block,
** as the variables of the inner block are now out of scope.
*/
void FuncState::solvegotos(BlockCnt *blockCnt) {
  LexState *lexState = getLexState();
  Labellist *gl = &lexState->getDyndata()->gt;
  int outlevel = reglevel(blockCnt->nactvar);  /* level outside the block */
  int igt = blockCnt->firstgoto;  /* first goto in the finishing block */
  while (igt < gl->n) {   /* for each pending goto */
    Labeldesc *gt = &gl->arr[igt];
    /* search for a matching label in the current block */
    Labeldesc *lb = lexState->findlabel(gt->name, blockCnt->firstlabel);
    if (lb != NULL)  /* found a match? */
      lexState->closegoto(igt, lb, blockCnt->upval);  /* close and remove goto */
    else {  /* adjust 'goto' for outer block */
      /* block has variables to be closed and goto escapes the scope of
         some variable? */
      if (blockCnt->upval && reglevel(gt->nactvar) > outlevel)
        gt->close = 1;  /* jump may need a close */
      gt->nactvar = blockCnt->nactvar;  /* correct level for outer block */
      igt++;  /* go to next goto */
    }
  }
  lexState->getDyndata()->label.n = blockCnt->firstlabel;  /* remove local labels */
}


static void enterblock (FuncState *fs, BlockCnt *bl, lu_byte isloop) {
  bl->isloop = isloop;
  bl->nactvar = fs->getNumActiveVars();
  bl->firstlabel = fs->getLexState()->getDyndata()->label.n;
  bl->firstgoto = fs->getLexState()->getDyndata()->gt.n;
  bl->upval = 0;
  /* inherit 'insidetbc' from enclosing block */
  bl->insidetbc = (fs->getBlock() != NULL && fs->getBlock()->insidetbc);
  bl->previous = fs->getBlock();  /* link block in function's block list */
  fs->setBlock(bl);
  lua_assert(fs->getFreeReg() == luaY_nvarstack(fs));
}


/*
** generates an error for an undefined 'goto'.
*/
static l_noret undefgoto (LexState *ls, Labeldesc *gt) {
  /* breaks are checked when created, cannot be undefined */
  lua_assert(!eqstr(gt->name, ls->getBreakName()));
  ls->semerror( "no visible label '%s' for <goto> at line %d",
                    getstr(gt->name), gt->line);
}


static void leaveblock (FuncState *fs) {
  BlockCnt *bl = fs->getBlock();
  LexState *ls = fs->getLexState();
  lu_byte stklevel = fs->reglevel( bl->nactvar);  /* level outside block */
  if (bl->previous && bl->upval)  /* need a 'close'? */
    fs->codeABC(OP_CLOSE, stklevel, 0, 0);
  fs->setFreeReg(stklevel);  /* free registers */
  fs->removevars(bl->nactvar);  /* remove block locals */
  lua_assert(bl->nactvar == fs->getNumActiveVars());  /* back to level on entry */
  if (bl->isloop == 2)  /* has to fix pending breaks? */
    ls->createlabel(ls->getBreakName(), 0, 0);
  fs->solvegotos(bl);
  if (bl->previous == NULL) {  /* was it the last block? */
    if (bl->firstgoto < ls->getDyndata()->gt.n)  /* still pending gotos? */
      undefgoto(ls, &ls->getDyndata()->gt.arr[bl->firstgoto]);  /* error */
  }
  fs->setBlock(bl->previous);  /* current block now is previous one */
}


/*
** adds a new prototype into list of prototypes
*/
static Proto *addprototype (LexState *ls) {
  Proto *clp;
  lua_State *L = ls->getLuaState();
  FuncState *fs = ls->getFuncState();
  Proto *f = fs->getProto();  /* prototype of current function */
  if (fs->getNP() >= f->getProtosSize()) {
    int oldsize = f->getProtosSize();
    luaM_growvector(L, f->getProtosRef(), fs->getNP(), f->getProtosSizeRef(), Proto *, MAXARG_Bx, "functions");
    while (oldsize < f->getProtosSize())
      f->getProtos()[oldsize++] = NULL;
  }
  f->getProtos()[fs->getNPRef()++] = clp = luaF_newproto(L);
  luaC_objbarrier(L, f, clp);
  return clp;
}


/*
** codes instruction to create new closure in parent function.
** The OP_CLOSURE instruction uses the last available register,
** so that, if it invokes the GC, the GC knows which registers
** are in use at that time.

*/
static void codeclosure (LexState *ls, expdesc *v) {
  FuncState *fs = ls->getFuncState()->getPrev();
  v->init(VRELOC, fs->codeABx(OP_CLOSURE, 0, fs->getNP() - 1));
  fs->exp2nextreg(v);  /* fix it at the last register */
}


static void open_func (LexState *ls, FuncState *fs, BlockCnt *bl) {
  lua_State *L = ls->getLuaState();
  Proto *f = fs->getProto();
  fs->setPrev(ls->getFuncState());  /* linked list of funcstates */
  fs->setLexState(ls);
  ls->setFuncState(fs);
  fs->setPC(0);
  fs->setPreviousLine(f->getLineDefined());
  fs->setInstructionsWithAbs(0);
  fs->setLastTarget(0);
  fs->setFreeReg(0);
  fs->setNK(0);
  fs->setNAbsLineInfo(0);
  fs->setNP(0);
  fs->setNumUpvalues(0);
  fs->setNumDebugVars(0);
  fs->setNumActiveVars(0);
  fs->setNeedClose(0);
  fs->setFirstLocal(ls->getDyndata()->actvar.n);
  fs->setFirstLabel(ls->getDyndata()->label.n);
  fs->setBlock(NULL);
  f->setSource(ls->getSource());
  luaC_objbarrier(L, f, f->getSource());
  f->setMaxStackSize(2);  /* registers 0/1 are always valid */
  fs->setKCache(luaH_new(L));  /* create table for function */
  sethvalue2s(L, L->getTop().p, fs->getKCache());  /* anchor it */
  L->inctop();  /* Phase 25e */
  enterblock(fs, bl, 0);
}


static void close_func (LexState *ls) {
  lua_State *L = ls->getLuaState();
  FuncState *fs = ls->getFuncState();
  Proto *f = fs->getProto();
  fs->ret(luaY_nvarstack(fs), 0);  /* final return */
  leaveblock(fs);
  lua_assert(fs->getBlock() == NULL);
  fs->finish();
  luaM_shrinkvector(L, f->getCodeRef(), f->getCodeSizeRef(), fs->getPC(), Instruction);
  luaM_shrinkvector(L, f->getLineInfoRef(), f->getLineInfoSizeRef(), fs->getPC(), ls_byte);
  luaM_shrinkvector(L, f->getAbsLineInfoRef(), f->getAbsLineInfoSizeRef(),
                       fs->getNAbsLineInfo(), AbsLineInfo);
  luaM_shrinkvector(L, f->getConstantsRef(), f->getConstantsSizeRef(), fs->getNK(), TValue);
  luaM_shrinkvector(L, f->getProtosRef(), f->getProtosSizeRef(), fs->getNP(), Proto *);
  luaM_shrinkvector(L, f->getLocVarsRef(), f->getLocVarsSizeRef(), fs->getNumDebugVars(), LocVar);
  luaM_shrinkvector(L, f->getUpvaluesRef(), f->getUpvaluesSizeRef(), fs->getNumUpvalues(), Upvaldesc);
  ls->setFuncState(fs->getPrev());
  L->getTop().p--;  /* pop kcache table */
  luaC_checkGC(L);
}


/*
** {======================================================================
** GRAMMAR RULES
** =======================================================================
*/


/*
** check whether current token is in the follow set of a block.
** 'until' closes syntactical blocks, but do not close scope,
** so it is handled in separate.
*/
static int block_follow (LexState *ls, int withuntil) {
  switch (ls->getCurrentToken().token) {
    case TK_ELSE: case TK_ELSEIF:
    case TK_END: case TK_EOS:
      return 1;
    case TK_UNTIL: return withuntil;
    default: return 0;
  }
}


static void statlist (LexState *ls) {
  /* statlist -> { stat [';'] } */
  while (!block_follow(ls, 1)) {
    if (ls->getCurrentToken().token == TK_RETURN) {
      statement(ls);
      return;  /* 'return' must be last statement */
    }
    statement(ls);
  }
}


static void fieldsel (LexState *ls, expdesc *v) {
  /* fieldsel -> ['.' | ':'] NAME */
  FuncState *fs = ls->getFuncState();
  expdesc key;
  fs->exp2anyregup(v);
  ls->nextToken();  /* skip the dot or colon */
  ls->codename( &key);
  fs->indexed(v, &key);
}


static void yindex (LexState *ls, expdesc *v) {
  /* index -> '[' expr ']' */
  ls->nextToken();  /* skip the '[' */
  expr(ls, v);
  ls->getFuncState()->exp2val(v);
  ls->checknext( ']');
}


/*
** {======================================================================
** Rules for Constructors
** =======================================================================
*/

typedef struct ConsControl {
  expdesc v;  /* last list item read */
  expdesc *t;  /* table descriptor */
  int nh;  /* total number of 'record' elements */
  int na;  /* number of array elements already stored */
  int tostore;  /* number of array elements pending to be stored */
  int maxtostore;  /* maximum number of pending elements */
} ConsControl;


/*
** Maximum number of elements in a constructor, to control the following:
** * counter overflows;
** * overflows in 'extra' for OP_NEWTABLE and OP_SETLIST;
** * overflows when adding multiple returns in OP_SETLIST.
*/
#define MAX_CNST	(INT_MAX/2)
#if MAX_CNST/(MAXARG_vC + 1) > MAXARG_Ax
#undef MAX_CNST
#define MAX_CNST	(MAXARG_Ax * (MAXARG_vC + 1))
#endif


static void recfield (LexState *ls, ConsControl *cc) {
  /* recfield -> (NAME | '['exp']') = exp */
  FuncState *fs = ls->getFuncState();
  lu_byte reg = ls->getFuncState()->getFreeReg();
  expdesc tab, key, val;
  if (ls->getCurrentToken().token == TK_NAME)
    ls->codename( &key);
  else  /* ls->getCurrentToken().token == '[' */
    yindex(ls, &key);
  cc->nh++;
  ls->checknext( '=');
  tab = *cc->t;
  fs->indexed(&tab, &key);
  expr(ls, &val);
  fs->storevar(&tab, &val);
  fs->setFreeReg(reg);  /* free registers */
}


static void closelistfield (FuncState *fs, ConsControl *cc) {
  lua_assert(cc->tostore > 0);
  fs->exp2nextreg(&cc->v);
  cc->v.setKind(VVOID);
  if (cc->tostore >= cc->maxtostore) {
    fs->setlist(cc->t->getInfo(), cc->na, cc->tostore);  /* flush */
    cc->na += cc->tostore;
    cc->tostore = 0;  /* no more items pending */
  }
}


static void lastlistfield (FuncState *fs, ConsControl *cc) {
  if (cc->tostore == 0) return;
  if (hasmultret(cc->v.getKind())) {
    luaK_setmultret(fs, &cc->v);
    fs->setlist(cc->t->getInfo(), cc->na, LUA_MULTRET);
    cc->na--;  /* do not count last expression (unknown number of elements) */
  }
  else {
    if (cc->v.getKind() != VVOID)
      fs->exp2nextreg(&cc->v);
    fs->setlist(cc->t->getInfo(), cc->na, cc->tostore);
  }
  cc->na += cc->tostore;
}


static void listfield (LexState *ls, ConsControl *cc) {
  /* listfield -> exp */
  expr(ls, &cc->v);
  cc->tostore++;
}


static void field (LexState *ls, ConsControl *cc) {
  /* field -> listfield | recfield */
  switch(ls->getCurrentToken().token) {
    case TK_NAME: {  /* may be 'listfield' or 'recfield' */
      if (ls->lookaheadToken() != '=')  /* expression? */
        listfield(ls, cc);
      else
        recfield(ls, cc);
      break;
    }
    case '[': {
      recfield(ls, cc);
      break;
    }
    default: {
      listfield(ls, cc);
      break;
    }
  }
}


/*
** Compute a limit for how many registers a constructor can use before
** emitting a 'SETLIST' instruction, based on how many registers are
** available.
*/
static int maxtostore (FuncState *fs) {
  int numfreeregs = MAX_FSTACK - fs->getFreeReg();
  if (numfreeregs >= 160)  /* "lots" of registers? */
    return numfreeregs / 5;  /* use up to 1/5 of them */
  else if (numfreeregs >= 80)  /* still "enough" registers? */
    return 10;  /* one 'SETLIST' instruction for each 10 values */
  else  /* save registers for potential more nesting */
    return 1;
}


static void constructor (LexState *ls, expdesc *t) {
  /* constructor -> '{' [ field { sep field } [sep] ] '}'
     sep -> ',' | ';' */
  FuncState *fs = ls->getFuncState();
  int line = ls->getLineNumber();
  int pc = fs->codevABCk(OP_NEWTABLE, 0, 0, 0, 0);
  ConsControl cc;
  fs->code(0);  /* space for extra arg. */
  cc.na = cc.nh = cc.tostore = 0;
  cc.t = t;
  t->init(VNONRELOC, fs->getFreeReg());  /* table will be at stack top */
  fs->reserveregs(1);
  cc.v.init(VVOID, 0);  /* no value (yet) */
  ls->checknext( '{' /*}*/);
  cc.maxtostore = maxtostore(fs);
  do {
    if (ls->getCurrentToken().token == /*{*/ '}') break;
    if (cc.v.getKind() != VVOID)  /* is there a previous list item? */
      closelistfield(fs, &cc);  /* close it */
    field(ls, &cc);
    luaY_checklimit(fs, cc.tostore + cc.na + cc.nh, MAX_CNST,
                    "items in a constructor");
  } while (ls->testnext( ',') || ls->testnext( ';'));
  ls->check_match( /*{*/ '}', '{' /*}*/, line);
  lastlistfield(fs, &cc);
  fs->settablesize(pc, t->getInfo(), cc.na, cc.nh);
}

/* }====================================================================== */


static void setvararg (FuncState *fs, int nparams) {
  fs->getProto()->setFlag(fs->getProto()->getFlag() | PF_ISVARARG);
  fs->codeABC(OP_VARARGPREP, nparams, 0, 0);
}


static void parlist (LexState *ls) {
  /* parlist -> [ {NAME ','} (NAME | '...') ] */
  FuncState *fs = ls->getFuncState();
  Proto *f = fs->getProto();
  int nparams = 0;
  int isvararg = 0;
  if (ls->getCurrentToken().token != ')') {  /* is 'parlist' not empty? */
    do {
      switch (ls->getCurrentToken().token) {
        case TK_NAME: {
          ls->new_localvar( ls->str_checkname());
          nparams++;
          break;
        }
        case TK_DOTS: {
          ls->nextToken();
          isvararg = 1;
          break;
        }
        default: ls->syntaxError( "<name> or '...' expected");
      }
    } while (!isvararg && ls->testnext( ','));
  }
  ls->adjustlocalvars(nparams);
  f->setNumParams(cast_byte(fs->getNumActiveVars()));
  if (isvararg)
    setvararg(fs, f->getNumParams());  /* declared vararg */
  fs->reserveregs(fs->getNumActiveVars());  /* reserve registers for parameters */
}


static void body (LexState *ls, expdesc *e, int ismethod, int line) {
  /* body ->  '(' parlist ')' block END */
  FuncState new_fs;
  BlockCnt bl;
  new_fs.setProto(addprototype(ls));
  new_fs.getProto()->setLineDefined(line);
  open_func(ls, &new_fs, &bl);
  ls->checknext( '(');
  if (ismethod) {
    new_localvarliteral(ls, "self");  /* create 'self' parameter */
    ls->adjustlocalvars(1);
  }
  parlist(ls);
  ls->checknext( ')');
  statlist(ls);
  new_fs.getProto()->setLastLineDefined(ls->getLineNumber());
  ls->check_match( TK_END, TK_FUNCTION, line);
  codeclosure(ls, e);
  close_func(ls);
}


static int explist (LexState *ls, expdesc *v) {
  /* explist -> expr { ',' expr } */
  int n = 1;  /* at least one expression */
  expr(ls, v);
  while (ls->testnext( ',')) {
    ls->getFuncState()->exp2nextreg(v);
    expr(ls, v);
    n++;
  }
  return n;
}


static void funcargs (LexState *ls, expdesc *f) {
  FuncState *fs = ls->getFuncState();
  expdesc args;
  int base, nparams;
  int line = ls->getLineNumber();
  switch (ls->getCurrentToken().token) {
    case '(': {  /* funcargs -> '(' [ explist ] ')' */
      ls->nextToken();
      if (ls->getCurrentToken().token == ')')  /* arg list is empty? */
        args.setKind(VVOID);
      else {
        explist(ls, &args);
        if (hasmultret(args.getKind()))
          luaK_setmultret(fs, &args);
      }
      ls->check_match( ')', '(', line);
      break;
    }
    case '{' /*}*/: {  /* funcargs -> constructor */
      constructor(ls, &args);
      break;
    }
    case TK_STRING: {  /* funcargs -> STRING */
      args.initString(ls->getCurrentToken().seminfo.ts);
      ls->nextToken();  /* must use 'seminfo' before 'next' */
      break;
    }
    default: {
      ls->syntaxError( "function arguments expected");
    }
  }
  lua_assert(f->getKind() == VNONRELOC);
  base = f->getInfo();  /* base register for call */
  if (hasmultret(args.getKind()))
    nparams = LUA_MULTRET;  /* open call */
  else {
    if (args.getKind() != VVOID)
      fs->exp2nextreg(&args);  /* close last argument */
    nparams = fs->getFreeReg() - (base+1);
  }
  f->init(VCALL, fs->codeABC(OP_CALL, base, nparams+1, 2));
  fs->fixline(line);
  /* call removes function and arguments and leaves one result (unless
     changed later) */
  fs->setFreeReg(cast_byte(base + 1));
}




/*
** {======================================================================
** Expression parsing
** =======================================================================
*/


static void primaryexp (LexState *ls, expdesc *v) {
  /* primaryexp -> NAME | '(' expr ')' */
  switch (ls->getCurrentToken().token) {
    case '(': {
      int line = ls->getLineNumber();
      ls->nextToken();
      expr(ls, v);
      ls->check_match( ')', '(', line);
      ls->getFuncState()->dischargevars(v);
      return;
    }
    case TK_NAME: {
      ls->singlevar(v);
      return;
    }
    default: {
      ls->syntaxError( "unexpected symbol");
    }
  }
}


static void suffixedexp (LexState *ls, expdesc *v) {
  /* suffixedexp ->
       primaryexp { '.' NAME | '[' exp ']' | ':' NAME funcargs | funcargs } */
  FuncState *fs = ls->getFuncState();
  primaryexp(ls, v);
  for (;;) {
    switch (ls->getCurrentToken().token) {
      case '.': {  /* fieldsel */
        fieldsel(ls, v);
        break;
      }
      case '[': {  /* '[' exp ']' */
        expdesc key;
        fs->exp2anyregup(v);
        yindex(ls, &key);
        fs->indexed(v, &key);
        break;
      }
      case ':': {  /* ':' NAME funcargs */
        expdesc key;
        ls->nextToken();
        ls->codename( &key);
        fs->self(v, &key);
        funcargs(ls, v);
        break;
      }
      case '(': case TK_STRING: case '{' /*}*/: {  /* funcargs */
        fs->exp2nextreg(v);
        funcargs(ls, v);
        break;
      }
      default: return;
    }
  }
}


static void simpleexp (LexState *ls, expdesc *v) {
  /* simpleexp -> FLT | INT | STRING | NIL | TRUE | FALSE | ... |
                  constructor | FUNCTION body | suffixedexp */
  switch (ls->getCurrentToken().token) {
    case TK_FLT: {
      v->init(VKFLT, 0);
      v->setFloatValue(ls->getCurrentToken().seminfo.r);
      break;
    }
    case TK_INT: {
      v->init(VKINT, 0);
      v->setIntValue(ls->getCurrentToken().seminfo.i);
      break;
    }
    case TK_STRING: {
      v->initString(ls->getCurrentToken().seminfo.ts);
      break;
    }
    case TK_NIL: {
      v->init(VNIL, 0);
      break;
    }
    case TK_TRUE: {
      v->init(VTRUE, 0);
      break;
    }
    case TK_FALSE: {
      v->init(VFALSE, 0);
      break;
    }
    case TK_DOTS: {  /* vararg */
      FuncState *fs = ls->getFuncState();
      check_condition(ls, fs->getProto()->getFlag() & PF_ISVARARG,
                      "cannot use '...' outside a vararg function");
      v->init(VVARARG, fs->codeABC(OP_VARARG, 0, 0, 1));
      break;
    }
    case '{' /*}*/: {  /* constructor */
      constructor(ls, v);
      return;
    }
    case TK_FUNCTION: {
      ls->nextToken();
      body(ls, v, 0, ls->getLineNumber());
      return;
    }
    default: {
      suffixedexp(ls, v);
      return;
    }
  }
  ls->nextToken();
}


static UnOpr getunopr (int op) {
  switch (op) {
    case TK_NOT: return OPR_NOT;
    case '-': return OPR_MINUS;
    case '~': return OPR_BNOT;
    case '#': return OPR_LEN;
    default: return OPR_NOUNOPR;
  }
}


static BinOpr getbinopr (int op) {
  switch (op) {
    case '+': return OPR_ADD;
    case '-': return OPR_SUB;
    case '*': return OPR_MUL;
    case '%': return OPR_MOD;
    case '^': return OPR_POW;
    case '/': return OPR_DIV;
    case TK_IDIV: return OPR_IDIV;
    case '&': return OPR_BAND;
    case '|': return OPR_BOR;
    case '~': return OPR_BXOR;
    case TK_SHL: return OPR_SHL;
    case TK_SHR: return OPR_SHR;
    case TK_CONCAT: return OPR_CONCAT;
    case TK_NE: return OPR_NE;
    case TK_EQ: return OPR_EQ;
    case '<': return OPR_LT;
    case TK_LE: return OPR_LE;
    case '>': return OPR_GT;
    case TK_GE: return OPR_GE;
    case TK_AND: return OPR_AND;
    case TK_OR: return OPR_OR;
    default: return OPR_NOBINOPR;
  }
}


/*
** Priority table for binary operators.
*/
static const struct {
  lu_byte left;  /* left priority for each binary operator */
  lu_byte right; /* right priority */
} priority[] = {  /* ORDER OPR */
   {10, 10}, {10, 10},           /* '+' '-' */
   {11, 11}, {11, 11},           /* '*' '%' */
   {14, 13},                  /* '^' (right associative) */
   {11, 11}, {11, 11},           /* '/' '//' */
   {6, 6}, {4, 4}, {5, 5},   /* '&' '|' '~' */
   {7, 7}, {7, 7},           /* '<<' '>>' */
   {9, 8},                   /* '..' (right associative) */
   {3, 3}, {3, 3}, {3, 3},   /* ==, <, <= */
   {3, 3}, {3, 3}, {3, 3},   /* ~=, >, >= */
   {2, 2}, {1, 1}            /* and, or */
};

#define UNARY_PRIORITY	12  /* priority for unary operators */


/*
** subexpr -> (simpleexp | unop subexpr) { binop subexpr }
** where 'binop' is any binary operator with a priority higher than 'limit'
*/
static BinOpr subexpr (LexState *ls, expdesc *v, int limit) {
  BinOpr op;
  UnOpr uop;
  enterlevel(ls);
  uop = getunopr(ls->getCurrentToken().token);
  if (uop != OPR_NOUNOPR) {  /* prefix (unary) operator? */
    int line = ls->getLineNumber();
    ls->nextToken();  /* skip operator */
    subexpr(ls, v, UNARY_PRIORITY);
    ls->getFuncState()->prefix(uop, v, line);
  }
  else simpleexp(ls, v);
  /* expand while operators have priorities higher than 'limit' */
  op = getbinopr(ls->getCurrentToken().token);
  while (op != OPR_NOBINOPR && priority[op].left > limit) {
    expdesc v2;
    BinOpr nextop;
    int line = ls->getLineNumber();
    ls->nextToken();  /* skip operator */
    ls->getFuncState()->infix(op, v);
    /* read sub-expression with higher priority */
    nextop = subexpr(ls, &v2, priority[op].right);
    ls->getFuncState()->posfix(op, v, &v2, line);
    op = nextop;
  }
  leavelevel(ls);
  return op;  /* return first untreated operator */
}


static void expr (LexState *ls, expdesc *v) {
  subexpr(ls, v, 0);
}

/* }==================================================================== */



/*
** {======================================================================
** Rules for Statements
** =======================================================================
*/


static void block (LexState *ls) {
  /* block -> statlist */
  FuncState *fs = ls->getFuncState();
  BlockCnt bl;
  enterblock(fs, &bl, 0);
  statlist(ls);
  leaveblock(fs);
}


/*
** structure to chain all variables in the left-hand side of an
** assignment
*/
struct LHS_assign {
  struct LHS_assign *prev;
  expdesc v;  /* variable (global, local, upvalue, or indexed) */
};


/*
** check whether, in an assignment to an upvalue/local variable, the
** upvalue/local variable is begin used in a previous assignment to a
** table. If so, save original upvalue/local value in a safe place and
** use this safe copy in the previous assignment.
*/
static void check_conflict (LexState *ls, struct LHS_assign *lh, expdesc *v) {
  FuncState *fs = ls->getFuncState();
  lu_byte extra = fs->getFreeReg();  /* eventual position to save local variable */
  int conflict = 0;
  for (; lh; lh = lh->prev) {  /* check all previous assignments */
    if (expdesc::isIndexed(lh->v.getKind())) {  /* assignment to table field? */
      if (lh->v.getKind() == VINDEXUP) {  /* is table an upvalue? */
        if (v->getKind() == VUPVAL && lh->v.getIndexedTableReg() == v->getInfo()) {
          conflict = 1;  /* table is the upvalue being assigned now */
          lh->v.setKind(VINDEXSTR);
          lh->v.setIndexedTableReg(extra);  /* assignment will use safe copy */
        }
      }
      else {  /* table is a register */
        if (v->getKind() == VLOCAL && lh->v.getIndexedTableReg() == v->getLocalRegister()) {
          conflict = 1;  /* table is the local being assigned now */
          lh->v.setIndexedTableReg(extra);  /* assignment will use safe copy */
        }
        /* is index the local being assigned? */
        if (lh->v.getKind() == VINDEXED && v->getKind() == VLOCAL &&
            lh->v.getIndexedKeyIndex() == v->getLocalRegister()) {
          conflict = 1;
          lh->v.setIndexedKeyIndex(extra);  /* previous assignment will use safe copy */
        }
      }
    }
  }
  if (conflict) {
    /* copy upvalue/local value to a temporary (in position 'extra') */
    if (v->getKind() == VLOCAL)
      fs->codeABC(OP_MOVE, extra, v->getLocalRegister(), 0);
    else
      fs->codeABC(OP_GETUPVAL, extra, v->getInfo(), 0);
    fs->reserveregs(1);
  }
}


/* Create code to store the "top" register in 'var' */
static void storevartop (FuncState *fs, expdesc *var) {
  expdesc e;
  e.init(VNONRELOC, fs->getFreeReg() - 1);
  fs->storevar(var, &e);  /* will also free the top register */
}


/*
** Parse and compile a multiple assignment. The first "variable"
** (a 'suffixedexp') was already read by the caller.
**
** assignment -> suffixedexp restassign
** restassign -> ',' suffixedexp restassign | '=' explist
*/
static void restassign (LexState *ls, struct LHS_assign *lh, int nvars) {
  expdesc e;
  check_condition(ls, expdesc::isVar(lh->v.getKind()), "syntax error");
  ls->check_readonly(&lh->v);
  if (ls->testnext( ',')) {  /* restassign -> ',' suffixedexp restassign */
    struct LHS_assign nv;
    nv.prev = lh;
    suffixedexp(ls, &nv.v);
    if (!expdesc::isIndexed(nv.v.getKind()))
      check_conflict(ls, lh, &nv.v);
    enterlevel(ls);  /* control recursion depth */
    restassign(ls, &nv, nvars+1);
    leavelevel(ls);
  }
  else {  /* restassign -> '=' explist */
    int nexps;
    ls->checknext( '=');
    nexps = explist(ls, &e);
    if (nexps != nvars)
      ls->adjust_assign(nvars, nexps, &e);
    else {
      ls->getFuncState()->setoneret(&e);  /* close last expression */
      ls->getFuncState()->storevar(&lh->v, &e);
      return;  /* avoid default */
    }
  }
  storevartop(ls->getFuncState(), &lh->v);  /* default assignment */
}


static int cond (LexState *ls) {
  /* cond -> exp */
  expdesc v;
  expr(ls, &v);  /* read condition */
  if (v.getKind() == VNIL) v.setKind(VFALSE);  /* 'falses' are all equal here */
  ls->getFuncState()->goiftrue(&v);
  return v.getFalseList();
}


static void gotostat (LexState *ls, int line) {
  TString *name = ls->str_checkname();  /* label's name */
  ls->newgotoentry(name, line);
}


/*
** Break statement. Semantically equivalent to "goto break".
*/
static void breakstat (LexState *ls, int line) {
  BlockCnt *bl;  /* to look for an enclosing loop */
  for (bl = ls->getFuncState()->getBlock(); bl != NULL; bl = bl->previous) {
    if (bl->isloop)  /* found one? */
      goto ok;
  }
  ls->syntaxError( "break outside loop");
 ok:
  bl->isloop = 2;  /* signal that block has pending breaks */
  ls->nextToken();  /* skip break */
  ls->newgotoentry(ls->getBreakName(), line);
}


/*
** Check whether there is already a label with the given 'name' at
** current function.
*/
static void checkrepeated (LexState *ls, TString *name) {
  Labeldesc *lb = ls->findlabel(name, ls->getFuncState()->getFirstLabel());
  if (l_unlikely(lb != NULL))  /* already defined? */
    ls->semerror( "label '%s' already defined on line %d",
                      getstr(name), lb->line);  /* error */
}


static void labelstat (LexState *ls, TString *name, int line) {
  /* label -> '::' NAME '::' */
  ls->checknext( TK_DBCOLON);  /* skip double colon */
  while (ls->getCurrentToken().token == ';' || ls->getCurrentToken().token == TK_DBCOLON)
    statement(ls);  /* skip other no-op statements */
  checkrepeated(ls, name);  /* check for repeated labels */
  ls->createlabel(name, line, block_follow(ls, 0));
}


static void whilestat (LexState *ls, int line) {
  /* whilestat -> WHILE cond DO block END */
  FuncState *fs = ls->getFuncState();
  int whileinit;
  int condexit;
  BlockCnt bl;
  ls->nextToken();  /* skip WHILE */
  whileinit = fs->getlabel();
  condexit = cond(ls);
  enterblock(fs, &bl, 1);
  ls->checknext( TK_DO);
  block(ls);
  luaK_jumpto(fs, whileinit);
  ls->check_match( TK_END, TK_WHILE, line);
  leaveblock(fs);
  fs->patchtohere(condexit);  /* false conditions finish the loop */
}


static void repeatstat (LexState *ls, int line) {
  /* repeatstat -> REPEAT block UNTIL cond */
  int condexit;
  FuncState *fs = ls->getFuncState();
  int repeat_init = fs->getlabel();
  BlockCnt bl1, bl2;
  enterblock(fs, &bl1, 1);  /* loop block */
  enterblock(fs, &bl2, 0);  /* scope block */
  ls->nextToken();  /* skip REPEAT */
  statlist(ls);
  ls->check_match( TK_UNTIL, TK_REPEAT, line);
  condexit = cond(ls);  /* read condition (inside scope block) */
  leaveblock(fs);  /* finish scope */
  if (bl2.upval) {  /* upvalues? */
    int exit = fs->jump();  /* normal exit must jump over fix */
    fs->patchtohere(condexit);  /* repetition must close upvalues */
    fs->codeABC(OP_CLOSE, fs->reglevel( bl2.nactvar), 0, 0);
    condexit = fs->jump();  /* repeat after closing upvalues */
    fs->patchtohere(exit);  /* normal exit comes to here */
  }
  fs->patchlist(condexit, repeat_init);  /* close the loop */
  leaveblock(fs);  /* finish loop */
}


/*
** Read an expression and generate code to put its results in next
** stack slot.
**
*/
static void exp1 (LexState *ls) {
  expdesc e;
  expr(ls, &e);
  ls->getFuncState()->exp2nextreg(&e);
  lua_assert(e.getKind() == VNONRELOC);
}


/*
** Fix for instruction at position 'pc' to jump to 'dest'.
** (Jump addresses are relative in Lua). 'back' true means
** a back jump.
*/
static void fixforjump (FuncState *fs, int pc, int dest, int back) {
  Instruction *jmp = &fs->getProto()->getCode()[pc];
  int offset = dest - (pc + 1);
  if (back)
    offset = -offset;
  if (l_unlikely(offset > MAXARG_Bx))
    fs->getLexState()->syntaxError("control structure too long");
  SETARG_Bx(*jmp, offset);
}


/*
** Generate code for a 'for' loop.
*/
static void forbody (LexState *ls, int base, int line, int nvars, int isgen) {
  /* forbody -> DO block */
  static const OpCode forprep[2] = {OP_FORPREP, OP_TFORPREP};
  static const OpCode forloop[2] = {OP_FORLOOP, OP_TFORLOOP};
  BlockCnt bl;
  FuncState *fs = ls->getFuncState();
  int prep, endfor;
  ls->checknext( TK_DO);
  prep = fs->codeABx(forprep[isgen], base, 0);
  fs->getFreeRegRef()--;  /* both 'forprep' remove one register from the stack */
  enterblock(fs, &bl, 0);  /* scope for declared variables */
  ls->adjustlocalvars(nvars);
  fs->reserveregs(nvars);
  block(ls);
  leaveblock(fs);  /* end of scope for declared variables */
  fixforjump(fs, prep, fs->getlabel(), 0);
  if (isgen) {  /* generic for? */
    fs->codeABC(OP_TFORCALL, base, 0, nvars);
    fs->fixline(line);
  }
  endfor = fs->codeABx(forloop[isgen], base, 0);
  fixforjump(fs, endfor, prep + 1, 1);
  fs->fixline(line);
}


static void fornum (LexState *ls, TString *varname, int line) {
  /* fornum -> NAME = exp,exp[,exp] forbody */
  FuncState *fs = ls->getFuncState();
  int base = fs->getFreeReg();
  new_localvarliteral(ls, "(for state)");
  new_localvarliteral(ls, "(for state)");
  ls->new_varkind( varname, RDKCONST);  /* control variable */
  ls->checknext( '=');
  exp1(ls);  /* initial value */
  ls->checknext( ',');
  exp1(ls);  /* limit */
  if (ls->testnext( ','))
    exp1(ls);  /* optional step */
  else {  /* default step = 1 */
    fs->intCode(fs->getFreeReg(), 1);
    fs->reserveregs(1);
  }
  ls->adjustlocalvars(2);  /* start scope for internal variables */
  forbody(ls, base, line, 1, 0);
}


static void forlist (LexState *ls, TString *indexname) {
  /* forlist -> NAME {,NAME} IN explist forbody */
  FuncState *fs = ls->getFuncState();
  expdesc e;
  int nvars = 4;  /* function, state, closing, control */
  int line;
  int base = fs->getFreeReg();
  /* create internal variables */
  new_localvarliteral(ls, "(for state)");  /* iterator function */
  new_localvarliteral(ls, "(for state)");  /* state */
  new_localvarliteral(ls, "(for state)");  /* closing var. (after swap) */
  ls->new_varkind( indexname, RDKCONST);  /* control variable */
  /* other declared variables */
  while (ls->testnext( ',')) {
    ls->new_localvar( ls->str_checkname());
    nvars++;
  }
  ls->checknext( TK_IN);
  line = ls->getLineNumber();
  ls->adjust_assign(4, explist(ls, &e), &e);
  ls->adjustlocalvars(3);  /* start scope for internal variables */
  fs->marktobeclosed();  /* last internal var. must be closed */
  fs->checkstack(2);  /* extra space to call iterator */
  forbody(ls, base, line, nvars - 3, 1);
}


static void forstat (LexState *ls, int line) {
  /* forstat -> FOR (fornum | forlist) END */
  FuncState *fs = ls->getFuncState();
  TString *varname;
  BlockCnt bl;
  enterblock(fs, &bl, 1);  /* scope for loop and control variables */
  ls->nextToken();  /* skip 'for' */
  varname = ls->str_checkname();  /* first variable name */
  switch (ls->getCurrentToken().token) {
    case '=': fornum(ls, varname, line); break;
    case ',': case TK_IN: forlist(ls, varname); break;
    default: ls->syntaxError( "'=' or 'in' expected");
  }
  ls->check_match( TK_END, TK_FOR, line);
  leaveblock(fs);  /* loop scope ('break' jumps to this point) */
}


static void test_then_block (LexState *ls, int *escapelist) {
  /* test_then_block -> [IF | ELSEIF] cond THEN block */
  FuncState *fs = ls->getFuncState();
  int condtrue;
  ls->nextToken();  /* skip IF or ELSEIF */
  condtrue = cond(ls);  /* read condition */
  ls->checknext( TK_THEN);
  block(ls);  /* 'then' part */
  if (ls->getCurrentToken().token == TK_ELSE ||
      ls->getCurrentToken().token == TK_ELSEIF)  /* followed by 'else'/'elseif'? */
    fs->concat(escapelist, fs->jump());  /* must jump over it */
  fs->patchtohere(condtrue);
}


static void ifstat (LexState *ls, int line) {
  /* ifstat -> IF cond THEN block {ELSEIF cond THEN block} [ELSE block] END */
  FuncState *fs = ls->getFuncState();
  int escapelist = NO_JUMP;  /* exit list for finished parts */
  test_then_block(ls, &escapelist);  /* IF cond THEN block */
  while (ls->getCurrentToken().token == TK_ELSEIF)
    test_then_block(ls, &escapelist);  /* ELSEIF cond THEN block */
  if (ls->testnext( TK_ELSE))
    block(ls);  /* 'else' part */
  ls->check_match( TK_END, TK_IF, line);
  fs->patchtohere(escapelist);  /* patch escape list to 'if' end */
}


static void localfunc (LexState *ls) {
  expdesc b;
  FuncState *fs = ls->getFuncState();
  int fvar = fs->getNumActiveVars();  /* function's variable index */
  ls->new_localvar( ls->str_checkname());  /* new local variable */
  ls->adjustlocalvars(1);  /* enter its scope */
  body(ls, &b, 0, ls->getLineNumber());  /* function created in next register */
  /* debug information will only see the variable after this point! */
  fs->localdebuginfo( fvar)->setStartPC(fs->getPC());
}


static lu_byte getvarattribute (LexState *ls, lu_byte df) {
  /* attrib -> ['<' NAME '>'] */
  if (ls->testnext( '<')) {
    TString *ts = ls->str_checkname();
    const char *attr = getstr(ts);
    ls->checknext( '>');
    if (strcmp(attr, "const") == 0)
      return RDKCONST;  /* read-only variable */
    else if (strcmp(attr, "close") == 0)
      return RDKTOCLOSE;  /* to-be-closed variable */
    else
      ls->semerror( "unknown attribute '%s'", attr);
  }
  return df;  /* return default value */
}


static void checktoclose (FuncState *fs, int level) {
  if (level != -1) {  /* is there a to-be-closed variable? */
    fs->marktobeclosed();
    fs->codeABC(OP_TBC, fs->reglevel( level), 0, 0);
  }
}


static void localstat (LexState *ls) {
  /* stat -> LOCAL NAME attrib { ',' NAME attrib } ['=' explist] */
  FuncState *fs = ls->getFuncState();
  int toclose = -1;  /* index of to-be-closed variable (if any) */
  Vardesc *var;  /* last variable */
  int vidx;  /* index of last variable */
  int nvars = 0;
  int nexps;
  expdesc e;
  /* get prefixed attribute (if any); default is regular local variable */
  lu_byte defkind = getvarattribute(ls, VDKREG);
  do {  /* for each variable */
    TString *vname = ls->str_checkname();  /* get its name */
    lu_byte kind = getvarattribute(ls, defkind);  /* postfixed attribute */
    vidx = ls->new_varkind( vname, kind);  /* predeclare it */
    if (kind == RDKTOCLOSE) {  /* to-be-closed? */
      if (toclose != -1)  /* one already present? */
        ls->semerror( "multiple to-be-closed variables in local list");
      toclose = fs->getNumActiveVars() + nvars;
    }
    nvars++;
  } while (ls->testnext( ','));
  if (ls->testnext( '='))  /* initialization? */
    nexps = explist(ls, &e);
  else {
    e.setKind(VVOID);
    nexps = 0;
  }
  var = fs->getlocalvardesc( vidx);  /* retrieve last variable */
  if (nvars == nexps &&  /* no adjustments? */
      var->vd.kind == RDKCONST &&  /* last variable is const? */
      fs->exp2const(&e, &var->k)) {  /* compile-time constant? */
    var->vd.kind = RDKCTC;  /* variable is a compile-time constant */
    ls->adjustlocalvars(nvars - 1);  /* exclude last variable */
    fs->getNumActiveVarsRef()++;  /* but count it */
  }
  else {
    ls->adjust_assign(nvars, nexps, &e);
    ls->adjustlocalvars(nvars);
  }
  checktoclose(fs, toclose);
}


static lu_byte getglobalattribute (LexState *ls, lu_byte df) {
  lu_byte kind = getvarattribute(ls, df);
  switch (kind) {
    case RDKTOCLOSE:
      ls->semerror( "global variables cannot be to-be-closed");
      return kind;  /* to avoid warnings */
    case RDKCONST:
      return GDKCONST;  /* adjust kind for global variable */
    default:
      return kind;
  }
}


static void globalnames (LexState *ls, lu_byte defkind) {
  FuncState *fs = ls->getFuncState();
  int nvars = 0;
  int lastidx;  /* index of last registered variable */
  do {  /* for each name */
    TString *vname = ls->str_checkname();
    lu_byte kind = getglobalattribute(ls, defkind);
    lastidx = ls->new_varkind( vname, kind);
    nvars++;
  } while (ls->testnext( ','));
  if (ls->testnext( '=')) {  /* initialization? */
    expdesc e;
    int i;
    int nexps = explist(ls, &e);  /* read list of expressions */
    ls->adjust_assign(nvars, nexps, &e);
    for (i = 0; i < nvars; i++) {  /* for each variable */
      expdesc var;
      TString *varname = fs->getlocalvardesc( lastidx - i)->vd.name;
      ls->buildglobal(varname, &var);  /* create global variable in 'var' */
      storevartop(fs, &var);
    }
  }
  fs->setNumActiveVars(cast_short(fs->getNumActiveVars() + nvars));  /* activate declaration */
}


static void globalstat (LexState *ls) {
  /* globalstat -> (GLOBAL) attrib '*'
     globalstat -> (GLOBAL) attrib NAME attrib {',' NAME attrib} */
  FuncState *fs = ls->getFuncState();
  /* get prefixed attribute (if any); default is regular global variable */
  lu_byte defkind = getglobalattribute(ls, GDKREG);
  if (!ls->testnext( '*'))
    globalnames(ls, defkind);
  else {
    /* use NULL as name to represent '*' entries */
    ls->new_varkind( NULL, defkind);
    fs->getNumActiveVarsRef()++;  /* activate declaration */
  }
}


static void globalfunc (LexState *ls, int line) {
  /* globalfunc -> (GLOBAL FUNCTION) NAME body */
  expdesc var, b;
  FuncState *fs = ls->getFuncState();
  TString *fname = ls->str_checkname();
  ls->new_varkind( fname, GDKREG);  /* declare global variable */
  fs->getNumActiveVarsRef()++;  /* enter its scope */
  ls->buildglobal(fname, &var);
  body(ls, &b, 0, ls->getLineNumber());  /* compile and return closure in 'b' */
  fs->storevar(&var, &b);
  fs->fixline(line);  /* definition "happens" in the first line */
}


static void globalstatfunc (LexState *ls, int line) {
  /* stat -> GLOBAL globalfunc | GLOBAL globalstat */
  ls->nextToken();  /* skip 'global' */
  if (ls->testnext( TK_FUNCTION))
    globalfunc(ls, line);
  else
    globalstat(ls);
}


static int funcname (LexState *ls, expdesc *v) {
  /* funcname -> NAME {fieldsel} [':' NAME] */
  int ismethod = 0;
  ls->singlevar(v);
  while (ls->getCurrentToken().token == '.')
    fieldsel(ls, v);
  if (ls->getCurrentToken().token == ':') {
    ismethod = 1;
    fieldsel(ls, v);
  }
  return ismethod;
}


static void funcstat (LexState *ls, int line) {
  /* funcstat -> FUNCTION funcname body */
  int ismethod;
  expdesc v, b;
  ls->nextToken();  /* skip FUNCTION */
  ismethod = funcname(ls, &v);
  ls->check_readonly(&v);
  body(ls, &b, ismethod, line);
  ls->getFuncState()->storevar(&v, &b);
  ls->getFuncState()->fixline(line);  /* definition "happens" in the first line */
}


static void exprstat (LexState *ls) {
  /* stat -> func | assignment */
  FuncState *fs = ls->getFuncState();
  struct LHS_assign v;
  suffixedexp(ls, &v.v);
  if (ls->getCurrentToken().token == '=' || ls->getCurrentToken().token == ',') { /* stat -> assignment ? */
    v.prev = NULL;
    restassign(ls, &v, 1);
  }
  else {  /* stat -> func */
    Instruction *inst;
    check_condition(ls, v.v.getKind() == VCALL, "syntax error");
    inst = &getinstruction(fs, &v.v);
    SETARG_C(*inst, 1);  /* call statement uses no results */
  }
}


static void retstat (LexState *ls) {
  /* stat -> RETURN [explist] [';'] */
  FuncState *fs = ls->getFuncState();
  expdesc e;
  int nret;  /* number of values being returned */
  int first = luaY_nvarstack(fs);  /* first slot to be returned */
  if (block_follow(ls, 1) || ls->getCurrentToken().token == ';')
    nret = 0;  /* return no values */
  else {
    nret = explist(ls, &e);  /* optional return values */
    if (hasmultret(e.getKind())) {
      luaK_setmultret(fs, &e);
      if (e.getKind() == VCALL && nret == 1 && !fs->getBlock()->insidetbc) {  /* tail call? */
        SET_OPCODE(getinstruction(fs,&e), OP_TAILCALL);
        lua_assert(GETARG_A(getinstruction(fs,&e)) == luaY_nvarstack(fs));
      }
      nret = LUA_MULTRET;  /* return all values */
    }
    else {
      if (nret == 1)  /* only one single value? */
        first = fs->exp2anyreg(&e);  /* can use original slot */
      else {  /* values must go to the top of the stack */
        fs->exp2nextreg(&e);
        lua_assert(nret == fs->getFreeReg() - first);
      }
    }
  }
  fs->ret(first, nret);
  ls->testnext( ';');  /* skip optional semicolon */
}


static void statement (LexState *ls) {
  int line = ls->getLineNumber();  /* may be needed for error messages */
  enterlevel(ls);
  switch (ls->getCurrentToken().token) {
    case ';': {  /* stat -> ';' (empty statement) */
      ls->nextToken();  /* skip ';' */
      break;
    }
    case TK_IF: {  /* stat -> ifstat */
      ifstat(ls, line);
      break;
    }
    case TK_WHILE: {  /* stat -> whilestat */
      whilestat(ls, line);
      break;
    }
    case TK_DO: {  /* stat -> DO block END */
      ls->nextToken();  /* skip DO */
      block(ls);
      ls->check_match( TK_END, TK_DO, line);
      break;
    }
    case TK_FOR: {  /* stat -> forstat */
      forstat(ls, line);
      break;
    }
    case TK_REPEAT: {  /* stat -> repeatstat */
      repeatstat(ls, line);
      break;
    }
    case TK_FUNCTION: {  /* stat -> funcstat */
      funcstat(ls, line);
      break;
    }
    case TK_LOCAL: {  /* stat -> localstat */
      ls->nextToken();  /* skip LOCAL */
      if (ls->testnext( TK_FUNCTION))  /* local function? */
        localfunc(ls);
      else
        localstat(ls);
      break;
    }
    case TK_GLOBAL: {  /* stat -> globalstatfunc */
      globalstatfunc(ls, line);
      break;
    }
    case TK_DBCOLON: {  /* stat -> label */
      ls->nextToken();  /* skip double colon */
      labelstat(ls, ls->str_checkname(), line);
      break;
    }
    case TK_RETURN: {  /* stat -> retstat */
      ls->nextToken();  /* skip RETURN */
      retstat(ls);
      break;
    }
    case TK_BREAK: {  /* stat -> breakstat */
      breakstat(ls, line);
      break;
    }
    case TK_GOTO: {  /* stat -> 'goto' NAME */
      ls->nextToken();  /* skip 'goto' */
      gotostat(ls, line);
      break;
    }
#if defined(LUA_COMPAT_GLOBAL)
    case TK_NAME: {
      /* compatibility code to parse global keyword when "global"
         is not reserved */
      if (ls->getCurrentToken().seminfo.ts == ls->getGlobalName()) {  /* current = "global"? */
        int lk = ls->lookaheadToken();
        if (lk == '<' || lk == TK_NAME || lk == '*' || lk == TK_FUNCTION) {
          /* 'global <attrib>' or 'global name' or 'global *' or
             'global function' */
          globalstatfunc(ls, line);
          break;
        }
      }  /* else... */
    }
#endif
    /* FALLTHROUGH */
    default: {  /* stat -> func | assignment */
      exprstat(ls);
      break;
    }
  }
  lua_assert(ls->getFuncState()->getProto()->getMaxStackSize() >= ls->getFuncState()->getFreeReg() &&
             ls->getFuncState()->getFreeReg() >= luaY_nvarstack(ls->getFuncState()));
  ls->getFuncState()->setFreeReg(luaY_nvarstack(ls->getFuncState()));  /* free registers */
  leavelevel(ls);
}

/* }====================================================================== */

/* }====================================================================== */


/*
** compiles the main function, which is a regular vararg function with an
** upvalue named LUA_ENV
*/
static void mainfunc (LexState *ls, FuncState *fs) {
  BlockCnt bl;
  Upvaldesc *env;
  open_func(ls, fs, &bl);
  setvararg(fs, 0);  /* main function is always declared vararg */
  env = fs->allocupvalue();  /* ...set environment upvalue */
  env->setInStack(1);
  env->setIndex(0);
  env->setKind(VDKREG);
  env->setName(ls->getEnvName());
  luaC_objbarrier(ls->getLuaState(), fs->getProto(), env->getName());
  ls->nextToken();  /* read first token */
  statlist(ls);  /* parse main body */
  ls->check( TK_EOS);
  close_func(ls);
}


LClosure *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff,
                       Dyndata *dyd, const char *name, int firstchar) {
  LexState lexstate;
  FuncState funcstate;
  LClosure *cl = LClosure::create(L, 1);  /* create main closure */
  setclLvalue2s(L, L->getTop().p, cl);  /* anchor it (to avoid being collected) */
  L->inctop();  /* Phase 25e */
  lexstate.setTable(luaH_new(L));  /* create table for scanner */
  sethvalue2s(L, L->getTop().p, lexstate.getTable());  /* anchor it */
  L->inctop();  /* Phase 25e */
  funcstate.setProto(luaF_newproto(L));
  cl->setProto(funcstate.getProto());
  luaC_objbarrier(L, cl, cl->getProto());
  funcstate.getProto()->setSource(luaS_new(L, name));  /* create and anchor TString */
  luaC_objbarrier(L, funcstate.getProto(), funcstate.getProto()->getSource());
  lexstate.setBuffer(buff);
  lexstate.setDyndata(dyd);
  dyd->actvar.n = dyd->gt.n = dyd->label.n = 0;
  lexstate.setInput(L, z, funcstate.getProto()->getSource(), firstchar);
  mainfunc(&lexstate, &funcstate);
  lua_assert(!funcstate.getPrev() && funcstate.getNumUpvalues() == 1 && !lexstate.getFuncState());
  /* all scopes should be correctly finished */
  lua_assert(dyd->actvar.n == 0 && dyd->gt.n == 0 && dyd->label.n == 0);
  L->getTop().p--;  /* remove scanner's table */
  return cl;  /* closure is on the stack, too */
}

