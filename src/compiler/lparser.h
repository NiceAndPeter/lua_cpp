/*
** $Id: lparser.h $
** Lua Parser
** See Copyright Notice in lua.h
*/

#ifndef lparser_h
#define lparser_h

#include <span>
#include "llimits.h"
#include "lobject.h"
#include "lopcodes.h"
#include "ltm.h"
#include "lzio.h"
#include "llex.h"
#include "../memory/LuaVector.h"

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
inline constexpr lu_byte VDKREG = 0;   /* regular local */
inline constexpr lu_byte RDKCONST = 1;   /* local constant */
inline constexpr lu_byte RDKTOCLOSE = 2;   /* to-be-closed */
inline constexpr lu_byte RDKCTC = 3;   /* local compile-time constant */
inline constexpr lu_byte GDKREG = 4;   /* regular global */
inline constexpr lu_byte GDKCONST = 5;   /* global constant */

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
class Labellist {
private:
  LuaVector<Labeldesc> vec;

public:
  explicit Labellist(lua_State* L) : vec(L) {
    /* Pre-reserve capacity to avoid early reallocations */
    vec.reserve(16);
  }

  /* Accessor methods matching old interface */
  inline Labeldesc* getArr() noexcept { return vec.data(); }
  inline const Labeldesc* getArr() const noexcept { return vec.data(); }
  inline int getN() const noexcept { return static_cast<int>(vec.size()); }
  inline int getSize() const noexcept { return static_cast<int>(vec.capacity()); }

  /* Modifying size */
  inline void setN(int new_n) { vec.resize(static_cast<size_t>(new_n)); }

  /* Direct vector access for modern operations */
  inline void push_back(const Labeldesc& desc) { vec.push_back(desc); }
  inline void reserve(int capacity) { vec.reserve(static_cast<size_t>(capacity)); }
  inline Labeldesc& operator[](int index) { return vec[static_cast<size_t>(index)]; }
  inline const Labeldesc& operator[](int index) const { return vec[static_cast<size_t>(index)]; }

  /* For luaM_growvector replacement */
  inline void ensureCapacity(int needed) {
    if (needed > getSize()) {
      vec.reserve(static_cast<size_t>(needed));
    }
  }
  inline Labeldesc* allocateNew() {
    vec.resize(vec.size() + 1);
    return &vec.back();
  }
};


/* dynamic structures used by the parser */
class Dyndata {
private:
  LuaVector<Vardesc> actvar_vec;

public:
  Labellist gt;     /* list of pending gotos */
  Labellist label;  /* list of active labels */

  explicit Dyndata(lua_State* L)
    : actvar_vec(L), gt(L), label(L) {
    /* Pre-reserve typical capacity to avoid early reallocations */
    actvar_vec.reserve(32);
  }

  /* Direct actvar accessor methods - avoid temporary object creation */
  inline Vardesc* actvarGetArr() noexcept { return actvar_vec.data(); }
  inline const Vardesc* actvarGetArr() const noexcept { return actvar_vec.data(); }
  inline int actvarGetN() const noexcept { return static_cast<int>(actvar_vec.size()); }
  inline int actvarGetSize() const noexcept { return static_cast<int>(actvar_vec.capacity()); }

  inline void actvarSetN(int new_n) { actvar_vec.resize(static_cast<size_t>(new_n)); }
  inline Vardesc& actvarAt(int index) { return actvar_vec[static_cast<size_t>(index)]; }
  inline const Vardesc& actvarAt(int index) const { return actvar_vec[static_cast<size_t>(index)]; }

  inline Vardesc* actvarAllocateNew() {
    actvar_vec.resize(actvar_vec.size() + 1);
    return &actvar_vec.back();
  }

  /* Phase 116: std::span accessors for actvar array */
  inline std::span<Vardesc> actvarGetSpan() noexcept {
    return std::span(actvar_vec.data(), actvar_vec.size());
  }
  inline std::span<const Vardesc> actvarGetSpan() const noexcept {
    return std::span(actvar_vec.data(), actvar_vec.size());
  }

