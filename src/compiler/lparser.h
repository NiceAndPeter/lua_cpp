/*
** $Id: lparser.h $
** Lua Parser
** See Copyright Notice in lua.h
*/

#ifndef lparser_h
#define lparser_h

#include "llimits.h"
#include "lobject.h"
#include "lopcodes.h"
#include "ltm.h"
#include "lzio.h"

/*
** grep "ORDER OPR" if you change these enums  (ORDER OP)
*/
typedef enum BinOpr {
  /* arithmetic operators */
  OPR_ADD, OPR_SUB, OPR_MUL, OPR_MOD, OPR_POW,
  OPR_DIV, OPR_IDIV,
  /* bitwise operators */
  OPR_BAND, OPR_BOR, OPR_BXOR,
  OPR_SHL, OPR_SHR,
  /* string operator */
  OPR_CONCAT,
  /* comparison operators */
  OPR_EQ, OPR_LT, OPR_LE,
  OPR_NE, OPR_GT, OPR_GE,
  /* logical operators */
  OPR_AND, OPR_OR,
  OPR_NOBINOPR
} BinOpr;

typedef enum UnOpr { OPR_MINUS, OPR_BNOT, OPR_NOT, OPR_LEN, OPR_NOUNOPR } UnOpr;

/*
** Expression and variable descriptor.
** Code generation for variables and expressions can be delayed to allow
** optimizations; An 'expdesc' structure describes a potentially-delayed
** variable/expression. It has a description of its "main" value plus a
** list of conditional jumps that can also produce its value (generated
** by short-circuit operators 'and'/'or').
*/

/* kinds of variables/expressions */
typedef enum {
  VVOID,  /* when 'expdesc' describes the last expression of a list,
             this kind means an empty list (so, no expression) */
  VNIL,  /* constant nil */
  VTRUE,  /* constant true */
  VFALSE,  /* constant false */
  VK,  /* constant in 'k'; info = index of constant in 'k' */
  VKFLT,  /* floating constant; nval = numerical float value */
  VKINT,  /* integer constant; ival = numerical integer value */
  VKSTR,  /* string constant; strval = TString address;
             (string is fixed by the scanner) */
  VNONRELOC,  /* expression has its value in a fixed register;
                 info = result register */
  VLOCAL,  /* local variable; var.ridx = register index;
              var.vidx = relative index in 'actvar.arr'  */
  VGLOBAL,  /* global variable;
               info = relative index in 'actvar.arr' (or -1 for
                      implicit declaration) */
  VUPVAL,  /* upvalue variable; info = index of upvalue in 'upvalues' */
  VCONST,  /* compile-time <const> variable;
              info = absolute index in 'actvar.arr'  */
  VINDEXED,  /* indexed variable;
                ind.t = table register;
                ind.idx = key's R index;
                ind.ro = true if it represents a read-only global;
                ind.keystr = if key is a string, index in 'k' of that string;
                             -1 if key is not a string */
  VINDEXUP,  /* indexed upvalue;
                ind.idx = key's K index;
                ind.* as in VINDEXED */
  VINDEXI, /* indexed variable with constant integer;
                ind.t = table register;
                ind.idx = key's value */
  VINDEXSTR, /* indexed variable with literal string;
                ind.idx = key's K index;
                ind.* as in VINDEXED */
  VJMP,  /* expression is a test/comparison;
            info = pc of corresponding jump instruction */
  VRELOC,  /* expression can put result in any register;
              info = instruction pc */
  VCALL,  /* expression is a function call; info = instruction pc */
  VVARARG  /* vararg expression; info = instruction pc */
} expkind;


/* Phase 44.6: vkisvar and vkisindexed macros replaced with expdesc static methods:
** - vkisvar(k) → expdesc::isVar(k)
** - vkisindexed(k) → expdesc::isIndexed(k)
*/

