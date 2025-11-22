/*
** $Id: lobject.h $
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/


#ifndef lobject_h
#define lobject_h


#include <cstdarg>
#include <span>


#include "llimits.h"
#include "lua.h"
#include "ltvalue.h"  /* TValue class */

/* Include focused headers */
#include "lobject_core.h"  /* GCObject, GCBase<T>, Udata */
#include "lstring.h"       /* TString */
#include "lproto.h"        /* Proto */
#include "lfunc.h"         /* UpVal, CClosure, LClosure */
#include "ltable.h"        /* Table, Node - after lstring/lproto/lfunc for dependencies */

/* Include ltm.h for tag method support needed by ltable.h inline functions */
#include "../core/ltm.h"   /* TMS enum, checknoTM function */

/* Forward declarations */
enum class GCAge : lu_byte;


/*
** setobj() moved to after type check functions are defined.
** See below after Collectable Types section.
*/


/*
** Entries in a Lua stack. Field 'tbclist' forms a list of all
** to-be-closed variables active in this stack. Dummy entries are
** used when the distance between two tbc variables does not fit
** in an unsigned short. They are represented by delta==0, and
** their real delta is always the maximum value that fits in
** that field.
*/
typedef union StackValue {
  TValue val;
  struct {
    Value value_;
    lu_byte tt_;
    unsigned short delta;
  } tbclist;
} StackValue;


/* index to stack elements */
typedef StackValue *StkId;


/*
** When reallocating the stack, change all pointers to the stack into
** proper offsets.
*/
typedef union {
  StkId p;  /* actual pointer */
  ptrdiff_t offset;  /* used while the stack is being reallocated */
} StkIdRel;


/* convert a 'StackValue' to a 'TValue' */
constexpr TValue* s2v(StackValue* o) noexcept { return &(o)->val; }
constexpr const TValue* s2v(const StackValue* o) noexcept { return &(o)->val; }



/*
** {==================================================================
** Nil
** ===================================================================
*/

/* Standard nil */
inline constexpr int LUA_VNIL = makevariant(LUA_TNIL, 0);

/* Empty slot (which might be different from a slot containing nil) */
inline constexpr int LUA_VEMPTY = makevariant(LUA_TNIL, 1);

/* Value returned for a key not found in a table (absent key) */
inline constexpr int LUA_VABSTKEY = makevariant(LUA_TNIL, 2);

/* Special variant to signal that a fast get is accessing a non-table */
inline constexpr int LUA_VNOTABLE = makevariant(LUA_TNIL, 3);


/* macro to test for (any kind of) nil */
constexpr bool ttisnil(const TValue* v) noexcept { return checktype(v, LUA_TNIL); }

constexpr bool TValue::isNil() const noexcept { return checktype(this, LUA_TNIL); }

/*
** Macro to test the result of a table access. Formally, it should
** distinguish between LUA_VEMPTY/LUA_VABSTKEY/LUA_VNOTABLE and
** other tags. As currently nil is equivalent to LUA_VEMPTY, it is
** simpler to just test whether the value is nil.
*/
constexpr bool tagisempty(int tag) noexcept { return novariant(tag) == LUA_TNIL; }


/* macro to test for a standard nil */
constexpr bool ttisstrictnil(const TValue* o) noexcept { return checktag(o, LUA_VNIL); }

constexpr bool TValue::isStrictNil() const noexcept { return checktag(this, LUA_VNIL); }

inline void setnilvalue(TValue* obj) noexcept { obj->setNil(); }


constexpr bool isabstkey(const TValue* v) noexcept { return checktag(v, LUA_VABSTKEY); }

constexpr bool TValue::isAbstKey() const noexcept { return checktag(this, LUA_VABSTKEY); }


/*
** function to detect non-standard nils (used only in assertions)
*/
constexpr bool isnonstrictnil(const TValue* v) noexcept {
	return ttisnil(v) && !ttisstrictnil(v);
}

constexpr bool TValue::isNonStrictNil() const noexcept {
	return isNil() && !isStrictNil();
}

/*
** By default, entries with any kind of nil are considered empty.
** (In any definition, values associated with absent keys must also
** be accepted as empty.)
*/
constexpr bool isempty(const TValue* v) noexcept { return ttisnil(v); }

constexpr bool TValue::isEmpty() const noexcept { return isNil(); }


/* macro defining a value corresponding to an absent key */
#define ABSTKEYCONSTANT		{nullptr}, LUA_VABSTKEY


/* mark an entry as empty */
inline void setempty(TValue* v) noexcept { settt_(v, LUA_VEMPTY); }



/* }================================================================== */


/*
** {==================================================================
** Booleans
** ===================================================================
*/


inline constexpr int LUA_VFALSE = makevariant(LUA_TBOOLEAN, 0);
inline constexpr int LUA_VTRUE = makevariant(LUA_TBOOLEAN, 1);

constexpr bool ttisboolean(const TValue* o) noexcept { return checktype(o, LUA_TBOOLEAN); }
constexpr bool ttisfalse(const TValue* o) noexcept { return checktag(o, LUA_VFALSE); }
constexpr bool ttistrue(const TValue* o) noexcept { return checktag(o, LUA_VTRUE); }

constexpr bool TValue::isBoolean() const noexcept { return checktype(this, LUA_TBOOLEAN); }
constexpr bool TValue::isFalse() const noexcept { return checktag(this, LUA_VFALSE); }
constexpr bool TValue::isTrue() const noexcept { return checktag(this, LUA_VTRUE); }

constexpr bool l_isfalse(const TValue* o) noexcept { return ttisfalse(o) || ttisnil(o); }
constexpr bool tagisfalse(int t) noexcept { return (t == LUA_VFALSE || novariant(t) == LUA_TNIL); }

constexpr bool TValue::isFalseLike() const noexcept { return isFalse() || isNil(); }



inline void setbfvalue(TValue* obj) noexcept { obj->setFalse(); }
inline void setbtvalue(TValue* obj) noexcept { obj->setTrue(); }

/* }================================================================== */


/*
** {==================================================================
** Threads
** ===================================================================
*/

inline constexpr int LUA_VTHREAD = makevariant(LUA_TTHREAD, 0);

constexpr bool ttisthread(const TValue* o) noexcept { return checktag(o, ctb(LUA_VTHREAD)); }

constexpr bool TValue::isThread() const noexcept { return checktag(this, ctb(LUA_VTHREAD)); }

inline lua_State* thvalue(const TValue* o) noexcept { return o->threadValue(); }

/* setthvalue now defined as inline function below */


/* }================================================================== */




/*
** {==================================================================
** TValue assignment functions
** ===================================================================
*/

/*
** TValue assignment now uses the operator= defined in lgc.h.
** Stack assignments use LuaStack::setSlot() and copySlot().
*/

/* }================================================================== */


/*
** {==================================================================
** Numbers
** ===================================================================
*/

/* Variant tags for numbers */
inline constexpr int LUA_VNUMINT = makevariant(LUA_TNUMBER, 0);  /* integer numbers */
inline constexpr int LUA_VNUMFLT = makevariant(LUA_TNUMBER, 1);  /* float numbers */

constexpr bool ttisnumber(const TValue* o) noexcept { return checktype(o, LUA_TNUMBER); }
constexpr bool ttisfloat(const TValue* o) noexcept { return checktag(o, LUA_VNUMFLT); }
constexpr bool ttisinteger(const TValue* o) noexcept { return checktag(o, LUA_VNUMINT); }