  /* Legacy accessor interface for backward compatibility */
  class ActvarAccessor {
  private:
    Dyndata* dyn;
  public:
    explicit ActvarAccessor(Dyndata* d) : dyn(d) {}
    inline int getN() const noexcept { return dyn->actvarGetN(); }
    inline void setN(int n) { dyn->actvarSetN(n); }
    inline Vardesc& operator[](int i) { return dyn->actvarAt(i); }
    inline Vardesc* allocateNew() { return dyn->actvarAllocateNew(); }
  };

  inline ActvarAccessor actvar() noexcept { return ActvarAccessor{this}; }
};


/* control of blocks */
struct BlockCnt;  /* defined in lparser.c */
struct ConsControl;  /* defined in lparser.c */
struct LHS_assign;  /* defined in lparser.c */


/*
** FuncState Subsystems - Single Responsibility Principle refactoring
** These classes separate FuncState's responsibilities into focused components
*/

/* 1. Code Buffer - Bytecode generation and line info tracking */
class CodeBuffer {
private:
  int pc;              /* Program counter (next instruction) */
  int lasttarget;      /* Label of last 'jump label' */
  int previousline;    /* Last line saved in lineinfo */
  int nabslineinfo;    /* Number of absolute line info entries */
  lu_byte iwthabs;     /* Instructions since last absolute line info */

public:
  /* Inline accessors for reading */
  inline int getPC() const noexcept { return pc; }
  inline int getLastTarget() const noexcept { return lasttarget; }
  inline int getPreviousLine() const noexcept { return previousline; }
  inline int getNAbsLineInfo() const noexcept { return nabslineinfo; }
  inline lu_byte getInstructionsWithAbs() const noexcept { return iwthabs; }

  /* Setters */
  inline void setPC(int pc_) noexcept { pc = pc_; }
  inline void setLastTarget(int lasttarget_) noexcept { lasttarget = lasttarget_; }
  inline void setPreviousLine(int previousline_) noexcept { previousline = previousline_; }
  inline void setNAbsLineInfo(int nabslineinfo_) noexcept { nabslineinfo = nabslineinfo_; }
  inline void setInstructionsWithAbs(lu_byte iwthabs_) noexcept { iwthabs = iwthabs_; }

  /* Increment/decrement methods */
  inline void incrementPC() noexcept { pc++; }
  inline void decrementPC() noexcept { pc--; }
  inline int postIncrementPC() noexcept { return pc++; }
  inline void incrementNAbsLineInfo() noexcept { nabslineinfo++; }
  inline void decrementNAbsLineInfo() noexcept { nabslineinfo--; }
  inline int postIncrementNAbsLineInfo() noexcept { return nabslineinfo++; }
  inline lu_byte postIncrementInstructionsWithAbs() noexcept { return iwthabs++; }
  inline void decrementInstructionsWithAbs() noexcept { iwthabs--; }

  /* Reference accessors for compound assignments */
  inline int& getPCRef() noexcept { return pc; }
  inline int& getLastTargetRef() noexcept { return lasttarget; }
  inline int& getPreviousLineRef() noexcept { return previousline; }
  inline int& getNAbsLineInfoRef() noexcept { return nabslineinfo; }
  inline lu_byte& getInstructionsWithAbsRef() noexcept { return iwthabs; }
};


/* 2. Constant Pool - Constant value management and deduplication */
class ConstantPool {
private:
  Table *cache;        /* Cache for constant deduplication */
  int count;           /* Number of constants in proto */

public:
  /* Inline accessors */
  inline Table* getCache() const noexcept { return cache; }
  inline int getCount() const noexcept { return count; }

  inline void setCache(Table* cache_) noexcept { cache = cache_; }
  inline void setCount(int count_) noexcept { count = count_; }

  /* Increment */
  inline void incrementCount() noexcept { count++; }

  /* Reference accessor */
  inline int& getCountRef() noexcept { return count; }
};