class expdesc {
private:
  expkind k;
  union {
    lua_Integer ival;    /* for VKINT */
    lua_Number nval;  /* for VKFLT */
    TString *strval;  /* for VKSTR */
    int info;  /* for generic use */
    struct {  /* for indexed variables */
      short idx;  /* index (R or "long" K) */
      lu_byte t;  /* table (register or upvalue) */
      lu_byte ro;  /* true if variable is read-only */
      int keystr;  /* index in 'k' of string key, or -1 if not a string */
    } ind;
    struct {  /* for local variables */
      lu_byte ridx;  /* register holding the variable */
      short vidx;  /* index in 'actvar.arr' */
    } var;
  } u;
  int t;  /* patch list of 'exit when true' */
  int f;  /* patch list of 'exit when false' */

public:
  // Inline accessors
  expkind getKind() const noexcept { return k; }
  void setKind(expkind kind) noexcept { k = kind; }
  bool isConstant() const noexcept { return k == VNIL || k == VFALSE || k == VTRUE || k == VKINT || k == VKFLT || k == VKSTR; }

  // Union field accessors (generic/constant values)
  int getInfo() const noexcept { return u.info; }
  void setInfo(int i) noexcept { u.info = i; }
  lua_Integer getIntValue() const noexcept { return u.ival; }
  void setIntValue(lua_Integer i) noexcept { u.ival = i; }
  lua_Number getFloatValue() const noexcept { return u.nval; }
  void setFloatValue(lua_Number n) noexcept { u.nval = n; }
  TString* getStringValue() const noexcept { return u.strval; }
  void setStringValue(TString* s) noexcept { u.strval = s; }

  // Indexed variable accessors (u.ind)
  short getIndexedKeyIndex() const noexcept { return u.ind.idx; }
  void setIndexedKeyIndex(short idx) noexcept { u.ind.idx = idx; }
  lu_byte getIndexedTableReg() const noexcept { return u.ind.t; }
  void setIndexedTableReg(lu_byte treg) noexcept { u.ind.t = treg; }
  lu_byte isIndexedReadOnly() const noexcept { return u.ind.ro; }
  void setIndexedReadOnly(lu_byte ro) noexcept { u.ind.ro = ro; }
  int getIndexedStringKeyIndex() const noexcept { return u.ind.keystr; }
  void setIndexedStringKeyIndex(int keystr) noexcept { u.ind.keystr = keystr; }

  // Local variable accessors (u.var)
  lu_byte getLocalRegister() const noexcept { return u.var.ridx; }
  void setLocalRegister(lu_byte ridx) noexcept { u.var.ridx = ridx; }
  short getLocalVarIndex() const noexcept { return u.var.vidx; }
  void setLocalVarIndex(short vidx) noexcept { u.var.vidx = vidx; }

  // Patch lists
  int getTrueList() const noexcept { return t; }
  void setTrueList(int list) noexcept { t = list; }
  int* getTrueListRef() noexcept { return &t; }
  int getFalseList() const noexcept { return f; }
  void setFalseList(int list) noexcept { f = list; }
  int* getFalseListRef() noexcept { return &f; }

  // Phase 44.6: Expression kind helper methods

  // Check if expression kind is a variable
  static bool isVar(expkind kind) noexcept {
    return VLOCAL <= kind && kind <= VINDEXSTR;
  }

  // Check if expression kind is indexed
  static bool isIndexed(expkind kind) noexcept {
    return VINDEXED <= kind && kind <= VINDEXSTR;
  }

  // Phase 84: Expression initialization methods
  void init(expkind kind, int i);
  void initString(TString *s);
};


/* kinds of variables */
#define VDKREG		0   /* regular local */
#define RDKCONST	1   /* local constant */
#define RDKTOCLOSE	2   /* to-be-closed */
#define RDKCTC		3   /* local compile-time constant */
#define GDKREG		4   /* regular global */
#define GDKCONST	5   /* global constant */

/* Phase 44.6: varinreg and varglobal macros replaced with Vardesc methods:
** - varinreg(v) → v->isInReg()
** - varglobal(v) → v->isGlobal()
*/

/* description of an active variable */
class Vardesc {
public:
  union {
    struct {
      Value value_;  /* value for compile-time constant */
      lu_byte tt_;   /* type tag for compile-time constant */
      lu_byte kind;
      lu_byte ridx;  /* register holding the variable */
      short pidx;  /* index of the variable in the Proto's 'locvars' array */
      TString *name;  /* variable name */
    } vd;
    TValue k;  /* constant value (if any) */
  };

  // Phase 44.6: Variable kind helper methods

  // Check if variable is in register
  bool isInReg() const noexcept {
    return vd.kind <= RDKTOCLOSE;
  }