constexpr bool TValue::isNumber() const noexcept { return checktype(this, LUA_TNUMBER); }
constexpr bool TValue::isFloat() const noexcept { return checktag(this, LUA_VNUMFLT); }
constexpr bool TValue::isInteger() const noexcept { return checktag(this, LUA_VNUMINT); }

// TValue::numberValue() implementation (needs LUA_VNUMINT constant)
inline lua_Number TValue::numberValue() const noexcept {
  return (tt_ == LUA_VNUMINT) ? static_cast<lua_Number>(value_.i) : value_.n;
}

inline lua_Number nvalue(const TValue* o) noexcept { return o->numberValue(); }

inline lua_Number fltvalue(const TValue* o) noexcept { return o->floatValue(); }
inline lua_Integer ivalue(const TValue* o) noexcept { return o->intValue(); }

constexpr lua_Number fltvalueraw(const Value& v) noexcept { return v.n; }
constexpr lua_Integer ivalueraw(const Value& v) noexcept { return v.i; }

inline void setfltvalue(TValue* obj, lua_Number x) noexcept { obj->setFloat(x); }
inline void chgfltvalue(TValue* obj, lua_Number x) noexcept { obj->changeFloat(x); }
inline void setivalue(TValue* obj, lua_Integer x) noexcept { obj->setInt(x); }
inline void chgivalue(TValue* obj, lua_Integer x) noexcept { obj->changeInt(x); }

/* }================================================================== */






/*
** {==================================================================
** Prototypes
** ===================================================================
*/

inline constexpr int LUA_VPROTO = makevariant(LUA_TPROTO, 0);


typedef l_uint32 Instruction;


/*
** Description of an upvalue for function prototypes
*/
class Upvaldesc {
private:
  TString *name;  /* upvalue name (for debug information) */
  lu_byte instack;  /* whether it is in stack (register) */
  lu_byte idx;  /* index of upvalue (in stack or in outer function's list) */
  lu_byte kind;  /* kind of corresponding variable */

public:
  // Inline accessors
  TString* getName() const noexcept { return name; }
  TString** getNamePtr() noexcept { return &name; }  // For serialization
  bool isInStack() const noexcept { return instack != 0; }
  lu_byte getInStackRaw() const noexcept { return instack; }
  lu_byte getIndex() const noexcept { return idx; }
  lu_byte getKind() const noexcept { return kind; }

  // Inline setters
  void setName(TString* n) noexcept { name = n; }
  void setInStack(lu_byte val) noexcept { instack = val; }
  void setIndex(lu_byte i) noexcept { idx = i; }
  void setKind(lu_byte k) noexcept { kind = k; }
};


/*
** Description of a local variable for function prototypes
** (used for debug information)
*/
class LocVar {
private:
  TString *varname;
  int startpc;  /* first point where variable is active */
  int endpc;    /* first point where variable is dead */

public:
  // Inline accessors
  TString* getVarName() const noexcept { return varname; }
  TString** getVarNamePtr() noexcept { return &varname; }  // For serialization
  int getStartPC() const noexcept { return startpc; }
  int getEndPC() const noexcept { return endpc; }
  bool isActive(int pc) const noexcept { return startpc <= pc && pc < endpc; }

  // Inline setters
  void setVarName(TString* name) noexcept { varname = name; }
  void setStartPC(int pc) noexcept { startpc = pc; }
  void setEndPC(int pc) noexcept { endpc = pc; }
};


/*
** Associates the absolute line source for a given instruction ('pc').
** The array 'lineinfo' gives, for each instruction, the difference in
** lines from the previous instruction. When that difference does not
** fit into a byte, Lua saves the absolute line for that instruction.
** (Lua also saves the absolute line periodically, to speed up the
** computation of a line number: we can use binary search in the
** absolute-line array, but we must traverse the 'lineinfo' array
** linearly to compute a line.)
*/
class AbsLineInfo {
private:
  int pc;
  int line;

public:
  // Inline accessors
  int getPC() const noexcept { return pc; }
  int getLine() const noexcept { return line; }

  // Inline setters
  void setPC(int p) noexcept { pc = p; }
  void setLine(int l) noexcept { line = l; }
};


/*
** Flags in Prototypes
*/
inline constexpr lu_byte PF_ISVARARG = 1;
inline constexpr lu_byte PF_FIXED = 2;  /* prototype has parts in fixed memory */


/*
** Proto Subsystem - Debug information management
** Separates debug data from runtime execution data for better organization
*/
class ProtoDebugInfo {
private:
  /* Line information */
  ls_byte *lineinfo;            /* Map from opcodes to source lines */
  int sizelineinfo;
  AbsLineInfo *abslineinfo;     /* Absolute line info for faster lookup */
  int sizeabslineinfo;

  /* Local variable information */
  LocVar *locvars;              /* Local variable descriptors */
  int sizelocvars;

  /* Source location */
  int linedefined;              /* First line of function definition */
  int lastlinedefined;          /* Last line of function definition */
  TString *source;              /* Source file name */

public:
  /* Inline accessors */
  inline ls_byte* getLineInfo() const noexcept { return lineinfo; }
  inline int getLineInfoSize() const noexcept { return sizelineinfo; }
  inline AbsLineInfo* getAbsLineInfo() const noexcept { return abslineinfo; }
  inline int getAbsLineInfoSize() const noexcept { return sizeabslineinfo; }
  inline LocVar* getLocVars() const noexcept { return locvars; }
  inline int getLocVarsSize() const noexcept { return sizelocvars; }
  inline int getLineDefined() const noexcept { return linedefined; }
  inline int getLastLineDefined() const noexcept { return lastlinedefined; }
  inline TString* getSource() const noexcept { return source; }

  /* Inline setters */
  inline void setLineInfo(ls_byte* li) noexcept { lineinfo = li; }
  inline void setLineInfoSize(int s) noexcept { sizelineinfo = s; }
  inline void setAbsLineInfo(AbsLineInfo* ali) noexcept { abslineinfo = ali; }
  inline void setAbsLineInfoSize(int s) noexcept { sizeabslineinfo = s; }
  inline void setLocVars(LocVar* lv) noexcept { locvars = lv; }
  inline void setLocVarsSize(int s) noexcept { sizelocvars = s; }
  inline void setLineDefined(int l) noexcept { linedefined = l; }
  inline void setLastLineDefined(int l) noexcept { lastlinedefined = l; }
  inline void setSource(TString* s) noexcept { source = s; }

  /* Reference accessors for luaM_growvector */
  inline int& getLineInfoSizeRef() noexcept { return sizelineinfo; }
  inline int& getAbsLineInfoSizeRef() noexcept { return sizeabslineinfo; }
  inline int& getLocVarsSizeRef() noexcept { return sizelocvars; }
  inline ls_byte*& getLineInfoRef() noexcept { return lineinfo; }
  inline AbsLineInfo*& getAbsLineInfoRef() noexcept { return abslineinfo; }
  inline LocVar*& getLocVarsRef() noexcept { return locvars; }

  /* Pointer accessors */
  inline TString** getSourcePtr() noexcept { return &source; }

  /* Phase 112: std::span accessors for debug info arrays */
  inline std::span<ls_byte> getLineInfoSpan() noexcept {
    return std::span(lineinfo, static_cast<size_t>(sizelineinfo));
  }
  inline std::span<const ls_byte> getLineInfoSpan() const noexcept {
    return std::span(lineinfo, static_cast<size_t>(sizelineinfo));
  }