/* 3. Variable Scope - Local variable and label tracking */
class VariableScope {
private:
  int firstlocal;      /* Index of first local in this function (Dyndata array) */
  int firstlabel;      /* Index of first label in this function */
  short ndebugvars;    /* Number of variables in f->locvars (debug info) */
  short nactvar;       /* Number of active variable declarations */

public:
  /* Inline accessors */
  inline int getFirstLocal() const noexcept { return firstlocal; }
  inline int getFirstLabel() const noexcept { return firstlabel; }
  inline short getNumDebugVars() const noexcept { return ndebugvars; }
  inline short getNumActiveVars() const noexcept { return nactvar; }

  inline void setFirstLocal(int firstlocal_) noexcept { firstlocal = firstlocal_; }
  inline void setFirstLabel(int firstlabel_) noexcept { firstlabel = firstlabel_; }
  inline void setNumDebugVars(short ndebugvars_) noexcept { ndebugvars = ndebugvars_; }
  inline void setNumActiveVars(short nactvar_) noexcept { nactvar = nactvar_; }

  /* Increment */
  inline short postIncrementNumDebugVars() noexcept { return ndebugvars++; }

  /* Reference accessors */
  inline short& getNumDebugVarsRef() noexcept { return ndebugvars; }
  inline short& getNumActiveVarsRef() noexcept { return nactvar; }
};


/* 4. Register Allocator - Register allocation tracking */
class RegisterAllocator {
private:
  lu_byte freereg;     /* First free register */

public:
  /* Inline accessors */
  inline lu_byte getFreeReg() const noexcept { return freereg; }
  inline void setFreeReg(lu_byte freereg_) noexcept { freereg = freereg_; }

  /* Decrement */
  inline void decrementFreeReg() noexcept { freereg--; }

  /* Reference accessor */
  inline lu_byte& getFreeRegRef() noexcept { return freereg; }
};


/* 5. Upvalue Tracker - Upvalue management */
class UpvalueTracker {
private:
  lu_byte nups;        /* Number of upvalues */
  lu_byte needclose;   /* Function needs to close upvalues when returning */

public:
  /* Inline accessors */
  inline lu_byte getNumUpvalues() const noexcept { return nups; }
  inline lu_byte getNeedClose() const noexcept { return needclose; }

  inline void setNumUpvalues(lu_byte nups_) noexcept { nups = nups_; }
  inline void setNeedClose(lu_byte needclose_) noexcept { needclose = needclose_; }

  /* Reference accessors */
  inline lu_byte& getNumUpvaluesRef() noexcept { return nups; }
  inline lu_byte& getNeedCloseRef() noexcept { return needclose; }
};


/* state needed to generate code for a given function */
class FuncState {
private:
  /* Core context (unchanged) */
  Proto *f;  /* current function header */
  class FuncState *prev;  /* enclosing function */
  class LexState *ls;  /* lexical state */
  struct BlockCnt *bl;  /* chain of current blocks */
  int np;  /* number of elements in 'p' (nested functions) */

  /* Subsystems (SRP refactoring) */
  CodeBuffer codeBuffer;           /* Bytecode generation & line info */
  ConstantPool constantPool;       /* Constant management */
  VariableScope variableScope;     /* Local variables & labels */
  RegisterAllocator registerAlloc; /* Register allocation */
  UpvalueTracker upvalueTrack;     /* Upvalue tracking */

public:
  /* Core context accessors (unchanged) */
  inline Proto* getProto() const noexcept { return f; }
  inline FuncState* getPrev() const noexcept { return prev; }
  inline class LexState* getLexState() const noexcept { return ls; }
  inline struct BlockCnt* getBlock() const noexcept { return bl; }
  inline int getNP() const noexcept { return np; }

  inline void setProto(Proto* proto) noexcept { f = proto; }
  inline void setPrev(FuncState* prev_) noexcept { prev = prev_; }
  inline void setLexState(class LexState* ls_) noexcept { ls = ls_; }
  inline void setBlock(struct BlockCnt* bl_) noexcept { bl = bl_; }
  inline void setNP(int np_) noexcept { np = np_; }
  inline void incrementNP() noexcept { np++; }
  inline int& getNPRef() noexcept { return np; }