  // Check if variable is global
  bool isGlobal() const noexcept {
    return vd.kind >= GDKREG;
  }
};



/* description of pending goto statements and label statements */
typedef struct Labeldesc {
  TString *name;  /* label identifier */
  int pc;  /* position in code */
  int line;  /* line where it appeared */
  short nactvar;  /* number of active variables in that position */
  lu_byte close;  /* true for goto that escapes upvalues */
} Labeldesc;


/* list of labels or gotos */
typedef struct Labellist {
  Labeldesc *arr;  /* array */
  int n;  /* number of entries in use */
  int size;  /* array size */
} Labellist;


/* dynamic structures used by the parser */
typedef struct Dyndata {
  struct {  /* list of all active local variables */
    Vardesc *arr;
    int n;
    int size;
  } actvar;
  Labellist gt;  /* list of pending gotos */
  Labellist label;   /* list of active labels */
} Dyndata;


/* control of blocks */
struct BlockCnt;  /* defined in lparser.c */


/* state needed to generate code for a given function */
class FuncState {
private:
  Proto *f;  /* current function header */
  struct FuncState *prev;  /* enclosing function */
  struct LexState *ls;  /* lexical state */
  struct BlockCnt *bl;  /* chain of current blocks */
  Table *kcache;  /* cache for reusing constants */
  int pc;  /* next position to code (equivalent to 'ncode') */
  int lasttarget;   /* 'label' of last 'jump label' */
  int previousline;  /* last line that was saved in 'lineinfo' */
  int nk;  /* number of elements in 'k' */
  int np;  /* number of elements in 'p' */
  int nabslineinfo;  /* number of elements in 'abslineinfo' */
  int firstlocal;  /* index of first local var (in Dyndata array) */
  int firstlabel;  /* index of first label (in 'dyd->label->arr') */
  short ndebugvars;  /* number of elements in 'f->locvars' */
  short nactvar;  /* number of active variable declarations */
  lu_byte nups;  /* number of upvalues */
  lu_byte freereg;  /* first free register */
  lu_byte iwthabs;  /* instructions issued since last absolute line info */
  lu_byte needclose;  /* function needs to close upvalues when returning */

public:
  // Inline accessors for reading
  Proto* getProto() const noexcept { return f; }
  FuncState* getPrev() const noexcept { return prev; }
  struct LexState* getLexState() const noexcept { return ls; }
  struct BlockCnt* getBlock() const noexcept { return bl; }
  Table* getKCache() const noexcept { return kcache; }
  int getPC() const noexcept { return pc; }
  int getLastTarget() const noexcept { return lasttarget; }
  int getPreviousLine() const noexcept { return previousline; }
  int getNK() const noexcept { return nk; }
  int getNP() const noexcept { return np; }
  int getNAbsLineInfo() const noexcept { return nabslineinfo; }
  int getFirstLocal() const noexcept { return firstlocal; }
  int getFirstLabel() const noexcept { return firstlabel; }
  short getNumDebugVars() const noexcept { return ndebugvars; }
  short getNumActiveVars() const noexcept { return nactvar; }
  lu_byte getNumUpvalues() const noexcept { return nups; }
  lu_byte getFreeReg() const noexcept { return freereg; }
  lu_byte getInstructionsWithAbs() const noexcept { return iwthabs; }
  lu_byte getNeedClose() const noexcept { return needclose; }

  // Setters for mutable fields
  void setProto(Proto* proto) noexcept { f = proto; }
  void setPrev(FuncState* prev_) noexcept { prev = prev_; }
  void setLexState(struct LexState* ls_) noexcept { ls = ls_; }
  void setBlock(struct BlockCnt* bl_) noexcept { bl = bl_; }
  void setKCache(Table* kcache_) noexcept { kcache = kcache_; }
  void setPC(int pc_) noexcept { pc = pc_; }
  void setLastTarget(int lasttarget_) noexcept { lasttarget = lasttarget_; }
  void setPreviousLine(int previousline_) noexcept { previousline = previousline_; }
  void setNK(int nk_) noexcept { nk = nk_; }
  void setNP(int np_) noexcept { np = np_; }
  void setNAbsLineInfo(int nabslineinfo_) noexcept { nabslineinfo = nabslineinfo_; }
  void setFirstLocal(int firstlocal_) noexcept { firstlocal = firstlocal_; }
  void setFirstLabel(int firstlabel_) noexcept { firstlabel = firstlabel_; }
  void setNumDebugVars(short ndebugvars_) noexcept { ndebugvars = ndebugvars_; }
  void setNumActiveVars(short nactvar_) noexcept { nactvar = nactvar_; }
  void setNumUpvalues(lu_byte nups_) noexcept { nups = nups_; }
  void setFreeReg(lu_byte freereg_) noexcept { freereg = freereg_; }
  void setInstructionsWithAbs(lu_byte iwthabs_) noexcept { iwthabs = iwthabs_; }
  void setNeedClose(lu_byte needclose_) noexcept { needclose = needclose_; }