  inline std::span<AbsLineInfo> getAbsLineInfoSpan() noexcept {
    return std::span(abslineinfo, static_cast<size_t>(sizeabslineinfo));
  }
  inline std::span<const AbsLineInfo> getAbsLineInfoSpan() const noexcept {
    return std::span(abslineinfo, static_cast<size_t>(sizeabslineinfo));
  }

  inline std::span<LocVar> getLocVarsSpan() noexcept {
    return std::span(locvars, static_cast<size_t>(sizelocvars));
  }
  inline std::span<const LocVar> getLocVarsSpan() const noexcept {
    return std::span(locvars, static_cast<size_t>(sizelocvars));
  }
};


/*
** Function Prototypes
*/
// Proto inherits from GCBase (CRTP)
class Proto : public GCBase<Proto> {
private:
  /* Runtime data (always needed for execution) */
  lu_byte numparams;  /* number of fixed (named) parameters */
  lu_byte flag;
  lu_byte maxstacksize;  /* number of registers needed by this function */
  int sizeupvalues;  /* size of 'upvalues' */
  int sizek;  /* size of 'k' */
  int sizecode;
  int sizep;  /* size of 'p' */
  TValue *k;  /* constants used by the function */
  Instruction *code;  /* opcodes */
  Proto **p;  /* functions defined inside the function */
  Upvaldesc *upvalues;  /* upvalue information */
  GCObject *gclist;

  /* Debug subsystem (debug information) */
  ProtoDebugInfo debugInfo;

public:
  // Phase 50: Constructor - initializes all fields to safe defaults
  Proto() noexcept {
    numparams = 0;
    flag = 0;
    maxstacksize = 0;
    sizeupvalues = 0;
    sizek = 0;
    sizecode = 0;
    sizep = 0;
    k = nullptr;
    code = nullptr;
    p = nullptr;
    upvalues = nullptr;
    gclist = nullptr;

    // Initialize debug info subsystem
    debugInfo.setLineInfoSize(0);
    debugInfo.setAbsLineInfoSize(0);
    debugInfo.setLocVarsSize(0);
    debugInfo.setLineDefined(0);
    debugInfo.setLastLineDefined(0);
    debugInfo.setLineInfo(nullptr);
    debugInfo.setAbsLineInfo(nullptr);
    debugInfo.setLocVars(nullptr);
    debugInfo.setSource(nullptr);
  }

  // Phase 50: Destructor - trivial (GC calls free() method explicitly)
  ~Proto() noexcept = default;

  // Phase 50: Placement new operator - integrates with Lua's GC (implemented in lgc.h)
  static void* operator new(size_t size, lua_State* L, lu_byte tt);

  // Disable regular new/delete (must use placement new with GC)
  static void* operator new(size_t) = delete;
  static void operator delete(void*) = delete;

  /* Subsystem access (for direct debug info manipulation) */
  inline ProtoDebugInfo& getDebugInfo() noexcept { return debugInfo; }
  inline const ProtoDebugInfo& getDebugInfo() const noexcept { return debugInfo; }

  /* Runtime data accessors */
  inline lu_byte getNumParams() const noexcept { return numparams; }
  inline lu_byte getFlag() const noexcept { return flag; }
  inline lu_byte getMaxStackSize() const noexcept { return maxstacksize; }
  inline int getCodeSize() const noexcept { return sizecode; }
  inline int getConstantsSize() const noexcept { return sizek; }
  inline int getUpvaluesSize() const noexcept { return sizeupvalues; }
  inline int getProtosSize() const noexcept { return sizep; }
  inline bool isVarArg() const noexcept { return flag != 0; }
  inline Instruction* getCode() const noexcept { return code; }
  inline TValue* getConstants() const noexcept { return k; }

  /* Phase 112: std::span accessors for arrays */
  inline std::span<Instruction> getCodeSpan() noexcept {
    return std::span(code, static_cast<size_t>(sizecode));
  }
  inline std::span<const Instruction> getCodeSpan() const noexcept {
    return std::span(code, static_cast<size_t>(sizecode));
  }

  inline std::span<TValue> getConstantsSpan() noexcept {
    return std::span(k, static_cast<size_t>(sizek));
  }
  inline std::span<const TValue> getConstantsSpan() const noexcept {
    return std::span(k, static_cast<size_t>(sizek));
  }

  inline std::span<Proto*> getProtosSpan() noexcept {
    return std::span(p, static_cast<size_t>(sizep));
  }
  inline std::span<Proto* const> getProtosSpan() const noexcept {
    return std::span(p, static_cast<size_t>(sizep));
  }

  inline std::span<Upvaldesc> getUpvaluesSpan() noexcept {
    return std::span(upvalues, static_cast<size_t>(sizeupvalues));
  }
  inline std::span<const Upvaldesc> getUpvaluesSpan() const noexcept {
    return std::span(upvalues, static_cast<size_t>(sizeupvalues));
  }

  inline Proto** getProtos() const noexcept { return p; }
  inline Upvaldesc* getUpvalues() const noexcept { return upvalues; }
  inline GCObject* getGclist() const noexcept { return gclist; }

  /* Delegating accessors for ProtoDebugInfo */
  inline int getLineInfoSize() const noexcept { return debugInfo.getLineInfoSize(); }
  inline int getLocVarsSize() const noexcept { return debugInfo.getLocVarsSize(); }
  inline int getAbsLineInfoSize() const noexcept { return debugInfo.getAbsLineInfoSize(); }
  inline int getLineDefined() const noexcept { return debugInfo.getLineDefined(); }
  inline int getLastLineDefined() const noexcept { return debugInfo.getLastLineDefined(); }
  inline TString* getSource() const noexcept { return debugInfo.getSource(); }
  inline ls_byte* getLineInfo() const noexcept { return debugInfo.getLineInfo(); }
  inline AbsLineInfo* getAbsLineInfo() const noexcept { return debugInfo.getAbsLineInfo(); }
  inline LocVar* getLocVars() const noexcept { return debugInfo.getLocVars(); }

  /* Runtime data setters */
  inline void setNumParams(lu_byte n) noexcept { numparams = n; }
  inline void setFlag(lu_byte f) noexcept { flag = f; }
  inline void setMaxStackSize(lu_byte s) noexcept { maxstacksize = s; }
  inline void setCodeSize(int s) noexcept { sizecode = s; }
  inline void setConstantsSize(int s) noexcept { sizek = s; }
  inline void setUpvaluesSize(int s) noexcept { sizeupvalues = s; }
  inline void setProtosSize(int s) noexcept { sizep = s; }
  inline void setCode(Instruction* c) noexcept { code = c; }
  inline void setConstants(TValue* constants) noexcept { k = constants; }
  inline void setProtos(Proto** protos) noexcept { p = protos; }
  inline void setUpvalues(Upvaldesc* uv) noexcept { upvalues = uv; }
  inline void setGclist(GCObject* gc) noexcept { gclist = gc; }

  /* Delegating setters for ProtoDebugInfo */
  inline void setLineInfoSize(int s) noexcept { debugInfo.setLineInfoSize(s); }
  inline void setLocVarsSize(int s) noexcept { debugInfo.setLocVarsSize(s); }
  inline void setAbsLineInfoSize(int s) noexcept { debugInfo.setAbsLineInfoSize(s); }
  inline void setLineDefined(int l) noexcept { debugInfo.setLineDefined(l); }
  inline void setLastLineDefined(int l) noexcept { debugInfo.setLastLineDefined(l); }
  inline void setSource(TString* s) noexcept { debugInfo.setSource(s); }
  inline void setLineInfo(ls_byte* li) noexcept { debugInfo.setLineInfo(li); }
  inline void setAbsLineInfo(AbsLineInfo* ali) noexcept { debugInfo.setAbsLineInfo(ali); }
  inline void setLocVars(LocVar* lv) noexcept { debugInfo.setLocVars(lv); }

