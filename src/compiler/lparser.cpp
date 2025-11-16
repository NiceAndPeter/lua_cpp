/*
** $Id: lparser.c $
** Lua Parser
** See Copyright Notice in lua.h
*/

#define lparser_c
#define LUA_CORE

#include "lprefix.h"


#include <climits>
#include <cstring>

#include "lua.h"

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
** (All converted to LexState private methods)
*/


l_noret Parser::error_expected(int token) {
  ls->syntaxError(
      luaO_pushfstring(ls->getLuaState(), "%s expected", ls->tokenToStr(token)));
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
int Parser::testnext(int c) {
  if (ls->getToken() == c) {
    ls->nextToken();
    return 1;
  }
  else return 0;
}


/*
** Check that next token is 'c'.
*/
void Parser::check(int c) {
  if (ls->getToken() != c)
    error_expected(c);
}


/*
** Check that next token is 'c' and skip it.
*/
void Parser::checknext(int c) {
  check(c);
  ls->nextToken();
}


#define check_condition(parser,c,msg)	{ if (!(c)) parser->getLexState()->syntaxError( msg); }


/*
** Check that next token is 'what' and skip it. In case of error,
** raise an error that the expected 'what' should match a 'who'
** in line 'where' (if that is not the current line).
*/
void Parser::check_match(int what, int who, int where) {
  if (l_unlikely(!testnext(what))) {
    if (where == ls->getLineNumber())  /* all in the same line? */
      error_expected(what);  /* do not need a complex message */
    else {
      ls->syntaxError(luaO_pushfstring(ls->getLuaState(),
             "%s expected (to close %s at line %d)",
              ls->tokenToStr(what), ls->tokenToStr(who), where));
    }
  }
}


TString *Parser::str_checkname() {
  TString *ts;
  check(TK_NAME);
  ts = ls->getSemInfo().ts;
  ls->nextToken();
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


void Parser::codename(expdesc *e) {
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
int Parser::new_varkind(TString *name, lu_byte kind) {
  Dyndata *dynData = ls->getDyndata();
  Vardesc *var;
  var = dynData->actvar().allocateNew();  /* LuaVector automatically grows */
  var->vd.kind = kind;  /* default */
  var->vd.name = name;
  return dynData->actvar().getN() - 1 - fs->getFirstLocal();
}


/*
** Create a new local variable with the given 'name' and regular kind.
*/
int Parser::new_localvar(TString *name) {
  return new_varkind(name, VDKREG);
}

#define new_localvarliteral(parser,v) \
    new_localvar(  \
      parser->getLexState()->newString( "" v, (sizeof(v)/sizeof(char)) - 1));



/*
** Return the "variable description" (Vardesc) of a given variable.
** (Unless noted otherwise, all variables are referred to by their
** compiler indices.)
*/
Vardesc *FuncState::getlocalvardesc(int vidx) {
  return &getLexState()->getDyndata()->actvar()[getFirstLocal() + vidx];
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
void Parser::check_readonly(expdesc *e) {
  // FuncState passed as parameter
  TString *varname = NULL;  /* to be set if variable is const */
  switch (e->getKind()) {
    case VCONST: {
      varname = ls->getDyndata()->actvar()[e->getInfo()].vd.name;
      break;
    }
    case VLOCAL: {
      Vardesc *vardesc = fs->getlocalvardesc(e->getLocalVarIndex());
      if (vardesc->vd.kind != VDKREG)  /* not a regular variable? */
        varname = vardesc->vd.name;
      break;
    }
    case VUPVAL: {
      Upvaldesc *up = &fs->getProto()->getUpvalues()[e->getInfo()];
      if (up->getKind() != VDKREG)
        varname = up->getName();
      break;
    }
    case VINDEXUP: case VINDEXSTR: case VINDEXED: {  /* global variable */
      if (e->isIndexedReadOnly())  /* read-only? */
        varname = tsvalue(&fs->getProto()->getConstants()[e->getIndexedStringKeyIndex()]);
      break;
    }
    default:
      lua_assert(e->getKind() == VINDEXI);  /* this one doesn't need any check */
      return;  /* integer index cannot be read-only */
  }
  if (varname)
    ls->semerror("attempt to assign to const variable '%s'", getstr(varname));
}


/*
** Start the scope for the last 'nvars' created variables.
*/
void Parser::adjustlocalvars(int nvars) {
  // FuncState passed as parameter
  int regLevel = fs->nvarstack();
  int i;
  for (i = 0; i < nvars; i++) {
    int vidx = fs->getNumActiveVarsRef()++;
    Vardesc *var = fs->getlocalvardesc(vidx);
    var->vd.ridx = cast_byte(regLevel++);
    var->vd.pidx = fs->registerlocalvar(var->vd.name);
    fs->checklimit(regLevel, MAXVARS, "local variables");
  }
}


/*
** Close the scope for all variables up to level 'tolevel'.
** (debug info.)
*/
void FuncState::removevars(int tolevel) {
  int current_n = getLexState()->getDyndata()->actvar().getN();
  getLexState()->getDyndata()->actvar().setN(current_n - (getNumActiveVars() - tolevel));
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


void Parser::buildglobal(TString *varname, expdesc *var) {
  // FuncState passed as parameter
  expdesc key;
  var->init(VGLOBAL, -1);  /* global by default */
  fs->singlevaraux(ls->getEnvName(), var, 1);  /* get environment variable */
  if (var->getKind() == VGLOBAL)
    ls->semerror("_ENV is global when accessing variable '%s'", getstr(varname));
  fs->exp2anyregup(var);  /* _ENV could be a constant */
  key.initString(varname);  /* key is variable name */
  fs->indexed(var, &key);  /* 'var' represents _ENV[varname] */
}


/*
** Find a variable with the given name, handling global variables too.
*/
void Parser::buildvar(TString *varname, expdesc *var) {
  // FuncState passed as parameter
  var->init(VGLOBAL, -1);  /* global by default */
  fs->singlevaraux(varname, var, 1);
  if (var->getKind() == VGLOBAL) {  /* global name? */
    int info = var->getInfo();
    /* global by default in the scope of a global declaration? */
    if (info == -2)
      ls->semerror("variable '%s' not declared", getstr(varname));
    buildglobal(varname, var);
    if (info != -1 && ls->getDyndata()->actvar()[info].vd.kind == GDKCONST)
      var->setIndexedReadOnly(1);  /* mark variable as read-only */
    else  /* anyway must be a global */
      lua_assert(info == -1 || ls->getDyndata()->actvar()[info].vd.kind == GDKREG);
  }
}


void Parser::singlevar(expdesc *var) {
  buildvar(str_checkname(), var);
}


/*
** Adjust the number of results from an expression list 'e' with 'nexps'
** expressions to 'nvars' values.
*/
void Parser::adjust_assign(int nvars, int nexps, expdesc *e) {
  // FuncState passed as parameter
  int needed = nvars - nexps;  /* extra values needed */
  if (hasmultret(e->getKind())) {  /* last expression has multiple returns? */
    int extra = needed + 1;  /* discount last expression itself */
    if (extra < 0)
      extra = 0;
    fs->setreturns(e, extra);  /* last exp. provides the difference */
  }
  else {
    if (e->getKind() != VVOID)  /* at least one expression? */
      fs->exp2nextreg(e);  /* close last expression */
    if (needed > 0)  /* missing values? */
      fs->nil(fs->getFreeReg(), needed);  /* complete with nils */
  }
  if (needed > 0)
    fs->reserveregs(needed);  /* registers for extra values */
  else  /* adding 'needed' is actually a subtraction */
    fs->setFreeReg(cast_byte(fs->getFreeReg() + needed));  /* remove extra values */
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
l_noret LexState::jumpscopeerror(FuncState *funcState, Labeldesc *gt) {
  TString *tsname = funcState->getlocalvardesc(gt->nactvar)->vd.name;
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
void LexState::closegoto(FuncState *funcState, int g, Labeldesc *label, int bup) {
  int i;
  Labellist *gl = &getDyndata()->gt;  /* list of gotos */
  Labeldesc *gt = &(*gl)[g];  /* goto to be resolved */
  lua_assert(eqstr(gt->name, label->name));
  if (l_unlikely(gt->nactvar < label->nactvar))  /* enter some scope? */
    jumpscopeerror(funcState, gt);
  if (gt->close ||
      (label->nactvar < gt->nactvar && bup)) {  /* needs close? */
    lu_byte stklevel = funcState->reglevel(label->nactvar);
    /* move jump to CLOSE position */
    funcState->getProto()->getCode()[gt->pc + 1] = funcState->getProto()->getCode()[gt->pc];
    /* put CLOSE instruction at original position */
    funcState->getProto()->getCode()[gt->pc] = CREATE_ABCk(OP_CLOSE, stklevel, 0, 0, 0);
    gt->pc++;  /* must point to jump instruction */
  }
  funcState->patchlist(gt->pc, label->pc);  /* goto jumps to label */
  for (i = g; i < gl->getN() - 1; i++)  /* remove goto from pending list */
    (*gl)[i] = (*gl)[i + 1];
  gl->setN(gl->getN() - 1);
}


/*
** Search for an active label with the given name, starting at
** index 'ilb' (so that it can search for all labels in current block
** or all labels in current function).
*/
Labeldesc *LexState::findlabel(TString *name, int ilb) {
  Dyndata *dynData = getDyndata();
  for (; ilb < dynData->label.getN(); ilb++) {
    Labeldesc *lb = &dynData->label[ilb];
    if (eqstr(lb->name, name))  /* correct label? */
      return lb;
  }
  return NULL;  /* label not found */
}


/*
** Adds a new label/goto in the corresponding list.
*/
int LexState::newlabelentry(FuncState *funcState, Labellist *l, TString *name, int line, int pc) {
  int n = l->getN();
  Labeldesc* desc = l->allocateNew();  /* LuaVector automatically grows */
  desc->name = name;
  desc->line = line;
  desc->nactvar = funcState->getNumActiveVars();
  desc->close = 0;
  desc->pc = pc;
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
int Parser::newgotoentry(TString *name, int line) {
  // FuncState passed as parameter
  int pc = fs->jump();  /* create jump */
  fs->codeABC(OP_CLOSE, 0, 1, 0);  /* spaceholder, marked as dead */
  return ls->newlabelentry(fs, &ls->getDyndata()->gt, name, line, pc);
}


/*
** Create a new label with the given 'name' at the given 'line'.
** 'last' tells whether label is the last non-op statement in its
** block. Solves all pending gotos to this new label and adds
** a close instruction if necessary.
** Returns true iff it added a close instruction.
*/
void LexState::createlabel(FuncState *funcState, TString *name, int line, int last) {
  // FuncState passed as parameter
  Labellist *ll = &getDyndata()->label;
  int l = newlabelentry(funcState, ll, name, line, funcState->getlabel());
  if (last) {  /* label is last no-op statement in the block? */
    /* assume that locals are already out of scope */
    (*ll)[l].nactvar = funcState->getBlock()->nactvar;
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
  while (igt < gl->getN()) {   /* for each pending goto */
    Labeldesc *gt = &(*gl)[igt];
    /* search for a matching label in the current block */
    Labeldesc *lb = lexState->findlabel(gt->name, blockCnt->firstlabel);
    if (lb != NULL)  /* found a match? */
      lexState->closegoto(this, igt, lb, blockCnt->upval);  /* close and remove goto */
    else {  /* adjust 'goto' for outer block */
      /* block has variables to be closed and goto escapes the scope of
         some variable? */
      if (blockCnt->upval && reglevel(gt->nactvar) > outlevel)
        gt->close = 1;  /* jump may need a close */
      gt->nactvar = blockCnt->nactvar;  /* correct level for outer block */
      igt++;  /* go to next goto */
    }
  }
  lexState->getDyndata()->label.setN(blockCnt->firstlabel);  /* remove local labels */
}


void FuncState::enterblock(BlockCnt *blk, lu_byte isloop) {
  blk->isloop = isloop;
  blk->nactvar = getNumActiveVars();
  blk->firstlabel = getLexState()->getDyndata()->label.getN();
  blk->firstgoto = getLexState()->getDyndata()->gt.getN();
  blk->upval = 0;
  /* inherit 'insidetbc' from enclosing block */
  blk->insidetbc = (getBlock() != NULL && getBlock()->insidetbc);
  blk->previous = getBlock();  /* link block in function's block list */
  setBlock(blk);
  lua_assert(getFreeReg() == luaY_nvarstack(this));
}


/*
** generates an error for an undefined 'goto'.
*/
l_noret LexState::undefgoto([[maybe_unused]] FuncState *funcState, Labeldesc *gt) {
  /* breaks are checked when created, cannot be undefined */
  lua_assert(!eqstr(gt->name, getBreakName()));
  semerror("no visible label '%s' for <goto> at line %d",
           getstr(gt->name), gt->line);
}


void FuncState::leaveblock() {
  BlockCnt *blk = getBlock();
  LexState *lexstate = getLexState();
  lu_byte stklevel = reglevel(blk->nactvar);  /* level outside block */
  if (blk->previous && blk->upval)  /* need a 'close'? */
    codeABC(OP_CLOSE, stklevel, 0, 0);
  setFreeReg(stklevel);  /* free registers */
  removevars(blk->nactvar);  /* remove block locals */
  lua_assert(blk->nactvar == getNumActiveVars());  /* back to level on entry */
  if (blk->isloop == 2)  /* has to fix pending breaks? */
    lexstate->createlabel(this, lexstate->getBreakName(), 0, 0);
  solvegotos(blk);
  if (blk->previous == NULL) {  /* was it the last block? */
    if (blk->firstgoto < lexstate->getDyndata()->gt.getN())  /* still pending gotos? */
      lexstate->undefgoto(this, &lexstate->getDyndata()->gt[blk->firstgoto]);  /* error */
  }
  setBlock(blk->previous);  /* current block now is previous one */
}


/*
** adds a new prototype into list of prototypes
*/
Proto *Parser::addprototype() {
  Proto *clp;
  lua_State *state = ls->getLuaState();
  FuncState *funcstate = fs;
  Proto *proto = funcstate->getProto();  /* prototype of current function */
  if (funcstate->getNP() >= proto->getProtosSize()) {
    int oldsize = proto->getProtosSize();
    luaM_growvector(state, proto->getProtosRef(), funcstate->getNP(), proto->getProtosSizeRef(), Proto *, MAXARG_Bx, "functions");
    while (oldsize < proto->getProtosSize())
      proto->getProtos()[oldsize++] = NULL;
  }
  proto->getProtos()[funcstate->getNPRef()++] = clp = luaF_newproto(state);
  luaC_objbarrier(state, proto, clp);
  return clp;
}


/*
** codes instruction to create new closure in parent function.
** The OP_CLOSURE instruction uses the last available register,
** so that, if it invokes the GC, the GC knows which registers
** are in use at that time.

*/
void Parser::codeclosure( expdesc *v) {
  FuncState *funcstate = fs->getPrev();
  v->init(VRELOC, funcstate->codeABx(OP_CLOSURE, 0, funcstate->getNP() - 1));
  funcstate->exp2nextreg(v);  /* fix it at the last register */
}


void Parser::open_func(FuncState *funcstate, BlockCnt *bl) {
  lua_State *state = ls->getLuaState();
  Proto *f = funcstate->getProto();
  funcstate->setPrev(fs);  /* linked list of funcstates */
  funcstate->setLexState(ls);
  setFuncState(funcstate);
  funcstate->setPC(0);
  funcstate->setPreviousLine(f->getLineDefined());
  funcstate->setInstructionsWithAbs(0);
  funcstate->setLastTarget(0);
  funcstate->setFreeReg(0);
  funcstate->setNK(0);
  funcstate->setNAbsLineInfo(0);
  funcstate->setNP(0);
  funcstate->setNumUpvalues(0);
  funcstate->setNumDebugVars(0);
  funcstate->setNumActiveVars(0);
  funcstate->setNeedClose(0);
  funcstate->setFirstLocal(ls->getDyndata()->actvar().getN());
  funcstate->setFirstLabel(ls->getDyndata()->label.getN());
  funcstate->setBlock(NULL);
  f->setSource(ls->getSource());
  luaC_objbarrier(state, f, f->getSource());
  f->setMaxStackSize(2);  /* registers 0/1 are always valid */
  funcstate->setKCache(luaH_new(state));  /* create table for function */
  sethvalue2s(state, state->getTop().p, funcstate->getKCache());  /* anchor it */
  state->inctop();  /* Phase 25e */
  funcstate->enterblock(bl, 0);
}


void Parser::close_func() {
  lua_State *state = ls->getLuaState();
  FuncState *funcstate = fs;
  Proto *f = funcstate->getProto();
  funcstate->ret(luaY_nvarstack(funcstate), 0);  /* final return */
  funcstate->leaveblock();
  lua_assert(funcstate->getBlock() == NULL);
  funcstate->finish();
  luaM_shrinkvector(state, f->getCodeRef(), f->getCodeSizeRef(), funcstate->getPC(), Instruction);
  luaM_shrinkvector(state, f->getLineInfoRef(), f->getLineInfoSizeRef(), funcstate->getPC(), ls_byte);
  luaM_shrinkvector(state, f->getAbsLineInfoRef(), f->getAbsLineInfoSizeRef(),
                       funcstate->getNAbsLineInfo(), AbsLineInfo);
  luaM_shrinkvector(state, f->getConstantsRef(), f->getConstantsSizeRef(), funcstate->getNK(), TValue);
  luaM_shrinkvector(state, f->getProtosRef(), f->getProtosSizeRef(), funcstate->getNP(), Proto *);
  luaM_shrinkvector(state, f->getLocVarsRef(), f->getLocVarsSizeRef(), funcstate->getNumDebugVars(), LocVar);
  luaM_shrinkvector(state, f->getUpvaluesRef(), f->getUpvaluesSizeRef(), funcstate->getNumUpvalues(), Upvaldesc);
  setFuncState(funcstate->getPrev());
  state->getTop().p--;  /* pop kcache table */
  luaC_checkGC(state);
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
int Parser::block_follow( int withuntil) {
  switch (ls->getToken()) {
    case TK_ELSE: case TK_ELSEIF:
    case TK_END: case TK_EOS:
      return 1;
    case TK_UNTIL: return withuntil;
    default: return 0;
  }
}


void Parser::statlist() {
  /* statlist -> { stat [';'] } */
  while (!block_follow(1)) {
    if (ls->getToken() == TK_RETURN) {
      statement();
      return;  /* 'return' must be last statement */
    }
    statement();
  }
}


void Parser::fieldsel( expdesc *v) {
  /* fieldsel -> ['.' | ':'] NAME */
  FuncState *funcstate = fs;
  expdesc key;
  funcstate->exp2anyregup(v);
  ls->nextToken();  /* skip the dot or colon */
  codename( &key);
  funcstate->indexed(v, &key);
}


void Parser::yindex( expdesc *v) {
  /* index -> '[' expr ']' */
  ls->nextToken();  /* skip the '[' */
  expr(v);
  fs->exp2val(v);
  checknext( ']');
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


void Parser::recfield( ConsControl *cc) {
  /* recfield -> (NAME | '['exp']') = exp */
  FuncState *funcstate = fs;
  lu_byte reg = fs->getFreeReg();
  expdesc tab, key, val;
  if (ls->getToken() == TK_NAME)
    codename( &key);
  else  /* ls->getToken() == '[' */
    yindex(&key);
  cc->nh++;
  checknext( '=');
  tab = *cc->t;
  funcstate->indexed(&tab, &key);
  expr(&val);
  funcstate->storevar(&tab, &val);
  funcstate->setFreeReg(reg);  /* free registers */
}


void FuncState::closelistfield(ConsControl *cc) {
  lua_assert(cc->tostore > 0);
  exp2nextreg(&cc->v);
  cc->v.setKind(VVOID);
  if (cc->tostore >= cc->maxtostore) {
    setlist(cc->t->getInfo(), cc->na, cc->tostore);  /* flush */
    cc->na += cc->tostore;
    cc->tostore = 0;  /* no more items pending */
  }
}


void FuncState::lastlistfield(ConsControl *cc) {
  if (cc->tostore == 0) return;
  if (hasmultret(cc->v.getKind())) {
    setreturns(&cc->v, LUA_MULTRET);
    setlist(cc->t->getInfo(), cc->na, LUA_MULTRET);
    cc->na--;  /* do not count last expression (unknown number of elements) */
  }
  else {
    if (cc->v.getKind() != VVOID)
      exp2nextreg(&cc->v);
    setlist(cc->t->getInfo(), cc->na, cc->tostore);
  }
  cc->na += cc->tostore;
}


void Parser::listfield( ConsControl *cc) {
  /* listfield -> exp */
  expr(&cc->v);
  cc->tostore++;
}


void Parser::field( ConsControl *cc) {
  /* field -> listfield | recfield */
  switch(ls->getToken()) {
    case TK_NAME: {  /* may be 'listfield' or 'recfield' */
      if (ls->lookaheadToken() != '=')  /* expression? */
        listfield(cc);
      else
        recfield(cc);
      break;
    }
    case '[': {
      recfield(cc);
      break;
    }
    default: {
      listfield(cc);
      break;
    }
  }
}


/*
** Compute a limit for how many registers a constructor can use before
** emitting a 'SETLIST' instruction, based on how many registers are
** available.
*/
int FuncState::maxtostore() {
  int numfreeregs = MAX_FSTACK - getFreeReg();
  if (numfreeregs >= 160)  /* "lots" of registers? */
    return numfreeregs / 5;  /* use up to 1/5 of them */
  else if (numfreeregs >= 80)  /* still "enough" registers? */
    return 10;  /* one 'SETLIST' instruction for each 10 values */
  else  /* save registers for potential more nesting */
    return 1;
}


void Parser::constructor( expdesc *table_exp) {
  /* constructor -> '{' [ field { sep field } [sep] ] '}'
     sep -> ',' | ';' */
  FuncState *funcstate = fs;
  int line = ls->getLineNumber();
  int pc = funcstate->codevABCk(OP_NEWTABLE, 0, 0, 0, 0);
  ConsControl cc;
  funcstate->code(0);  /* space for extra arg. */
  cc.na = cc.nh = cc.tostore = 0;
  cc.t = table_exp;
  table_exp->init(VNONRELOC, funcstate->getFreeReg());  /* table will be at stack top */
  funcstate->reserveregs(1);
  cc.v.init(VVOID, 0);  /* no value (yet) */
  checknext( '{' /*}*/);
  cc.maxtostore = funcstate->maxtostore();
  do {
    if (ls->getToken() == /*{*/ '}') break;
    if (cc.v.getKind() != VVOID)  /* is there a previous list item? */
      funcstate->closelistfield(&cc);  /* close it */
    field(&cc);
    luaY_checklimit(funcstate, cc.tostore + cc.na + cc.nh, MAX_CNST,
                    "items in a constructor");
  } while (testnext( ',') || testnext( ';'));
  check_match( /*{*/ '}', '{' /*}*/, line);
  funcstate->lastlistfield(&cc);
  funcstate->settablesize(pc, table_exp->getInfo(), cc.na, cc.nh);
}

/* }====================================================================== */


void FuncState::setvararg(int nparams) {
  getProto()->setFlag(getProto()->getFlag() | PF_ISVARARG);
  codeABC(OP_VARARGPREP, nparams, 0, 0);
}


void Parser::parlist() {
  /* parlist -> [ {NAME ','} (NAME | '...') ] */
  FuncState *funcstate = fs;
  Proto *f = funcstate->getProto();
  int nparams = 0;
  int isvararg = 0;
  if (ls->getToken() != ')') {  /* is 'parlist' not empty? */
    do {
      switch (ls->getToken()) {
        case TK_NAME: {
          new_localvar( str_checkname());
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
    } while (!isvararg && testnext( ','));
  }
  adjustlocalvars(nparams);
  f->setNumParams(cast_byte(funcstate->getNumActiveVars()));
  if (isvararg)
    funcstate->setvararg(f->getNumParams());  /* declared vararg */
  funcstate->reserveregs(funcstate->getNumActiveVars());  /* reserve registers for parameters */
}


void Parser::body( expdesc *e, int ismethod, int line) {
  /* body ->  '(' parlist ')' block END */
  FuncState new_fs;
  BlockCnt bl;
  new_fs.setProto(addprototype());
  new_fs.getProto()->setLineDefined(line);
  open_func(&new_fs, &bl);
  checknext( '(');
  if (ismethod) {
    new_localvarliteral(this, "self");  /* create 'self' parameter */
    adjustlocalvars(1);
  }
  parlist();
  checknext( ')');
  statlist();
  new_fs.getProto()->setLastLineDefined(ls->getLineNumber());
  check_match( TK_END, TK_FUNCTION, line);
  codeclosure(e);
  close_func();
}


int Parser::explist( expdesc *v) {
  /* explist -> expr { ',' expr } */
  int n = 1;  /* at least one expression */
  expr(v);
  while (testnext( ',')) {
    fs->exp2nextreg(v);
    expr(v);
    n++;
  }
  return n;
}


void Parser::funcargs( expdesc *f) {
  FuncState *funcstate = fs;
  expdesc args;
  int base, nparams;
  int line = ls->getLineNumber();
  switch (ls->getToken()) {
    case '(': {  /* funcargs -> '(' [ explist ] ')' */
      ls->nextToken();
      if (ls->getToken() == ')')  /* arg list is empty? */
        args.setKind(VVOID);
      else {
        explist(&args);
        if (hasmultret(args.getKind()))
          funcstate->setreturns(&args, LUA_MULTRET);
      }
      check_match( ')', '(', line);
      break;
    }
    case '{' /*}*/: {  /* funcargs -> constructor */
      constructor(&args);
      break;
    }
    case TK_STRING: {  /* funcargs -> STRING */
      args.initString(ls->getSemInfo().ts);
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
      funcstate->exp2nextreg(&args);  /* close last argument */
    nparams = funcstate->getFreeReg() - (base+1);
  }
  f->init(VCALL, funcstate->codeABC(OP_CALL, base, nparams+1, 2));
  funcstate->fixline(line);
  /* call removes function and arguments and leaves one result (unless
     changed later) */
  funcstate->setFreeReg(cast_byte(base + 1));
}




/*
** {======================================================================
** Expression parsing
** =======================================================================
*/


void Parser::primaryexp( expdesc *v) {
  /* primaryexp -> NAME | '(' expr ')' */
  switch (ls->getToken()) {
    case '(': {
      int line = ls->getLineNumber();
      ls->nextToken();
      expr(v);
      check_match( ')', '(', line);
      fs->dischargevars(v);
      return;
    }
    case TK_NAME: {
      singlevar(v);
      return;
    }
    default: {
      ls->syntaxError( "unexpected symbol");
    }
  }
}


void Parser::suffixedexp( expdesc *v) {
  /* suffixedexp ->
       primaryexp { '.' NAME | '[' exp ']' | ':' NAME funcargs | funcargs } */
  FuncState *funcstate = fs;
  primaryexp(v);
  for (;;) {
    switch (ls->getToken()) {
      case '.': {  /* fieldsel */
        fieldsel(v);
        break;
      }
      case '[': {  /* '[' exp ']' */
        expdesc key;
        funcstate->exp2anyregup(v);
        yindex(&key);
        funcstate->indexed(v, &key);
        break;
      }
      case ':': {  /* ':' NAME funcargs */
        expdesc key;
        ls->nextToken();
        codename( &key);
        funcstate->self(v, &key);
        funcargs(v);
        break;
      }
      case '(': case TK_STRING: case '{' /*}*/: {  /* funcargs */
        funcstate->exp2nextreg(v);
        funcargs(v);
        break;
      }
      default: return;
    }
  }
}


void Parser::simpleexp( expdesc *v) {
  /* simpleexp -> FLT | INT | STRING | NIL | TRUE | FALSE | ... |
                  constructor | FUNCTION body | suffixedexp */
  switch (ls->getToken()) {
    case TK_FLT: {
      v->init(VKFLT, 0);
      v->setFloatValue(ls->getSemInfo().r);
      break;
    }
    case TK_INT: {
      v->init(VKINT, 0);
      v->setIntValue(ls->getSemInfo().i);
      break;
    }
    case TK_STRING: {
      v->initString(ls->getSemInfo().ts);
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
      FuncState *funcstate = fs;
      check_condition(this, funcstate->getProto()->getFlag() & PF_ISVARARG,
                      "cannot use '...' outside a vararg function");
      v->init(VVARARG, funcstate->codeABC(OP_VARARG, 0, 0, 1));
      break;
    }
    case '{' /*}*/: {  /* constructor */
      constructor(v);
      return;
    }
    case TK_FUNCTION: {
      ls->nextToken();
      body(v, 0, ls->getLineNumber());
      return;
    }
    default: {
      suffixedexp(v);
      return;
    }
  }
  ls->nextToken();
}


inline UnOpr getunopr (int op) noexcept {
  switch (op) {
    case TK_NOT: return OPR_NOT;
    case '-': return OPR_MINUS;
    case '~': return OPR_BNOT;
    case '#': return OPR_LEN;
    default: return OPR_NOUNOPR;
  }
}


inline BinOpr getbinopr (int op) noexcept {
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
BinOpr Parser::subexpr( expdesc *v, int limit) {
  BinOpr op;
  UnOpr uop;
  enterlevel(ls);
  uop = getunopr(ls->getToken());
  if (uop != OPR_NOUNOPR) {  /* prefix (unary) operator? */
    int line = ls->getLineNumber();
    ls->nextToken();  /* skip operator */
    subexpr(v, UNARY_PRIORITY);
    fs->prefix(uop, v, line);
  }
  else simpleexp(v);
  /* expand while operators have priorities higher than 'limit' */
  op = getbinopr(ls->getToken());
  while (op != OPR_NOBINOPR && priority[op].left > limit) {
    expdesc v2;
    BinOpr nextop;
    int line = ls->getLineNumber();
    ls->nextToken();  /* skip operator */
    fs->infix(op, v);
    /* read sub-expression with higher priority */
    nextop = subexpr(&v2, priority[op].right);
    fs->posfix(op, v, &v2, line);
    op = nextop;
  }
  leavelevel(ls);
  return op;  /* return first untreated operator */
}


void Parser::expr( expdesc *v) {
  subexpr(v, 0);
}

/* }==================================================================== */



/*
** {======================================================================
** Rules for Statements
** =======================================================================
*/


void Parser::block() {
  /* block -> statlist */
  FuncState *funcstate = fs;
  BlockCnt bl;
  funcstate->enterblock(&bl, 0);
  statlist();
  funcstate->leaveblock();
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
void Parser::check_conflict( struct LHS_assign *lh, expdesc *v) {
  FuncState *funcstate = fs;
  lu_byte extra = funcstate->getFreeReg();  /* eventual position to save local variable */
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
      funcstate->codeABC(OP_MOVE, extra, v->getLocalRegister(), 0);
    else
      funcstate->codeABC(OP_GETUPVAL, extra, v->getInfo(), 0);
    funcstate->reserveregs(1);
  }
}


/* Create code to store the "top" register in 'var' */
void FuncState::storevartop(expdesc *var) {
  expdesc e;
  e.init(VNONRELOC, getFreeReg() - 1);
  storevar(var, &e);  /* will also free the top register */
}


/*
** Parse and compile a multiple assignment. The first "variable"
** (a 'suffixedexp') was already read by the caller.
**
** assignment -> suffixedexp restassign
** restassign -> ',' suffixedexp restassign | '=' explist
*/
void Parser::restassign( struct LHS_assign *lh, int nvars) {
  expdesc e;
  check_condition(this, expdesc::isVar(lh->v.getKind()), "syntax error");
  check_readonly(&lh->v);
  if (testnext( ',')) {  /* restassign -> ',' suffixedexp restassign */
    struct LHS_assign nv;
    nv.prev = lh;
    suffixedexp(&nv.v);
    if (!expdesc::isIndexed(nv.v.getKind()))
      check_conflict(lh, &nv.v);
    enterlevel(ls);  /* control recursion depth */
    restassign(&nv, nvars+1);
    leavelevel(ls);
  }
  else {  /* restassign -> '=' explist */
    int nexps;
    checknext( '=');
    nexps = explist(&e);
    if (nexps != nvars)
      adjust_assign(nvars, nexps, &e);
    else {
      fs->setoneret(&e);  /* close last expression */
      fs->storevar(&lh->v, &e);
      return;  /* avoid default */
    }
  }
  fs->storevartop(&lh->v);  /* default assignment */
}


int Parser::cond() {
  /* cond -> exp */
  expdesc v;
  expr(&v);  /* read condition */
  if (v.getKind() == VNIL) v.setKind(VFALSE);  /* 'falses' are all equal here */
  fs->goiftrue(&v);
  return v.getFalseList();
}


void Parser::gotostat( int line) {
  TString *name = str_checkname();  /* label's name */
  newgotoentry(name, line);
}


/*
** Break statement. Semantically equivalent to "goto break".
*/
void Parser::breakstat( int line) {
  BlockCnt *bl;  /* to look for an enclosing loop */
  for (bl = fs->getBlock(); bl != NULL; bl = bl->previous) {
    if (bl->isloop)  /* found one? */
      goto ok;
  }
  ls->syntaxError( "break outside loop");
 ok:
  bl->isloop = 2;  /* signal that block has pending breaks */
  ls->nextToken();  /* skip break */
  newgotoentry(ls->getBreakName(), line);
}


/*
** Check whether there is already a label with the given 'name' at
** current function.
*/
void Parser::checkrepeated( TString *name) {
  Labeldesc *lb = ls->findlabel(name, fs->getFirstLabel());
  if (l_unlikely(lb != NULL))  /* already defined? */
    ls->semerror( "label '%s' already defined on line %d",
                      getstr(name), lb->line);  /* error */
}


void Parser::labelstat( TString *name, int line) {
  /* label -> '::' NAME '::' */
  checknext( TK_DBCOLON);  /* skip double colon */
  while (ls->getToken() == ';' || ls->getToken() == TK_DBCOLON)
    statement();  /* skip other no-op statements */
  checkrepeated(name);  /* check for repeated labels */
  ls->createlabel(fs, name, line, block_follow(0));
}


void Parser::whilestat( int line) {
  /* whilestat -> WHILE cond DO block END */
  FuncState *funcstate = fs;
  int whileinit;
  int condexit;
  BlockCnt bl;
  ls->nextToken();  /* skip WHILE */
  whileinit = funcstate->getlabel();
  condexit = cond();
  funcstate->enterblock(&bl, 1);
  checknext( TK_DO);
  block();
  funcstate->patchlist(funcstate->jump(), whileinit);
  check_match( TK_END, TK_WHILE, line);
  funcstate->leaveblock();
  funcstate->patchtohere(condexit);  /* false conditions finish the loop */
}


void Parser::repeatstat( int line) {
  /* repeatstat -> REPEAT block UNTIL cond */
  int condexit;
  FuncState *funcstate = fs;
  int repeat_init = funcstate->getlabel();
  BlockCnt bl1, bl2;
  funcstate->enterblock(&bl1, 1);  /* loop block */
  funcstate->enterblock(&bl2, 0);  /* scope block */
  ls->nextToken();  /* skip REPEAT */
  statlist();
  check_match( TK_UNTIL, TK_REPEAT, line);
  condexit = cond();  /* read condition (inside scope block) */
  funcstate->leaveblock();  /* finish scope */
  if (bl2.upval) {  /* upvalues? */
    int exit = funcstate->jump();  /* normal exit must jump over fix */
    funcstate->patchtohere(condexit);  /* repetition must close upvalues */
    funcstate->codeABC(OP_CLOSE, funcstate->reglevel(bl2.nactvar), 0, 0);
    condexit = funcstate->jump();  /* repeat after closing upvalues */
    funcstate->patchtohere(exit);  /* normal exit comes to here */
  }
  funcstate->patchlist(condexit, repeat_init);  /* close the loop */
  funcstate->leaveblock();  /* finish loop */
}


/*
** Read an expression and generate code to put its results in next
** stack slot.
**
*/
void Parser::exp1() {
  expdesc e;
  expr(&e);
  fs->exp2nextreg(&e);
  lua_assert(e.getKind() == VNONRELOC);
}


/*
** Fix for instruction at position 'pcpos' to jump to 'dest'.
** (Jump addresses are relative in Lua). 'back' true means
** a back jump.
*/
void FuncState::fixforjump(int pcpos, int dest, int back) {
  Instruction *jmp = &getProto()->getCode()[pcpos];
  int offset = dest - (pcpos + 1);
  if (back)
    offset = -offset;
  if (l_unlikely(offset > MAXARG_Bx))
    getLexState()->syntaxError("control structure too long");
  SETARG_Bx(*jmp, offset);
}


/*
** Generate code for a 'for' loop.
*/
void Parser::forbody( int base, int line, int nvars, int isgen) {
  /* forbody -> DO block */
  static const OpCode forprep[2] = {OP_FORPREP, OP_TFORPREP};
  static const OpCode forloop[2] = {OP_FORLOOP, OP_TFORLOOP};
  BlockCnt bl;
  FuncState *funcstate = fs;
  int prep, endfor;
  checknext( TK_DO);
  prep = funcstate->codeABx(forprep[isgen], base, 0);
  funcstate->getFreeRegRef()--;  /* both 'forprep' remove one register from the stack */
  funcstate->enterblock(&bl, 0);  /* scope for declared variables */
  adjustlocalvars(nvars);
  funcstate->reserveregs(nvars);
  block();
  funcstate->leaveblock();  /* end of scope for declared variables */
  funcstate->fixforjump(prep, funcstate->getlabel(), 0);
  if (isgen) {  /* generic for? */
    funcstate->codeABC(OP_TFORCALL, base, 0, nvars);
    funcstate->fixline(line);
  }
  endfor = funcstate->codeABx(forloop[isgen], base, 0);
  funcstate->fixforjump(endfor, prep + 1, 1);
  funcstate->fixline(line);
}


void Parser::fornum( TString *varname, int line) {
  /* fornum -> NAME = exp,exp[,exp] forbody */
  FuncState *funcstate = fs;
  int base = funcstate->getFreeReg();
  new_localvarliteral(this, "(for state)");
  new_localvarliteral(this, "(for state)");
  new_varkind( varname, RDKCONST);  /* control variable */
  checknext( '=');
  exp1();  /* initial value */
  checknext( ',');
  exp1();  /* limit */
  if (testnext( ','))
    exp1();  /* optional step */
  else {  /* default step = 1 */
    funcstate->intCode(funcstate->getFreeReg(), 1);
    funcstate->reserveregs(1);
  }
  adjustlocalvars(2);  /* start scope for internal variables */
  forbody(base, line, 1, 0);
}


void Parser::forlist( TString *indexname) {
  /* forlist -> NAME {,NAME} IN explist forbody */
  FuncState *funcstate = fs;
  expdesc e;
  int nvars = 4;  /* function, state, closing, control */
  int line;
  int base = funcstate->getFreeReg();
  /* create internal variables */
  new_localvarliteral(this, "(for state)");  /* iterator function */
  new_localvarliteral(this, "(for state)");  /* state */
  new_localvarliteral(this, "(for state)");  /* closing var. (after swap) */
  new_varkind( indexname, RDKCONST);  /* control variable */
  /* other declared variables */
  while (testnext( ',')) {
    new_localvar( str_checkname());
    nvars++;
  }
  checknext( TK_IN);
  line = ls->getLineNumber();
  adjust_assign(4, explist(&e), &e);
  adjustlocalvars(3);  /* start scope for internal variables */
  funcstate->marktobeclosed();  /* last internal var. must be closed */
  funcstate->checkstack(2);  /* extra space to call iterator */
  forbody(base, line, nvars - 3, 1);
}


void Parser::forstat( int line) {
  /* forstat -> FOR (fornum | forlist) END */
  FuncState *funcstate = fs;
  TString *varname;
  BlockCnt bl;
  funcstate->enterblock(&bl, 1);  /* scope for loop and control variables */
  ls->nextToken();  /* skip 'for' */
  varname = str_checkname();  /* first variable name */
  switch (ls->getToken()) {
    case '=': fornum(varname, line); break;
    case ',': case TK_IN: forlist(varname); break;
    default: ls->syntaxError( "'=' or 'in' expected");
  }
  check_match( TK_END, TK_FOR, line);
  funcstate->leaveblock();  /* loop scope ('break' jumps to this point) */
}


void Parser::test_then_block( int *escapelist) {
  /* test_then_block -> [IF | ELSEIF] cond THEN block */
  FuncState *funcstate = fs;
  int condtrue;
  ls->nextToken();  /* skip IF or ELSEIF */
  condtrue = cond();  /* read condition */
  checknext( TK_THEN);
  block();  /* 'then' part */
  if (ls->getToken() == TK_ELSE ||
      ls->getToken() == TK_ELSEIF)  /* followed by 'else'/'elseif'? */
    funcstate->concat(escapelist, funcstate->jump());  /* must jump over it */
  funcstate->patchtohere(condtrue);
}


void Parser::ifstat( int line) {
  /* ifstat -> IF cond THEN block {ELSEIF cond THEN block} [ELSE block] END */
  FuncState *funcstate = fs;
  int escapelist = NO_JUMP;  /* exit list for finished parts */
  test_then_block(&escapelist);  /* IF cond THEN block */
  while (ls->getToken() == TK_ELSEIF)
    test_then_block(&escapelist);  /* ELSEIF cond THEN block */
  if (testnext( TK_ELSE))
    block();  /* 'else' part */
  check_match( TK_END, TK_IF, line);
  funcstate->patchtohere(escapelist);  /* patch escape list to 'if' end */
}


void Parser::localfunc() {
  expdesc b;
  FuncState *funcstate = fs;
  int fvar = funcstate->getNumActiveVars();  /* function's variable index */
  new_localvar( str_checkname());  /* new local variable */
  adjustlocalvars(1);  /* enter its scope */
  body(&b, 0, ls->getLineNumber());  /* function created in next register */
  /* debug information will only see the variable after this point! */
  funcstate->localdebuginfo( fvar)->setStartPC(funcstate->getPC());
}


lu_byte Parser::getvarattribute( lu_byte df) {
  /* attrib -> ['<' NAME '>'] */
  if (testnext( '<')) {
    TString *ts = str_checkname();
    const char *attr = getstr(ts);
    checknext( '>');
    if (strcmp(attr, "const") == 0)
      return RDKCONST;  /* read-only variable */
    else if (strcmp(attr, "close") == 0)
      return RDKTOCLOSE;  /* to-be-closed variable */
    else
      ls->semerror( "unknown attribute '%s'", attr);
  }
  return df;  /* return default value */
}


void FuncState::checktoclose(int level) {
  if (level != -1) {  /* is there a to-be-closed variable? */
    marktobeclosed();
    codeABC(OP_TBC, reglevel(level), 0, 0);
  }
}


void Parser::localstat() {
  /* stat -> LOCAL NAME attrib { ',' NAME attrib } ['=' explist] */
  FuncState *funcstate = fs;
  int toclose = -1;  /* index of to-be-closed variable (if any) */
  Vardesc *var;  /* last variable */
  int vidx;  /* index of last variable */
  int nvars = 0;
  int nexps;
  expdesc e;
  /* get prefixed attribute (if any); default is regular local variable */
  lu_byte defkind = getvarattribute(VDKREG);
  do {  /* for each variable */
    TString *vname = str_checkname();  /* get its name */
    lu_byte kind = getvarattribute(defkind);  /* postfixed attribute */
    vidx = new_varkind( vname, kind);  /* predeclare it */
    if (kind == RDKTOCLOSE) {  /* to-be-closed? */
      if (toclose != -1)  /* one already present? */
        ls->semerror( "multiple to-be-closed variables in local list");
      toclose = funcstate->getNumActiveVars() + nvars;
    }
    nvars++;
  } while (testnext( ','));
  if (testnext( '='))  /* initialization? */
    nexps = explist(&e);
  else {
    e.setKind(VVOID);
    nexps = 0;
  }
  var = funcstate->getlocalvardesc( vidx);  /* retrieve last variable */
  if (nvars == nexps &&  /* no adjustments? */
      var->vd.kind == RDKCONST &&  /* last variable is const? */
      funcstate->exp2const(&e, &var->k)) {  /* compile-time constant? */
    var->vd.kind = RDKCTC;  /* variable is a compile-time constant */
    adjustlocalvars(nvars - 1);  /* exclude last variable */
    funcstate->getNumActiveVarsRef()++;  /* but count it */
  }
  else {
    adjust_assign(nvars, nexps, &e);
    adjustlocalvars(nvars);
  }
  funcstate->checktoclose(toclose);
}


lu_byte Parser::getglobalattribute( lu_byte df) {
  lu_byte kind = getvarattribute(df);
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


void Parser::globalnames( lu_byte defkind) {
  FuncState *funcstate = fs;
  int nvars = 0;
  int lastidx;  /* index of last registered variable */
  do {  /* for each name */
    TString *vname = str_checkname();
    lu_byte kind = getglobalattribute(defkind);
    lastidx = new_varkind( vname, kind);
    nvars++;
  } while (testnext( ','));
  if (testnext( '=')) {  /* initialization? */
    expdesc e;
    int i;
    int nexps = explist(&e);  /* read list of expressions */
    adjust_assign(nvars, nexps, &e);
    for (i = 0; i < nvars; i++) {  /* for each variable */
      expdesc var;
      TString *varname = funcstate->getlocalvardesc(lastidx - i)->vd.name;
      buildglobal(varname, &var);  /* create global variable in 'var' */
      funcstate->storevartop(&var);
    }
  }
  funcstate->setNumActiveVars(cast_short(funcstate->getNumActiveVars() + nvars));  /* activate declaration */
}


void Parser::globalstat() {
  /* globalstat -> (GLOBAL) attrib '*'
     globalstat -> (GLOBAL) attrib NAME attrib {',' NAME attrib} */
  FuncState *funcstate = fs;
  /* get prefixed attribute (if any); default is regular global variable */
  lu_byte defkind = getglobalattribute(GDKREG);
  if (!testnext( '*'))
    globalnames(defkind);
  else {
    /* use NULL as name to represent '*' entries */
    new_varkind( NULL, defkind);
    funcstate->getNumActiveVarsRef()++;  /* activate declaration */
  }
}


void Parser::globalfunc( int line) {
  /* globalfunc -> (GLOBAL FUNCTION) NAME body */
  expdesc var, b;
  FuncState *funcstate = fs;
  TString *fname = str_checkname();
  new_varkind( fname, GDKREG);  /* declare global variable */
  funcstate->getNumActiveVarsRef()++;  /* enter its scope */
  buildglobal(fname, &var);
  body(&b, 0, ls->getLineNumber());  /* compile and return closure in 'b' */
  funcstate->storevar(&var, &b);
  funcstate->fixline(line);  /* definition "happens" in the first line */
}


void Parser::globalstatfunc( int line) {
  /* stat -> GLOBAL globalfunc | GLOBAL globalstat */
  ls->nextToken();  /* skip 'global' */
  if (testnext( TK_FUNCTION))
    globalfunc(line);
  else
    globalstat();
}


int Parser::funcname( expdesc *v) {
  /* funcname -> NAME {fieldsel} [':' NAME] */
  int ismethod = 0;
  singlevar(v);
  while (ls->getToken() == '.')
    fieldsel(v);
  if (ls->getToken() == ':') {
    ismethod = 1;
    fieldsel(v);
  }
  return ismethod;
}


void Parser::funcstat( int line) {
  /* funcstat -> FUNCTION funcname body */
  int ismethod;
  expdesc v, b;
  ls->nextToken();  /* skip FUNCTION */
  ismethod = funcname(&v);
  check_readonly(&v);
  body(&b, ismethod, line);
  fs->storevar(&v, &b);
  fs->fixline(line);  /* definition "happens" in the first line */
}


void Parser::exprstat() {
  /* stat -> func | assignment */
  FuncState *funcstate = fs;
  struct LHS_assign v;
  suffixedexp(&v.v);
  if (ls->getToken() == '=' || ls->getToken() == ',') { /* stat -> assignment ? */
    v.prev = NULL;
    restassign(&v, 1);
  }
  else {  /* stat -> func */
    Instruction *inst;
    check_condition(this, v.v.getKind() == VCALL, "syntax error");
    inst = &getinstruction(funcstate, &v.v);
    SETARG_C(*inst, 1);  /* call statement uses no results */
  }
}


void Parser::retstat() {
  /* stat -> RETURN [explist] [';'] */
  FuncState *funcstate = fs;
  expdesc e;
  int nret;  /* number of values being returned */
  int first = luaY_nvarstack(funcstate);  /* first slot to be returned */
  if (block_follow(1) || ls->getToken() == ';')
    nret = 0;  /* return no values */
  else {
    nret = explist(&e);  /* optional return values */
    if (hasmultret(e.getKind())) {
      funcstate->setreturns(&e, LUA_MULTRET);
      if (e.getKind() == VCALL && nret == 1 && !funcstate->getBlock()->insidetbc) {  /* tail call? */
        SET_OPCODE(getinstruction(funcstate,&e), OP_TAILCALL);
        lua_assert(GETARG_A(getinstruction(funcstate,&e)) == luaY_nvarstack(funcstate));
      }
      nret = LUA_MULTRET;  /* return all values */
    }
    else {
      if (nret == 1)  /* only one single value? */
        first = funcstate->exp2anyreg(&e);  /* can use original slot */
      else {  /* values must go to the top of the stack */
        funcstate->exp2nextreg(&e);
        lua_assert(nret == funcstate->getFreeReg() - first);
      }
    }
  }
  funcstate->ret(first, nret);
  testnext( ';');  /* skip optional semicolon */
}


void Parser::statement() {
  int line = ls->getLineNumber();  /* may be needed for error messages */
  enterlevel(ls);
  switch (ls->getToken()) {
    case ';': {  /* stat -> ';' (empty statement) */
      ls->nextToken();  /* skip ';' */
      break;
    }
    case TK_IF: {  /* stat -> ifstat */
      ifstat(line);
      break;
    }
    case TK_WHILE: {  /* stat -> whilestat */
      whilestat(line);
      break;
    }
    case TK_DO: {  /* stat -> DO block END */
      ls->nextToken();  /* skip DO */
      block();
      check_match( TK_END, TK_DO, line);
      break;
    }
    case TK_FOR: {  /* stat -> forstat */
      forstat(line);
      break;
    }
    case TK_REPEAT: {  /* stat -> repeatstat */
      repeatstat(line);
      break;
    }
    case TK_FUNCTION: {  /* stat -> funcstat */
      funcstat(line);
      break;
    }
    case TK_LOCAL: {  /* stat -> localstat */
      ls->nextToken();  /* skip LOCAL */
      if (testnext( TK_FUNCTION))  /* local function? */
        localfunc();
      else
        localstat();
      break;
    }
    case TK_GLOBAL: {  /* stat -> globalstatfunc */
      globalstatfunc(line);
      break;
    }
    case TK_DBCOLON: {  /* stat -> label */
      ls->nextToken();  /* skip double colon */
      labelstat(str_checkname(), line);
      break;
    }
    case TK_RETURN: {  /* stat -> retstat */
      ls->nextToken();  /* skip RETURN */
      retstat();
      break;
    }
    case TK_BREAK: {  /* stat -> breakstat */
      breakstat(line);
      break;
    }
    case TK_GOTO: {  /* stat -> 'goto' NAME */
      ls->nextToken();  /* skip 'goto' */
      gotostat(line);
      break;
    }
#if defined(LUA_COMPAT_GLOBAL)
    case TK_NAME: {
      /* compatibility code to parse global keyword when "global"
         is not reserved */
      if (ls->getSemInfo().ts == ls->getGlobalName()) {  /* current = "global"? */
        int lk = ls->lookaheadToken();
        if (lk == '<' || lk == TK_NAME || lk == '*' || lk == TK_FUNCTION) {
          /* 'global <attrib>' or 'global name' or 'global *' or
             'global function' */
          globalstatfunc(line);
          break;
        }
      }  /* else... */
    }
#endif
    /* FALLTHROUGH */
    default: {  /* stat -> func | assignment */
      exprstat();
      break;
    }
  }
  lua_assert(fs->getProto()->getMaxStackSize() >= fs->getFreeReg() &&
             fs->getFreeReg() >= luaY_nvarstack(fs));
  fs->setFreeReg(luaY_nvarstack(fs));  /* free registers */
  leavelevel(ls);
}

/* }====================================================================== */

/* }====================================================================== */


/*
** compiles the main function, which is a regular vararg function with an
** upvalue named LUA_ENV
*/
void Parser::mainfunc(FuncState *funcstate) {
  BlockCnt bl;
  Upvaldesc *env;
  open_func(funcstate, &bl);
  funcstate->setvararg(0);  /* main function is always declared vararg */
  env = funcstate->allocupvalue();  /* ...set environment upvalue */
  env->setInStack(1);
  env->setIndex(0);
  env->setKind(VDKREG);
  env->setName(ls->getEnvName());
  luaC_objbarrier(ls->getLuaState(), funcstate->getProto(), env->getName());
  ls->nextToken();  /* read first token */
  statlist();  /* parse main body */
  check( TK_EOS);
  close_func();
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
  dyd->actvar().setN(0);
  dyd->gt.setN(0);
  dyd->label.setN(0);
  lexstate.setInput(L, z, funcstate.getProto()->getSource(), firstchar);
  Parser parser(&lexstate, nullptr);
  parser.mainfunc(&funcstate);
  lua_assert(!funcstate.getPrev() && funcstate.getNumUpvalues() == 1);
  /* all scopes should be correctly finished */
  lua_assert(dyd->actvar().getN() == 0 && dyd->gt.getN() == 0 && dyd->label.getN() == 0);
  L->getTop().p--;  /* remove scanner's table */
  return cl;  /* closure is on the stack, too */
}