  /* Subsystem access methods (for direct subsystem manipulation) */
  inline CodeBuffer& getCodeBuffer() noexcept { return codeBuffer; }
  inline const CodeBuffer& getCodeBuffer() const noexcept { return codeBuffer; }
  inline ConstantPool& getConstantPool() noexcept { return constantPool; }
  inline const ConstantPool& getConstantPool() const noexcept { return constantPool; }
  inline VariableScope& getVariableScope() noexcept { return variableScope; }
  inline const VariableScope& getVariableScope() const noexcept { return variableScope; }
  inline RegisterAllocator& getRegisterAllocator() noexcept { return registerAlloc; }
  inline const RegisterAllocator& getRegisterAllocator() const noexcept { return registerAlloc; }
  inline UpvalueTracker& getUpvalueTracker() noexcept { return upvalueTrack; }
  inline const UpvalueTracker& getUpvalueTracker() const noexcept { return upvalueTrack; }

  /* Delegating accessors for CodeBuffer */
  inline int getPC() const noexcept { return codeBuffer.getPC(); }
  inline int getLastTarget() const noexcept { return codeBuffer.getLastTarget(); }
  inline int getPreviousLine() const noexcept { return codeBuffer.getPreviousLine(); }
  inline int getNAbsLineInfo() const noexcept { return codeBuffer.getNAbsLineInfo(); }
  inline lu_byte getInstructionsWithAbs() const noexcept { return codeBuffer.getInstructionsWithAbs(); }

  inline void setPC(int pc_) noexcept { codeBuffer.setPC(pc_); }
  inline void setLastTarget(int lasttarget_) noexcept { codeBuffer.setLastTarget(lasttarget_); }
  inline void setPreviousLine(int previousline_) noexcept { codeBuffer.setPreviousLine(previousline_); }
  inline void setNAbsLineInfo(int nabslineinfo_) noexcept { codeBuffer.setNAbsLineInfo(nabslineinfo_); }
  inline void setInstructionsWithAbs(lu_byte iwthabs_) noexcept { codeBuffer.setInstructionsWithAbs(iwthabs_); }

  inline void incrementPC() noexcept { codeBuffer.incrementPC(); }
  inline void decrementPC() noexcept { codeBuffer.decrementPC(); }
  inline int postIncrementPC() noexcept { return codeBuffer.postIncrementPC(); }
  inline void incrementNAbsLineInfo() noexcept { codeBuffer.incrementNAbsLineInfo(); }
  inline void decrementNAbsLineInfo() noexcept { codeBuffer.decrementNAbsLineInfo(); }
  inline int postIncrementNAbsLineInfo() noexcept { return codeBuffer.postIncrementNAbsLineInfo(); }
  inline lu_byte postIncrementInstructionsWithAbs() noexcept { return codeBuffer.postIncrementInstructionsWithAbs(); }
  inline void decrementInstructionsWithAbs() noexcept { codeBuffer.decrementInstructionsWithAbs(); }

  inline int& getPCRef() noexcept { return codeBuffer.getPCRef(); }
  inline int& getLastTargetRef() noexcept { return codeBuffer.getLastTargetRef(); }
  inline int& getPreviousLineRef() noexcept { return codeBuffer.getPreviousLineRef(); }
  inline int& getNAbsLineInfoRef() noexcept { return codeBuffer.getNAbsLineInfoRef(); }
  inline lu_byte& getInstructionsWithAbsRef() noexcept { return codeBuffer.getInstructionsWithAbsRef(); }

  /* Delegating accessors for ConstantPool */
  inline Table* getKCache() const noexcept { return constantPool.getCache(); }
  inline int getNK() const noexcept { return constantPool.getCount(); }

  inline void setKCache(Table* kcache_) noexcept { constantPool.setCache(kcache_); }
  inline void setNK(int nk_) noexcept { constantPool.setCount(nk_); }
  inline void incrementNK() noexcept { constantPool.incrementCount(); }
  inline int& getNKRef() noexcept { return constantPool.getCountRef(); }