  /* Pointer accessors for serialization and GC */
  inline TString** getSourcePtr() noexcept { return debugInfo.getSourcePtr(); }
  inline GCObject** getGclistPtr() noexcept { return &gclist; }

  /* Runtime data reference accessors for luaM_growvector */
  inline int& getCodeSizeRef() noexcept { return sizecode; }
  inline int& getConstantsSizeRef() noexcept { return sizek; }
  inline int& getUpvaluesSizeRef() noexcept { return sizeupvalues; }
  inline int& getProtosSizeRef() noexcept { return sizep; }

  inline Instruction*& getCodeRef() noexcept { return code; }
  inline TValue*& getConstantsRef() noexcept { return k; }
  inline Proto**& getProtosRef() noexcept { return p; }
  inline Upvaldesc*& getUpvaluesRef() noexcept { return upvalues; }

  /* Delegating reference accessors for ProtoDebugInfo */
  inline int& getLineInfoSizeRef() noexcept { return debugInfo.getLineInfoSizeRef(); }
  inline int& getLocVarsSizeRef() noexcept { return debugInfo.getLocVarsSizeRef(); }
  inline int& getAbsLineInfoSizeRef() noexcept { return debugInfo.getAbsLineInfoSizeRef(); }
  inline ls_byte*& getLineInfoRef() noexcept { return debugInfo.getLineInfoRef(); }
  inline AbsLineInfo*& getAbsLineInfoRef() noexcept { return debugInfo.getAbsLineInfoRef(); }
  inline LocVar*& getLocVarsRef() noexcept { return debugInfo.getLocVarsRef(); }

  // Phase 44.5: Additional Proto helper methods

  // Get relative PC for debug info
  int getPCRelative(const Instruction* pc) const noexcept {
    return cast_int(pc - code) - 1;
  }

  // Methods (implemented in lfunc.cpp)
  lu_mem memorySize() const;
  void free(lua_State* L);
  const char* getLocalName(int local_number, int pc) const;
};

/* }================================================================== */


/*
** {==================================================================
** Functions
** ===================================================================
*/

inline constexpr int LUA_VUPVAL = makevariant(LUA_TUPVAL, 0);


/* Variant tags for functions */
inline constexpr int LUA_VLCL = makevariant(LUA_TFUNCTION, 0);  /* Lua closure */
inline constexpr int LUA_VLCF = makevariant(LUA_TFUNCTION, 1);  /* light C function */
inline constexpr int LUA_VCCL = makevariant(LUA_TFUNCTION, 2);  /* C closure */

constexpr bool ttisfunction(const TValue* o) noexcept { return checktype(o, LUA_TFUNCTION); }
constexpr bool ttisLclosure(const TValue* o) noexcept { return checktag(o, ctb(LUA_VLCL)); }
constexpr bool ttislcf(const TValue* o) noexcept { return checktag(o, LUA_VLCF); }
constexpr bool ttisCclosure(const TValue* o) noexcept { return checktag(o, ctb(LUA_VCCL)); }
constexpr bool ttisclosure(const TValue* o) noexcept { return ttisLclosure(o) || ttisCclosure(o); }

constexpr bool TValue::isFunction() const noexcept { return checktype(this, LUA_TFUNCTION); }
constexpr bool TValue::isLClosure() const noexcept { return checktag(this, ctb(LUA_VLCL)); }
constexpr bool TValue::isLightCFunction() const noexcept { return checktag(this, LUA_VLCF); }
constexpr bool TValue::isCClosure() const noexcept { return checktag(this, ctb(LUA_VCCL)); }
constexpr bool TValue::isClosure() const noexcept { return isLClosure() || isCClosure(); }

inline constexpr bool isLfunction(const TValue* o) noexcept {
	return ttisLclosure(o);
}

constexpr bool TValue::isLuaFunction() const noexcept { return isLClosure(); }

inline Closure* clvalue(const TValue* o) noexcept { return o->closureValue(); }
inline LClosure* clLvalue(const TValue* o) noexcept { return o->lClosureValue(); }
inline CClosure* clCvalue(const TValue* o) noexcept { return o->cClosureValue(); }

inline lua_CFunction fvalue(const TValue* o) noexcept { return o->functionValue(); }

constexpr lua_CFunction fvalueraw(const Value& v) noexcept { return v.f; }


/* setfvalue now defined as inline function below */

/* setclCvalue now defined as inline function below */


/*
** Upvalues for Lua closures
*/
// UpVal inherits from GCBase (CRTP)
class UpVal : public GCBase<UpVal> {
private:
  union {
    TValue *p;  /* points to stack or to its own value */
    ptrdiff_t offset;  /* used while the stack is being reallocated */
  } v;
  union {
    struct {  /* (when open) */
      UpVal *next;  /* linked list */
      UpVal **previous;
    } open;
    TValue value;  /* the value (when closed) */
  } u;

public:
  // Phase 50: Constructor - initializes all fields to safe defaults
  UpVal() noexcept {
    v.p = nullptr;  // Initialize v union (pointer variant)
    // Initialize u union as closed upvalue with nil
    u.value.valueField().n = 0;  // Zero-initialize Value union
    u.value.setType(LUA_TNIL);
  }

  // Phase 50: Destructor - trivial (GC handles deallocation)
  ~UpVal() noexcept = default;

  // Phase 50: Placement new operator - integrates with Lua's GC (implemented in lgc.h)
  static void* operator new(size_t size, lua_State* L, lu_byte tt);

  // Disable regular new/delete (must use placement new with GC)
  static void* operator new(size_t) = delete;
  static void operator delete(void*) = delete;

  // Inline accessors for v union
  TValue* getVP() noexcept { return v.p; }
  const TValue* getVP() const noexcept { return v.p; }
  void setVP(TValue* ptr) noexcept { v.p = ptr; }

  ptrdiff_t getOffset() const noexcept { return v.offset; }
  void setOffset(ptrdiff_t off) noexcept { v.offset = off; }

  // Inline accessors for u union (open upvalues)
  UpVal* getOpenNext() const noexcept { return u.open.next; }
  void setOpenNext(UpVal* next_uv) noexcept { u.open.next = next_uv; }
  UpVal** getOpenNextPtr() noexcept { return &u.open.next; }

  UpVal** getOpenPrevious() const noexcept { return u.open.previous; }
  void setOpenPrevious(UpVal** prev) noexcept { u.open.previous = prev; }

  // Accessor for u.value (closed upvalues)
  TValue* getValueSlot() noexcept { return &u.value; }
  const TValue* getValueSlot() const noexcept { return &u.value; }

  // Status check
  bool isOpen() const noexcept { return v.p != &u.value; }

  // Level accessor for open upvalues (Phase 44.3)
  StkId getLevel() const noexcept {
    lua_assert(isOpen());
    return reinterpret_cast<StkId>(v.p);
  }

  // Backward compatibility (getValue returns current value pointer)
  TValue* getValue() noexcept { return v.p; }
  const TValue* getValue() const noexcept { return v.p; }

  // Methods (implemented in lfunc.cpp)
  void unlink();
};



// Closures inherit from GCBase (CRTP)
// ClosureHeader fields: nupvalues, gclist (GC fields inherited from GCBase)