  // Increment/decrement methods (replacing Ref() usage)
  void incrementPC() noexcept { pc++; }
  void decrementPC() noexcept { pc--; }
  int postIncrementPC() noexcept { return pc++; }
  void incrementNK() noexcept { nk++; }
  void incrementNP() noexcept { np++; }
  void incrementNAbsLineInfo() noexcept { nabslineinfo++; }
  void decrementNAbsLineInfo() noexcept { nabslineinfo--; }
  int postIncrementNAbsLineInfo() noexcept { return nabslineinfo++; }
  short postIncrementNumDebugVars() noexcept { return ndebugvars++; }
  lu_byte postIncrementInstructionsWithAbs() noexcept { return iwthabs++; }
  void decrementInstructionsWithAbs() noexcept { iwthabs--; }
  void decrementFreeReg() noexcept { freereg--; }

  // Reference accessors for compound assignments
  int& getPCRef() noexcept { return pc; }
  int& getLastTargetRef() noexcept { return lasttarget; }
  int& getPreviousLineRef() noexcept { return previousline; }
  int& getNKRef() noexcept { return nk; }
  int& getNPRef() noexcept { return np; }
  int& getNAbsLineInfoRef() noexcept { return nabslineinfo; }
  short& getNumDebugVarsRef() noexcept { return ndebugvars; }
  short& getNumActiveVarsRef() noexcept { return nactvar; }
  lu_byte& getNumUpvaluesRef() noexcept { return nups; }
  lu_byte& getFreeRegRef() noexcept { return freereg; }
  lu_byte& getInstructionsWithAbsRef() noexcept { return iwthabs; }
  lu_byte& getNeedCloseRef() noexcept { return needclose; }