  /* Delegating accessors for VariableScope */
  inline int getFirstLocal() const noexcept { return variableScope.getFirstLocal(); }
  inline int getFirstLabel() const noexcept { return variableScope.getFirstLabel(); }
  inline short getNumDebugVars() const noexcept { return variableScope.getNumDebugVars(); }
  inline short getNumActiveVars() const noexcept { return variableScope.getNumActiveVars(); }

  inline void setFirstLocal(int firstlocal_) noexcept { variableScope.setFirstLocal(firstlocal_); }
  inline void setFirstLabel(int firstlabel_) noexcept { variableScope.setFirstLabel(firstlabel_); }
  inline void setNumDebugVars(short ndebugvars_) noexcept { variableScope.setNumDebugVars(ndebugvars_); }
  inline void setNumActiveVars(short nactvar_) noexcept { variableScope.setNumActiveVars(nactvar_); }

  inline short postIncrementNumDebugVars() noexcept { return variableScope.postIncrementNumDebugVars(); }
  inline short& getNumDebugVarsRef() noexcept { return variableScope.getNumDebugVarsRef(); }
  inline short& getNumActiveVarsRef() noexcept { return variableScope.getNumActiveVarsRef(); }

  /* Delegating accessors for RegisterAllocator */
  inline lu_byte getFreeReg() const noexcept { return registerAlloc.getFreeReg(); }
  inline void setFreeReg(lu_byte freereg_) noexcept { registerAlloc.setFreeReg(freereg_); }
  inline void decrementFreeReg() noexcept { registerAlloc.decrementFreeReg(); }
  inline lu_byte& getFreeRegRef() noexcept { return registerAlloc.getFreeRegRef(); }

  /* Delegating accessors for UpvalueTracker */
  inline lu_byte getNumUpvalues() const noexcept { return upvalueTrack.getNumUpvalues(); }
  inline lu_byte getNeedClose() const noexcept { return upvalueTrack.getNeedClose(); }
  inline void setNumUpvalues(lu_byte nups_) noexcept { upvalueTrack.setNumUpvalues(nups_); }
  inline void setNeedClose(lu_byte needclose_) noexcept { upvalueTrack.setNeedClose(needclose_); }
  inline lu_byte& getNumUpvaluesRef() noexcept { return upvalueTrack.getNumUpvaluesRef(); }
  inline lu_byte& getNeedCloseRef() noexcept { return upvalueTrack.getNeedCloseRef(); }

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
  // Operator functions use strongly-typed enum classes for type safety
  void prefix(UnOpr op, expdesc *v, int line);
  void infix(BinOpr op, expdesc *v);
  void posfix(BinOpr op, expdesc *v1, expdesc *v2, int line);
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
  // Phase 85: Upvalue and variable search
  int searchupvalue(TString *name);
  Upvaldesc *allocupvalue();
  int newupvalue(TString *name, expdesc *v);
  int searchvar(TString *n, expdesc *var);
  void markupval(int level);
  void marktobeclosed();
  // Phase 86: Variable lookup auxiliary
  void singlevaraux(TString *n, expdesc *var, int base);
  // Phase 87: Goto resolution
  void solvegotos(BlockCnt *blockCnt);
  // Phase 88: Block management (used by parser infrastructure)
  void enterblock(BlockCnt *blk, lu_byte isloop);
  void leaveblock();
  // Phase 88: Constructor helpers (used by parser infrastructure)
  void closelistfield(ConsControl *cc);
  void lastlistfield(ConsControl *cc);
  int maxtostore();
  // Phase 88: Variable handling (used by parser infrastructure)
  void setvararg(int nparams);
  void storevartop(expdesc *var);
  void checktoclose(int level);
  void fixforjump(int pcpos, int dest, int back);

private:
  // Internal helper methods (only used within lcode.cpp)
  int codesJ(int o, int sj, int k);
  int finaltarget(int i);
  void goiffalse(expdesc *e);
};


/*
** Phase 95: Parser class - Separates parsing logic from lexical analysis
** Extracted from LexState to achieve proper separation of concerns
*/
class Parser {
private:
  class LexState *ls;  /* lexical state (for tokens and shared data) */
  class FuncState *fs;  /* current function state */

public:
  // Constructor
  explicit Parser(class LexState* lexState, class FuncState* funcState)
    : ls(lexState), fs(funcState) {}