class CClosure : public GCBase<CClosure> {
private:
  lu_byte nupvalues;
  GCObject *gclist;
  lua_CFunction f;
  TValue upvalue[1];  /* list of upvalues */

public:
  // Member placement new operator for GC allocation (defined in lgc.h)
  static void* operator new(size_t size, lua_State* L, lu_byte tt, size_t extra = 0);

  // Constructor
  CClosure(int nupvals);

  // Factory method
  static CClosure* create(lua_State* L, int nupvals);

  // Inline accessors
  lua_CFunction getFunction() const noexcept { return f; }
  void setFunction(lua_CFunction func) noexcept { f = func; }

  lu_byte getNumUpvalues() const noexcept { return nupvalues; }
  void setNumUpvalues(lu_byte n) noexcept { nupvalues = n; }

  TValue* getUpvalue(int idx) noexcept { return &upvalue[idx]; }
  const TValue* getUpvalue(int idx) const noexcept { return &upvalue[idx]; }

  GCObject* getGclist() noexcept { return gclist; }
  void setGclist(GCObject* gc) noexcept { gclist = gc; }
  // For GC gray list traversal - allows efficient list manipulation
  GCObject** getGclistPtr() noexcept { return &gclist; }

  // Static helper for size calculation (can access private upvalue field)
  static constexpr size_t sizeForUpvalues(int n) noexcept {
    return offsetof(CClosure, upvalue) + sizeof(TValue) * cast_uint(n);
  }
};

class LClosure : public GCBase<LClosure> {
private:
  lu_byte nupvalues;
  GCObject *gclist;
  Proto *p;
  UpVal *upvals[1];  /* list of upvalues */

public:
  // Member placement new operator for GC allocation (defined in lgc.h)
  static void* operator new(size_t size, lua_State* L, lu_byte tt, size_t extra = 0);

  // Constructor
  LClosure(int nupvals);

  // Factory method
  static LClosure* create(lua_State* L, int nupvals);

  // Inline accessors
  Proto* getProto() const noexcept { return p; }
  void setProto(Proto* proto) noexcept { p = proto; }

  lu_byte getNumUpvalues() const noexcept { return nupvalues; }
  void setNumUpvalues(lu_byte n) noexcept { nupvalues = n; }

  UpVal* getUpval(int idx) const noexcept { return upvals[idx]; }
  void setUpval(int idx, UpVal* uv) noexcept { upvals[idx] = uv; }
  UpVal** getUpvalPtr(int idx) noexcept { return &upvals[idx]; }

  GCObject* getGclist() noexcept { return gclist; }
  void setGclist(GCObject* gc) noexcept { gclist = gc; }
  // For GC gray list traversal - allows efficient list manipulation
  GCObject** getGclistPtr() noexcept { return &gclist; }

  // Static helper for size calculation (can access private upvals field)
  static constexpr size_t sizeForUpvalues(int n) noexcept {
    return offsetof(LClosure, upvals) + sizeof(UpVal *) * cast_uint(n);
  }

  // Methods (implemented in lfunc.cpp)
  void initUpvals(lua_State* L);
};


typedef union Closure {
  CClosure c;
  LClosure l;
} Closure;

inline Proto* getproto(const TValue* o) noexcept {
	return clLvalue(o)->getProto();
}

/* }================================================================== */


/*
** {==================================================================
** Tables
** ===================================================================
*/

inline constexpr int LUA_VTABLE = makevariant(LUA_TTABLE, 0);

constexpr bool ttistable(const TValue* o) noexcept { return checktag(o, ctb(LUA_VTABLE)); }

constexpr bool TValue::isTable() const noexcept { return checktag(this, ctb(LUA_VTABLE)); }

inline Table* hvalue(const TValue* o) noexcept { return o->tableValue(); }

/*
** Phase 17: TValue setter method implementations
** These need all type constants, so they're defined here at the end
*/
inline void TValue::setNil() noexcept { tt_ = LUA_VNIL; }
inline void TValue::setFalse() noexcept { tt_ = LUA_VFALSE; }
inline void TValue::setTrue() noexcept { tt_ = LUA_VTRUE; }

inline void TValue::setInt(lua_Integer i) noexcept {
  value_.i = i;
  tt_ = LUA_VNUMINT;
}

inline void TValue::setFloat(lua_Number n) noexcept {
  value_.n = n;
  tt_ = LUA_VNUMFLT;
}

inline void TValue::setPointer(void* p) noexcept {
  value_.p = p;
  tt_ = LUA_VLIGHTUSERDATA;
}

inline void TValue::setFunction(lua_CFunction f) noexcept {
  value_.f = f;
  tt_ = LUA_VLCF;
}

inline void TValue::setString(lua_State* L, TString* s) noexcept {
  value_.gc = reinterpret_cast<GCObject*>(s);
  tt_ = ctb(s->getType());
  (void)L; // checkliveness removed - needs lstate.h
}

inline void TValue::setUserdata(lua_State* L, Udata* u) noexcept {
  value_.gc = reinterpret_cast<GCObject*>(u);
  tt_ = ctb(LUA_VUSERDATA);
  (void)L;
}

inline void TValue::setTable(lua_State* L, Table* t) noexcept {
  value_.gc = reinterpret_cast<GCObject*>(t);
  tt_ = ctb(LUA_VTABLE);
  (void)L;
}

inline void TValue::setLClosure(lua_State* L, LClosure* cl) noexcept {
  value_.gc = reinterpret_cast<GCObject*>(cl);
  tt_ = ctb(LUA_VLCL);
  (void)L;
}

inline void TValue::setCClosure(lua_State* L, CClosure* cl) noexcept {
  value_.gc = reinterpret_cast<GCObject*>(cl);
  tt_ = ctb(LUA_VCCL);
  (void)L;
}

inline void TValue::setThread(lua_State* L, lua_State* th) noexcept {
  value_.gc = reinterpret_cast<GCObject*>(th);
  tt_ = ctb(LUA_VTHREAD);
  (void)L;
}

inline void TValue::setGCObject(lua_State* L, GCObject* gc) noexcept {
  value_.gc = gc;
  tt_ = ctb(gc->getType());
  (void)L;
}

// Wrapper functions to replace setter macros
inline void setpvalue(TValue* obj, void* p) noexcept { obj->setPointer(p); }
inline void setfvalue(TValue* obj, lua_CFunction f) noexcept { obj->setFunction(f); }
inline void setsvalue(lua_State* L, TValue* obj, TString* s) noexcept { obj->setString(L, s); }
inline void setuvalue(lua_State* L, TValue* obj, Udata* u) noexcept { obj->setUserdata(L, u); }
inline void sethvalue(lua_State* L, TValue* obj, Table* t) noexcept { obj->setTable(L, t); }
inline void setthvalue(lua_State* L, TValue* obj, lua_State* th) noexcept { obj->setThread(L, th); }
inline void setclLvalue(lua_State* L, TValue* obj, LClosure* cl) noexcept { obj->setLClosure(L, cl); }
inline void setclCvalue(lua_State* L, TValue* obj, CClosure* cl) noexcept { obj->setCClosure(L, cl); }
inline void setgcovalue(lua_State* L, TValue* obj, GCObject* gc) noexcept { obj->setGCObject(L, gc); }

/* Note: setter macros are now defined as inline functions above */

inline void sethvalue2s(lua_State* L, StackValue* o, Table* h) noexcept {
	sethvalue(L, s2v(o), h);
}