  // Code generation methods (from lcode.h) - Phase 27c
  // Note: OpCode is typedef'd in lopcodes.h, we use int to avoid circular deps
  int code(Instruction i);
  int codeABx(int o, int A, int Bx);
  int codeABCk(int o, int A, int B, int C, int k);
  int codeABC(int o, int A, int B, int C) { return codeABCk(o, A, B, C, 0); }
  int codevABCk(int o, int A, int B, int C, int k);
  int exp2const(const expdesc *e, TValue *v);
  void fixline(int line);
  void nil(int from, int n);
  void reserveregs(int n);
  void checkstack(int n);
  void intCode(int reg, lua_Integer n);
  void dischargevars(expdesc *e);
  int exp2anyreg(expdesc *e);
  void exp2anyregup(expdesc *e);
  void exp2nextreg(expdesc *e);
  void exp2val(expdesc *e);
  void self(expdesc *e, expdesc *key);
  void indexed(expdesc *t, expdesc *k);
  void goiftrue(expdesc *e);
  void storevar(expdesc *var, expdesc *e);
  void setreturns(expdesc *e, int nresults);
  void setoneret(expdesc *e);
  int jump();
  void ret(int first, int nret);
  void patchlist(int list, int target);
  void patchtohere(int list);
  void concat(int *l1, int l2);
  int getlabel();
  // Note: prefix, infix, posfix use UnOpr/BinOpr types from lcode.h
  // We use int here to avoid circular dependency, will cast in implementation
  void prefix(int op, expdesc *v, int line);
  void infix(int op, expdesc *v);
  void posfix(int op, expdesc *v1, expdesc *v2, int line);
  void settablesize(int pcpos, unsigned ra, unsigned asize, unsigned hsize);
  void setlist(int base, int nelems, int tostore);
  void finish();
  // Phase 77: Code generation primitives (moved from private to public as they're used by other methods)
  int codeAsBx(OpCode o, int A, int Bc);
  int codek(int reg, int k);
  int getjump(int position);
  void fixjump(int position, int dest);
  Instruction *getjumpcontrol(int position);
  int patchtestreg(int node, int reg);
  void patchlistaux(int list, int vtarget, int reg, int dtarget);
  // More Phase 77 methods (public for now as used by unconverted functions)
  int condjump(OpCode o, int A, int B, int C, int k);
  int removevalues(int list);
  void savelineinfo(Proto *proto, int line);
  void removelastlineinfo();
  void removelastinstruction();
  Instruction *previousinstruction();
  void freeRegister(int reg);
  void freeRegisters(int r1, int r2);
  void freeExpression(expdesc *e);
  void freeExpressions(expdesc *e1, expdesc *e2);
  TValue *const2val(const expdesc *e);
  int codeextraarg(int A);
  // Phase 78: Constant management (public for now as used by unconverted functions)
  int addk(Proto *proto, TValue *v);
  int k2proto(TValue *key, TValue *v);
  int stringK(TString *s);
  int intK(lua_Integer n);
  int numberK(lua_Number r);
  int boolF();
  int boolT();
  int nilK();
  void floatCode(int reg, lua_Number flt);
  int str2K(expdesc *e);
  int exp2K(expdesc *e);
  // Phase 79: Expression & code generation (public for now as used by unconverted functions)
  void discharge2reg(expdesc *e, int reg);
  void discharge2anyreg(expdesc *e);
  int code_loadbool(int A, OpCode op);
  int need_value(int list);
  void exp2reg(expdesc *e, int reg);
  int exp2RK(expdesc *e);
  void codeABRK(OpCode o, int A, int B, expdesc *ec);
  void negatecondition(expdesc *e);
  int jumponcond(expdesc *e, int cond);
  void codenot(expdesc *e);
  int isKstr(expdesc *e);
  int constfolding(int op, expdesc *e1, const expdesc *e2);
  void codeunexpval(OpCode op, expdesc *e, int line);
  void finishbinexpval(expdesc *e1, expdesc *e2, OpCode op, int v2, int flip, int line, OpCode mmop, TMS event);
  void codebinexpval(BinOpr opr, expdesc *e1, expdesc *e2, int line);
  void codebini(OpCode op, expdesc *e1, expdesc *e2, int flip, int line, TMS event);
  void codebinK(BinOpr opr, expdesc *e1, expdesc *e2, int flip, int line);
  int finishbinexpneg(expdesc *e1, expdesc *e2, OpCode op, int line, TMS event);
  void codebinNoK(BinOpr opr, expdesc *e1, expdesc *e2, int flip, int line);
  void codearith(BinOpr opr, expdesc *e1, expdesc *e2, int flip, int line);
  void codecommutative(BinOpr op, expdesc *e1, expdesc *e2, int line);
  void codebitwise(BinOpr opr, expdesc *e1, expdesc *e2, int line);
  void codeorder(BinOpr opr, expdesc *e1, expdesc *e2);
  void codeeq(BinOpr opr, expdesc *e1, expdesc *e2);
  void codeconcat(expdesc *e1, expdesc *e2, int line);
  // Phase 82: Limit checking
  l_noret errorlimit(int limit, const char *what);
  void checklimit(int v, int l, const char *what);
  // Phase 83: Variable utilities
  Vardesc *getlocalvardesc(int vidx);
  lu_byte reglevel(int nvar);
  lu_byte nvarstack();
  LocVar *localdebuginfo(int vidx);
  void init_var(expdesc *e, int vidx);
  short registerlocalvar(TString *varname);
  // Phase 84: Variable scope
  void removevars(int tolevel);

private:
  // Internal helper methods (only used within lcode.cpp)
  int codesJ(int o, int sj, int k);
  int finaltarget(int i);
  void goiffalse(expdesc *e);
};


LUAI_FUNC lu_byte luaY_nvarstack (FuncState *fs);
LUAI_FUNC void luaY_checklimit (FuncState *fs, int v, int l,
                                const char *what);
LUAI_FUNC LClosure *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff,
                                 Dyndata *dyd, const char *name, int firstchar);


#endif