  // Accessors
  inline class LexState* getLexState() const noexcept { return ls; }
  inline class FuncState* getFuncState() const noexcept { return fs; }
  inline class Dyndata* getDyndata() const noexcept { return ls->getDyndata(); }

  inline void setLexState(class LexState* lexState) noexcept { ls = lexState; }
  inline void setFuncState(class FuncState* funcState) noexcept { fs = funcState; }

  // Parser utility methods (extracted from LexState public API)
  l_noret error_expected(int token);
  int testnext(int c);
  void check(int c);
  void checknext(int c);
  void check_match(int what, int who, int where);
  TString *str_checkname();

  // Variable utilities
  void codename(expdesc *e);
  int new_varkind(TString *name, lu_byte kind);
  int new_localvar(TString *name);
  void check_readonly(expdesc *e);
  void adjustlocalvars(int nvars);

  // Variable building and assignment
  void buildglobal(TString *varname, expdesc *var);
  void buildvar(TString *varname, expdesc *var);
  void singlevar(expdesc *var);
  void adjust_assign(int nvars, int nexps, expdesc *e);

  // Label and goto management
  int newgotoentry(TString *name, int line);

  // Parser infrastructure
  Proto *addprototype();
  void mainfunc(FuncState *funcState);

private:
  // Parser implementation methods (extracted from LexState private methods)
  void statement();
  void expr(expdesc *v);
  int block_follow(int withuntil);
  void statlist();
  void fieldsel(expdesc *v);
  void yindex(expdesc *v);
  void recfield(ConsControl *cc);
  void listfield(ConsControl *cc);
  void field(ConsControl *cc);
  void constructor(expdesc *t);
  void parlist();
  void body(expdesc *e, int ismethod, int line);
  int explist(expdesc *v);
  void funcargs(expdesc *f);
  void primaryexp(expdesc *v);
  void suffixedexp(expdesc *v);
  void simpleexp(expdesc *v);
  BinOpr subexpr(expdesc *v, int limit);
  void block();
  void restassign(struct LHS_assign *lh, int nvars);
  int cond();
  void gotostat(int line);
  void breakstat(int line);
  void checkrepeated(TString *name);
  void labelstat(TString *name, int line);
  void whilestat(int line);
  void repeatstat(int line);
  void exp1();
  void forbody(int base, int line, int nvars, int isgen);
  void fornum(TString *varname, int line);
  void forlist(TString *indexname);
  void forstat(int line);
  void test_then_block(int *escapelist);
  void ifstat(int line);
  void localfunc();
  lu_byte getvarattribute(lu_byte df);
  void localstat();
  lu_byte getglobalattribute(lu_byte df);
  void globalnames(lu_byte defkind);
  void globalstat();
  void globalfunc(int line);
  void globalstatfunc(int line);
  int funcname(expdesc *v);
  void funcstat(int line);
  void exprstat();
  void retstat();
  void codeclosure(expdesc *v);
  void open_func(FuncState *funcState, BlockCnt *bl);
  void close_func();
  void check_conflict(struct LHS_assign *lh, expdesc *v);
};


LUAI_FUNC lu_byte luaY_nvarstack (FuncState *fs);
LUAI_FUNC void luaY_checklimit (FuncState *fs, int v, int l,
                                const char *what);
LUAI_FUNC LClosure *luaY_parser (lua_State *L, ZIO *z, Mbuffer *buff,
                                 Dyndata *dyd, const char *name, int firstchar);


/*
** Marks the end of a patch list. It is an invalid value both as an absolute
** address, and as a list link (would link an element to itself).
*/
inline constexpr int NO_JUMP = -1;


/* true if operation is foldable (that is, it is arithmetic or bitwise) */
inline constexpr bool foldbinop(BinOpr op) noexcept {
	return op <= BinOpr::OPR_SHR;
}


/* get (pointer to) instruction of given 'expdesc' */
inline Instruction& getinstruction(FuncState* fs, expdesc* e) noexcept {
	return fs->getProto()->getCode()[e->getInfo()];
}


#endif