// Setter wrapper functions
inline void setthvalue2s(lua_State* L, StackValue* o, lua_State* t) noexcept {
	setthvalue(L, s2v(o), t);
}

inline void setsvalue2s(lua_State* L, StackValue* o, TString* s) noexcept {
	setsvalue(L, s2v(o), s);
}

inline void setsvalue2n(lua_State* L, TValue* obj, TString* s) noexcept {
	setsvalue(L, obj, s);
}

inline void setclLvalue2s(lua_State* L, StackValue* o, LClosure* cl) noexcept {
	setclLvalue(L, s2v(o), cl);
}


/*
** Nodes for Hash tables: A pack of two TValue's (key-value pairs)
** plus a 'next' field to link colliding entries. The distribution
** of the key's fields ('key_tt' and 'key_val') not forming a proper
** 'TValue' allows for a smaller size for 'Node' both in 4-byte
** and 8-byte alignments.
** Phase 44.2: Converted from union to class with proper encapsulation
*/
class Node {
private:
  union {
    struct {
      Value value_;  /* value */
      lu_byte tt_;   /* value type tag */
      lu_byte key_tt;  /* key type */
      int next;  /* for chaining */
      Value key_val;  /* key value */
    } u;
    TValue i_val;  /* direct access to node's value as a proper 'TValue' */
  };

public:
  // Default constructor
  constexpr Node() noexcept : u{{0}, LUA_VNIL, LUA_TNIL, 0, {0}} {}

  // Constructor for initializing with explicit values
  constexpr Node(Value val, lu_byte val_tt, lu_byte key_tt, int next_val, Value key_val) noexcept
    : u{val, val_tt, key_tt, next_val, key_val} {}

  // Copy assignment operator (needed because union contains TValue with user-declared operator=)
  inline Node& operator=(const Node& other) noexcept {
    u = other.u;  // Copy the union
    return *this;
  }

  // Value access
  inline TValue* getValue() noexcept { return &i_val; }
  inline const TValue* getValue() const noexcept { return &i_val; }

  // Next chain access
  inline int& getNext() noexcept { return u.next; }
  inline int getNext() const noexcept { return u.next; }
  inline void setNext(int n) noexcept { u.next = n; }

  // Key type access
  inline lu_byte getKeyType() const noexcept { return u.key_tt; }
  inline void setKeyType(lu_byte tt) noexcept { u.key_tt = tt; }

  // Key value access
  inline const Value& getKeyValue() const noexcept { return u.key_val; }
  inline Value& getKeyValue() noexcept { return u.key_val; }
  inline void setKeyValue(const Value& v) noexcept { u.key_val = v; }

  // Key type checks
  inline bool isKeyNil() const noexcept {
    return u.key_tt == LUA_TNIL;
  }

  inline bool isKeyInteger() const noexcept {
    return u.key_tt == LUA_VNUMINT;
  }

  inline bool isKeyShrStr() const noexcept {
    return u.key_tt == ctb(LUA_VSHRSTR);
  }

  inline bool isKeyDead() const noexcept {
    return u.key_tt == LUA_TDEADKEY;
  }

  inline bool isKeyCollectable() const noexcept {
    return (u.key_tt & BIT_ISCOLLECTABLE) != 0;
  }

  // Key value getters (typed)
  inline lua_Integer getKeyIntValue() const noexcept {
    return u.key_val.i;
  }

  inline TString* getKeyStrValue() const noexcept {
    return reinterpret_cast<TString*>(u.key_val.gc);
  }

  inline GCObject* getKeyGC() const noexcept {
    return u.key_val.gc;
  }

  inline GCObject* getKeyGCOrNull() const noexcept {
    return isKeyCollectable() ? u.key_val.gc : nullptr;
  }

  // Key setters
  inline void setKeyNil() noexcept {
    u.key_tt = LUA_TNIL;
  }

  inline void setKeyDead() noexcept {
    u.key_tt = LUA_TDEADKEY;
  }

  // Copy TValue to key
  inline void setKey(const TValue* obj) noexcept {
    u.key_val = obj->getValue();
    u.key_tt = obj->getType();
  }

  // Copy key to TValue
  inline void getKey(lua_State* L, TValue* obj) const noexcept {
    obj->valueField() = u.key_val;
    obj->setType(u.key_tt);
    (void)L; // checkliveness removed to avoid forward declaration issues
  }
};


/* Phase 44.2: setnodekey and getnodekey macros replaced with Node::setKey() and Node::getKey() methods */


// Table inherits from GCBase (CRTP)
class Table : public GCBase<Table> {
private:
  mutable lu_byte flags;  /* 1<<p means tagmethod(p) is not present (mutable for metamethod caching) */
  lu_byte lsizenode;  /* log2 of number of slots of 'node' array */
  unsigned int asize;  /* number of slots in 'array' array */
  Value *array;  /* array part */
  Node *node;
  Table *metatable;
  GCObject *gclist;

public:
  // Phase 50: Constructor - initializes all fields to safe defaults
  Table() noexcept {
    flags = 0;
    lsizenode = 0;
    asize = 0;
    array = nullptr;
    node = nullptr;
    metatable = nullptr;
    gclist = nullptr;
  }

  // Phase 50: Destructor - trivial (GC handles deallocation)
  ~Table() noexcept = default;

  // Phase 50: Placement new operator - integrates with Lua's GC (implemented in lgc.h)
  static void* operator new(size_t size, lua_State* L, lu_byte tt);

  // Disable regular new/delete (must use placement new with GC)
  static void* operator new(size_t) = delete;
  static void operator delete(void*) = delete;

  // Inline accessors
  lu_byte getFlags() const noexcept { return flags; }
  void setFlags(lu_byte f) noexcept { flags = f; }

  // Flags field bit manipulation methods (const - flags is mutable)
  void setFlagBits(int mask) const noexcept { flags |= cast_byte(mask); }
  void clearFlagBits(int mask) const noexcept { flags &= cast_byte(~mask); }

  // Flags field reference accessor (for backward compatibility)
  lu_byte& getFlagsRef() noexcept { return flags; }

  lu_byte getLsizenode() const noexcept { return lsizenode; }
  void setLsizenode(lu_byte ls) noexcept { lsizenode = ls; }

  unsigned int arraySize() const noexcept { return asize; }
  void setArraySize(unsigned int sz) noexcept { asize = sz; }

  Value* getArray() noexcept { return array; }
  const Value* getArray() const noexcept { return array; }
  void setArray(Value* arr) noexcept { array = arr; }

  // Phase 115.3: std::span accessors for array part
  std::span<Value> getArraySpan() noexcept {
    return std::span(array, asize);
  }
  std::span<const Value> getArraySpan() const noexcept {
    return std::span(array, asize);
  }

  Node* getNodeArray() noexcept { return node; }
  const Node* getNodeArray() const noexcept { return node; }
  void setNodeArray(Node* n) noexcept { node = n; }

  unsigned int nodeSize() const noexcept { return (1u << lsizenode); }
  Table* getMetatable() const noexcept { return metatable; }
  void setMetatable(Table* mt) noexcept { metatable = mt; }

  GCObject* getGclist() noexcept { return gclist; }
  void setGclist(GCObject* gc) noexcept { gclist = gc; }
  // For GC gray list traversal - allows efficient list manipulation
  GCObject** getGclistPtr() noexcept { return &gclist; }

  // Flag operations (inline for performance)
  // Note: BITDUMMY = (1 << 6), defined in ltable.h
  bool isDummy() const noexcept { return (flags & (1 << 6)) != 0; }
  void setDummy() noexcept { flags |= (1 << 6); }
  void setNoDummy() noexcept { flags &= cast_byte(~(1 << 6)); }
  // invalidateTMCache uses maskflags from ltm.h, so can't inline here - use macro instead

  // Phase 44.1: Additional table helper methods
  inline unsigned int allocatedNodeSize() const noexcept {
    return isDummy() ? 0 : nodeSize();
  }

  inline unsigned int* getLenHint() noexcept {
    return static_cast<unsigned int*>(static_cast<void*>(array));
  }

  inline const unsigned int* getLenHint() const noexcept {
    return static_cast<const unsigned int*>(static_cast<const void*>(array));
  }

  inline lu_byte* getArrayTag(lua_Unsigned k) noexcept {
    return static_cast<lu_byte*>(static_cast<void*>(array)) + sizeof(unsigned) + k;
  }

  inline const lu_byte* getArrayTag(lua_Unsigned k) const noexcept {
    return static_cast<const lu_byte*>(static_cast<const void*>(array)) + sizeof(unsigned) + k;
  }

  inline Value* getArrayVal(lua_Unsigned k) noexcept {
    return array - 1 - k;
  }

  inline const Value* getArrayVal(lua_Unsigned k) const noexcept {
    return array - 1 - k;
  }

  static inline unsigned int powerOfTwo(unsigned int x) noexcept {
    return (1u << x);
  }

  // Node accessors (Phase 19: Table macro reduction)
  Node* getNode(unsigned int i) noexcept { return &node[i]; }
  const Node* getNode(unsigned int i) const noexcept { return &node[i]; }

  // Method declarations (implemented in ltable.cpp)
  lu_byte get(const TValue* key, TValue* res);
  lu_byte getInt(lua_Integer key, TValue* res);
  lu_byte getShortStr(TString* key, TValue* res);
  lu_byte getStr(TString* key, TValue* res);
  TValue* HgetShortStr(TString* key);

  int pset(const TValue* key, TValue* val);
  int psetInt(lua_Integer key, TValue* val);
  int psetShortStr(TString* key, TValue* val);
  int psetStr(TString* key, TValue* val);

  void set(lua_State* L, const TValue* key, TValue* value);
  void setInt(lua_State* L, lua_Integer key, TValue* value);
  void finishSet(lua_State* L, const TValue* key, TValue* value, int hres);

  void resize(lua_State* L, unsigned nasize, unsigned nhsize);
  void resizeArray(lua_State* L, unsigned nasize);
  lu_mem size() const;
  int tableNext(lua_State* L, StkId key);  // renamed from next() to avoid conflict with GC field
  lua_Unsigned getn(lua_State* L);

  // Phase 33: Factory and helper methods
  static Table* create(lua_State* L);  // Factory method (replaces luaH_new)
  void destroy(lua_State* L);  // Explicit destructor (replaces luaH_free)
  Node* mainPosition(const TValue* key) const;  // replaces luaH_mainposition
};


/*
** Phase 44.2: Node key macros replaced with Node class methods:
** - keytt(node) → node->getKeyType()
** - keyval(node) → node->getKeyValue()
** - keyisnil(node) → node->isKeyNil()
** - keyisinteger(node) → node->isKeyInteger()
** - keyival(node) → node->getKeyIntValue()
** - keyisshrstr(node) → node->isKeyShrStr()
** - keystrval(node) → node->getKeyStrValue()
** - setnilkey(node) → node->setKeyNil()
** - keyiscollectable(n) → n->isKeyCollectable()
** - gckey(n) → n->getKeyGC()
** - gckeyN(n) → n->getKeyGCOrNull()
** - setdeadkey(node) → node->setKeyDead()
** - keyisdead(node) → node->isKeyDead()
*/

/* }================================================================== */



/*
** 'module' operation for hashing (size is always a power of 2)
*/
inline unsigned int lmod(unsigned int s, unsigned int size) noexcept {
	lua_assert((size & (size - 1)) == 0);  /* size must be power of 2 */
	return s & (size - 1);
}


/* Phase 44.1: twoto now Table::powerOfTwo(x) static method */
/* Phase 44.1: sizenode now Table::nodeSize() method */


/* size of buffer for 'luaO_utf8esc' function */
inline constexpr int UTF8BUFFSZ = 8;


/* macro to call 'luaO_pushvfstring' correctly */
#define pushvfstring(L, argp, fmt, msg)	\
  { va_start(argp, fmt); \
  msg = luaO_pushvfstring(L, fmt, argp); \
  va_end(argp); \
  if (msg == nullptr) (L)->doThrow(LUA_ERRMEM);  /* only after 'va_end' */ }


[[nodiscard]] LUAI_FUNC int luaO_utf8esc (char *buff, l_uint32 x);
[[nodiscard]] LUAI_FUNC lu_byte luaO_ceillog2 (unsigned int x);
[[nodiscard]] LUAI_FUNC lu_byte luaO_codeparam (unsigned int p);
[[nodiscard]] LUAI_FUNC l_mem luaO_applyparam (lu_byte p, l_mem x);

[[nodiscard]] LUAI_FUNC int luaO_rawarith (lua_State *L, int op, const TValue *p1,
                             const TValue *p2, TValue *res);
LUAI_FUNC void luaO_arith (lua_State *L, int op, const TValue *p1,
                           const TValue *p2, StkId res);
[[nodiscard]] LUAI_FUNC size_t luaO_str2num (const char *s, TValue *o);
[[nodiscard]] LUAI_FUNC unsigned luaO_tostringbuff (const TValue *obj, char *buff);
[[nodiscard]] LUAI_FUNC lu_byte luaO_hexavalue (int c);
LUAI_FUNC void luaO_tostring (lua_State *L, TValue *obj);
LUAI_FUNC const char *luaO_pushvfstring (lua_State *L, const char *fmt,
                                                       va_list argp);
LUAI_FUNC const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);

// Phase 115.1: std::span-based string utilities
LUAI_FUNC void luaO_chunkid (std::span<char> out, std::span<const char> source);

// C-style wrapper for compatibility
inline void luaO_chunkid (char *out, const char *source, size_t srclen) {
	luaO_chunkid(std::span(out, LUA_IDSIZE), std::span(source, srclen));
}


/*
** {==================================================================
** TValue Operator Overloading
** ===================================================================
*/

/* Forward declarations for lvm.h types/functions */
#ifndef F2Imod_defined
#define F2Imod_defined
enum class F2Imod {
  F2Ieq,     /* no rounding; accepts only integral values */
  F2Ifloor,  /* takes the floor of the number */
  F2Iceil    /* takes the ceiling of the number */
};
#endif

#ifndef luaV_flttointeger_declared
#define luaV_flttointeger_declared
LUAI_FUNC int luaV_flttointeger (lua_Number n, lua_Integer *p, F2Imod mode);
#endif

/* Forward declarations for comparison helpers (defined in lvm.cpp and lstring.h) */
/* These handle mixed int/float comparisons correctly */
[[nodiscard]] LUAI_FUNC int LTintfloat (lua_Integer i, lua_Number f);
[[nodiscard]] LUAI_FUNC int LEintfloat (lua_Integer i, lua_Number f);
[[nodiscard]] LUAI_FUNC int LTfloatint (lua_Number f, lua_Integer i);
[[nodiscard]] LUAI_FUNC int LEfloatint (lua_Number f, lua_Integer i);
[[nodiscard]] LUAI_FUNC int l_strcmp (const TString* ts1, const TString* ts2);
/* luaS_eqstr declared in lstring.h */

/* String comparison helpers (defined in lstring.h) */
bool eqshrstr(const TString* a, const TString* b) noexcept;  /* forward decl */

/*
** Operator< for TValue (numeric and string comparison only, no metamethods)
** For general comparison with metamethods, use luaV_lessthan()
*/
inline bool operator<(const TValue& l, const TValue& r) noexcept {
	// Both numbers?
	if (ttisnumber(&l) && ttisnumber(&r)) {
		if (ttisinteger(&l)) {
			lua_Integer li = ivalue(&l);
			if (ttisinteger(&r))
				return li < ivalue(&r);  /* both integers */
			else
				return LTintfloat(li, fltvalue(&r));  /* int < float */
		}
		else {
			lua_Number lf = fltvalue(&l);  /* l is float */
			if (ttisfloat(&r))
				return lf < fltvalue(&r);  /* both floats */
			else
				return LTfloatint(lf, ivalue(&r));  /* float < int */
		}
	}
	// Both strings? (no metamethods - raw comparison)
	else if (ttisstring(&l) && ttisstring(&r)) {
		return *tsvalue(&l) < *tsvalue(&r);  /* Use TString operator< */
	}
	// Different types or non-comparable types
	return false;
}

/*
** Operator<= for TValue (numeric and string comparison only, no metamethods)
** For general comparison with metamethods, use luaV_lessequal()
*/
inline bool operator<=(const TValue& l, const TValue& r) noexcept {
	// Both numbers?
	if (ttisnumber(&l) && ttisnumber(&r)) {
		if (ttisinteger(&l)) {
			lua_Integer li = ivalue(&l);
			if (ttisinteger(&r))
				return li <= ivalue(&r);  /* both integers */
			else
				return LEintfloat(li, fltvalue(&r));  /* int <= float */
		}
		else {
			lua_Number lf = fltvalue(&l);  /* l is float */
			if (ttisfloat(&r))
				return lf <= fltvalue(&r);  /* both floats */
			else
				return LEfloatint(lf, ivalue(&r));  /* float <= int */
		}
	}
	// Both strings? (no metamethods - raw comparison)
	else if (ttisstring(&l) && ttisstring(&r)) {
		return *tsvalue(&l) <= *tsvalue(&r);  /* Use TString operator<= */
	}
	// Different types or non-comparable types
	return false;
}

/*
** Operator== for TValue (raw equality only, no metamethods)
** For general equality with metamethods, use luaV_equalobj()
** This is similar to luaV_rawequalobj() but as an operator
*/
inline bool operator==(const TValue& l, const TValue& r) noexcept {
	if (ttype(&l) != ttype(&r))  /* different base types? */
		return false;
	else if (ttypetag(&l) != ttypetag(&r)) {
		/* Different variants - only numbers and strings can be equal across variants */
		switch (ttypetag(&l)) {
			case LUA_VNUMINT: {  /* int == float? */
				lua_Integer i2;
				return (luaV_flttointeger(fltvalue(&r), &i2, F2Imod::F2Ieq) &&
				        ivalue(&l) == i2);
			}
			case LUA_VNUMFLT: {  /* float == int? */
				lua_Integer i1;
				return (luaV_flttointeger(fltvalue(&l), &i1, F2Imod::F2Ieq) &&
				        i1 == ivalue(&r));
			}
			case LUA_VSHRSTR: case LUA_VLNGSTR: {
				/* Compare strings with different variants */
				return tsvalue(&l)->equals(tsvalue(&r));
			}
			default:
				return false;
		}
	}
	else {  /* same variant */
		switch (ttypetag(&l)) {
			case LUA_VNIL: case LUA_VFALSE: case LUA_VTRUE:
				return true;
			case LUA_VNUMINT:
				return ivalue(&l) == ivalue(&r);
			case LUA_VNUMFLT:
				return fltvalue(&l) == fltvalue(&r);
			case LUA_VLIGHTUSERDATA:
				return pvalue(&l) == pvalue(&r);
			case LUA_VSHRSTR:
				return eqshrstr(tsvalue(&l), tsvalue(&r));
			case LUA_VLNGSTR:
				return tsvalue(&l)->equals(tsvalue(&r));
			case LUA_VUSERDATA:
				return uvalue(&l) == uvalue(&r);
			case LUA_VLCF:
				return fvalue(&l) == fvalue(&r);
			default:  /* other collectable types (tables, closures, threads) */
				return gcvalue(&l) == gcvalue(&r);
		}
	}
}

/*
** Operator!= for TValue
*/
inline bool operator!=(const TValue& l, const TValue& r) noexcept {
	return !(l == r);
}


/*
** TString comparison operators
** Provide idiomatic C++ comparison syntax for TString objects
*/

/* operator< for TString - lexicographic ordering */
inline bool operator<(const TString& l, const TString& r) noexcept {
	return l_strcmp(&l, &r) < 0;
}

/* operator<= for TString - lexicographic ordering */
inline bool operator<=(const TString& l, const TString& r) noexcept {
	return l_strcmp(&l, &r) <= 0;
}

/* operator== for TString - equality check using existing equals() method */
inline bool operator==(const TString& l, const TString& r) noexcept {
	// Use equals() method which handles short vs long string optimization
	return l.equals(&r);
}

/* operator!= for TString - inequality check */
inline bool operator!=(const TString& l, const TString& r) noexcept {
	return !(l == r);
}

/* }================================================================== */


/*
** ===================================================================
** GC Type Safety Notes
** ===================================================================
** All GC-managed types inherit from GCBase<Derived> (CRTP pattern) which
** provides common GC fields (next, tt, marked). The reinterpret_cast
** operations used for GC pointer conversions are safe because:
**
** 1. All GC objects have a common initial sequence (GCObject fields)
** 2. Type tags are checked before conversions (via lua_assert)
** 3. Memory is allocated with proper alignment for all types
** 4. The CRTP pattern ensures type safety at compile time
**
** Note: These types do NOT have C++ standard layout due to CRTP and
** other C++ features, but the GC conversions remain safe through careful
** design and runtime type checking.
*/


/*
** {==================================================================
** Table fast-access inline functions
** ===================================================================
** Phase 121: Moved here from ltable.h to resolve circular dependencies
** These functions need both Table (from ltable.h) and TMS (from ltm.h)
*/

// Phase 88: Convert luaH_fastgeti and luaH_fastseti macros to inline functions
// These are hot-path table access functions used throughout the VM
inline void luaH_fastgeti(Table* t, lua_Integer k, TValue* res, lu_byte& tag) noexcept {
	Table* h = t;
	lua_Unsigned u = l_castS2U(k) - 1u;
	if (u < h->arraySize()) {
		tag = *h->getArrayTag(u);
		if (!tagisempty(tag)) {
			farr2val(h, u, tag, res);
		}
	} else {
		tag = luaH_getint(h, k, res);
	}
}

inline void luaH_fastseti(Table* t, lua_Integer k, TValue* val, int& hres) noexcept {
	Table* h = t;
	lua_Unsigned u = l_castS2U(k) - 1u;
	if (u < h->arraySize()) {
		lu_byte* tag = h->getArrayTag(u);
		if (checknoTM(h->getMetatable(), TMS::TM_NEWINDEX) || !tagisempty(*tag)) {
			fval2arr(h, u, tag, val);
			hres = HOK;
		} else {
			hres = ~cast_int(u);
		}
	} else {
		hres = luaH_psetint(h, k, val);
	}
}

/* }================================================================== */


#endif

