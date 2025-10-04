/*
** $Id: lobject.h $
** Type definitions for Lua objects
** See Copyright Notice in lua.h
*/


#ifndef lobject_h
#define lobject_h


#include <stdarg.h>


#include "llimits.h"
#include "lua.h"


/*
** Extra types for collectable non-values
*/
#define LUA_TUPVAL	LUA_NUMTYPES  /* upvalues */
#define LUA_TPROTO	(LUA_NUMTYPES+1)  /* function prototypes */
#define LUA_TDEADKEY	(LUA_NUMTYPES+2)  /* removed keys in tables */



/*
** number of all possible types (including LUA_TNONE but excluding DEADKEY)
*/
#define LUA_TOTALTYPES		(LUA_TPROTO + 2)


/*
** tags for Tagged Values have the following use of bits:
** bits 0-3: actual tag (a LUA_T* constant)
** bits 4-5: variant bits
** bit 6: whether value is collectable
*/

/* add variant bits to a type */
constexpr int makevariant(int t, int v) noexcept { return (t | (v << 4)); }



/*
** Union of all Lua values
*/
typedef union Value {
  struct GCObject *gc;    /* collectable objects */
  void *p;         /* light userdata */
  lua_CFunction f; /* light C functions */
  lua_Integer i;   /* integer numbers */
  lua_Number n;    /* float numbers */
  /* not used, but may avoid warnings for uninitialized value */
  lu_byte ub;
} Value;


/*
** Forward declarations for TValue accessor methods
*/
class TString;
class Udata;
class Table;
union Closure;
class LClosure;
class CClosure;
class lua_State;


/*
** Tagged Values. This is the basic representation of values in Lua:
** an actual value plus a tag with its type.
*/

class TValue {
public:
  Value value_;
  lu_byte tt_;

  // Inline accessors for hot-path access
  lu_byte getType() const noexcept { return tt_; }
  const Value& getValue() const noexcept { return value_; }
  Value& getValue() noexcept { return value_; }

  // Value accessors (Phase 15-16: Macro reduction)
  // Integer value (for VKINT/VNUMINT types)
  lua_Integer intValue() const noexcept { return value_.i; }

  // Float value (for VNUMFLT types)
  lua_Number floatValue() const noexcept { return value_.n; }

  // Pointer value (for VLIGHTUSERDATA)
  void* pointerValue() const noexcept { return value_.p; }

  // GC object value (for collectable types)
  GCObject* gcValue() const noexcept { return value_.gc; }

  // C function value (for light C functions)
  lua_CFunction functionValue() const noexcept { return value_.f; }

  // Phase 16: Type-specific value accessors
  // Note: These return pointers to specific types from GC union
  // The gco2* conversion happens in the inline wrapper functions below
  TString* stringValue() const noexcept { return reinterpret_cast<TString*>(value_.gc); }
  Udata* userdataValue() const noexcept { return reinterpret_cast<Udata*>(value_.gc); }
  Table* tableValue() const noexcept { return reinterpret_cast<Table*>(value_.gc); }
  Closure* closureValue() const noexcept { return reinterpret_cast<Closure*>(value_.gc); }
  LClosure* lClosureValue() const noexcept { return reinterpret_cast<LClosure*>(value_.gc); }
  CClosure* cClosureValue() const noexcept { return reinterpret_cast<CClosure*>(value_.gc); }
  lua_State* threadValue() const noexcept { return reinterpret_cast<lua_State*>(value_.gc); }

  // Number value (returns int or float depending on type)
  // Note: Actual conversion logic is in nvalue() wrapper below (needs type constants)
  lua_Number numberValue() const noexcept;

  // Phase 17: Setter methods (HOT PATH - performance critical!)
  // Note: These need type constants, so implementations are below
  void setNil() noexcept;
  void setFalse() noexcept;
  void setTrue() noexcept;
  void setInt(lua_Integer i) noexcept;
  void setFloat(lua_Number n) noexcept;
  void setPointer(void* p) noexcept;
  void setFunction(lua_CFunction f) noexcept;
  void setString(lua_State* L, TString* s) noexcept;
  void setUserdata(lua_State* L, Udata* u) noexcept;
  void setTable(lua_State* L, Table* t) noexcept;
  void setLClosure(lua_State* L, LClosure* cl) noexcept;
  void setCClosure(lua_State* L, CClosure* cl) noexcept;
  void setThread(lua_State* L, lua_State* th) noexcept;
  void setGCObject(lua_State* L, GCObject* gc) noexcept;

  // Change value (no type change - for optimization)
  void changeInt(lua_Integer i) noexcept { value_.i = i; }
  void changeFloat(lua_Number n) noexcept { value_.n = n; }

  // Copy from another TValue
  void copy(const TValue* other) noexcept {
    value_ = other->value_;
    tt_ = other->tt_;
  }

  // Low-level field access (for macros during transition)
  Value& valueField() noexcept { return value_; }
  const Value& valueField() const noexcept { return value_; }
  void setType(lu_byte t) noexcept { tt_ = t; }
};


/* Access to TValue's internal value union */
constexpr Value& val_(TValue* o) noexcept { return o->value_; }
constexpr const Value& val_(const TValue* o) noexcept { return o->value_; }
constexpr Value& valraw(TValue* o) noexcept { return val_(o); }
constexpr const Value& valraw(const TValue* o) noexcept { return val_(o); }


/* raw type tag of a TValue */
constexpr lu_byte rawtt(const TValue* o) noexcept { return o->tt_; }

/* tag with no variants (bits 0-3) */
constexpr int novariant(int t) noexcept { return (t & 0x0F); }

/* type tag of a TValue (bits 0-3 for tags + variant bits 4-5) */
constexpr int withvariant(int t) noexcept { return (t & 0x3F); }

constexpr int ttypetag(const TValue* o) noexcept { return withvariant(rawtt(o)); }

/* type of a TValue */
constexpr int ttype(const TValue* o) noexcept { return novariant(rawtt(o)); }


/* Macros to test type */
constexpr bool checktag(const TValue* o, int t) noexcept { return rawtt(o) == t; }
constexpr bool checktype(const TValue* o, int t) noexcept { return ttype(o) == t; }

/* Bit mark for collectable types */
#define BIT_ISCOLLECTABLE	(1 << 6)

/* mark a tag as collectable */
constexpr int ctb(int t) noexcept { return (t | BIT_ISCOLLECTABLE); }


/* Macros for internal tests */

/* collectable object has the same tag as the original value */
/* NOTE: righttt() defined as inline function after gcvalue() below */

/*
** Any value being manipulated by the program either is non
** collectable, or the collectable object has the right tag
** and it is not dead. The option 'L == NULL' allows other
** macros using this one to be used where L is not available.
*/
#define checkliveness(L,obj) \
	((void)L, lua_longassert(!iscollectable(obj) || \
		(righttt(obj) && (L == NULL || !isdead(G(L),gcvalue(obj))))))


/* Macros to set values */

/* set a value's tag */
inline void settt_(TValue* o, lu_byte t) noexcept { o->tt_ = t; }


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
#define LUA_VNIL	makevariant(LUA_TNIL, 0)

/* Empty slot (which might be different from a slot containing nil) */
#define LUA_VEMPTY	makevariant(LUA_TNIL, 1)

/* Value returned for a key not found in a table (absent key) */
#define LUA_VABSTKEY	makevariant(LUA_TNIL, 2)

/* Special variant to signal that a fast get is accessing a non-table */
#define LUA_VNOTABLE    makevariant(LUA_TNIL, 3)


/* macro to test for (any kind of) nil */
constexpr bool ttisnil(const TValue* v) noexcept { return checktype(v, LUA_TNIL); }

/*
** Macro to test the result of a table access. Formally, it should
** distinguish between LUA_VEMPTY/LUA_VABSTKEY/LUA_VNOTABLE and
** other tags. As currently nil is equivalent to LUA_VEMPTY, it is
** simpler to just test whether the value is nil.
*/
constexpr bool tagisempty(int tag) noexcept { return novariant(tag) == LUA_TNIL; }


/* macro to test for a standard nil */
constexpr bool ttisstrictnil(const TValue* o) noexcept { return checktag(o, LUA_VNIL); }


inline void setnilvalue(TValue* obj) noexcept { obj->setNil(); }


constexpr bool isabstkey(const TValue* v) noexcept { return checktag(v, LUA_VABSTKEY); }


/*
** function to detect non-standard nils (used only in assertions)
*/
constexpr bool isnonstrictnil(const TValue* v) noexcept {
	return ttisnil(v) && !ttisstrictnil(v);
}


/*
** By default, entries with any kind of nil are considered empty.
** (In any definition, values associated with absent keys must also
** be accepted as empty.)
*/
constexpr bool isempty(const TValue* v) noexcept { return ttisnil(v); }


/* macro defining a value corresponding to an absent key */
#define ABSTKEYCONSTANT		{NULL}, LUA_VABSTKEY


/* mark an entry as empty */
inline void setempty(TValue* v) noexcept { settt_(v, LUA_VEMPTY); }



/* }================================================================== */


/*
** {==================================================================
** Booleans
** ===================================================================
*/


#define LUA_VFALSE	makevariant(LUA_TBOOLEAN, 0)
#define LUA_VTRUE	makevariant(LUA_TBOOLEAN, 1)

constexpr bool ttisboolean(const TValue* o) noexcept { return checktype(o, LUA_TBOOLEAN); }
constexpr bool ttisfalse(const TValue* o) noexcept { return checktag(o, LUA_VFALSE); }
constexpr bool ttistrue(const TValue* o) noexcept { return checktag(o, LUA_VTRUE); }


constexpr bool l_isfalse(const TValue* o) noexcept { return ttisfalse(o) || ttisnil(o); }
constexpr bool tagisfalse(int t) noexcept { return (t == LUA_VFALSE || novariant(t) == LUA_TNIL); }



inline void setbfvalue(TValue* obj) noexcept { obj->setFalse(); }
inline void setbtvalue(TValue* obj) noexcept { obj->setTrue(); }

/* }================================================================== */


/*
** {==================================================================
** Threads
** ===================================================================
*/

#define LUA_VTHREAD		makevariant(LUA_TTHREAD, 0)

constexpr bool ttisthread(const TValue* o) noexcept { return checktag(o, ctb(LUA_VTHREAD)); }

inline lua_State* thvalue(const TValue* o) noexcept { return o->threadValue(); }

#ifndef __cplusplus
#define setthvalue(L,obj,x) \
  { TValue *io = (obj); lua_State *x_ = (x); \
    val_(io).gc = obj2gco(x_); settt_(io, ctb(LUA_VTHREAD)); \
    checkliveness(L,io); }
#endif

#define setthvalue2s(L,o,t)	setthvalue(L,s2v(o),t)

/* }================================================================== */


/*
** {==================================================================
** Collectable Objects
** ===================================================================
*/

/*
** Common Header for all collectable objects (in macro form, to be
** included in other objects)
*/
#define CommonHeader	struct GCObject *next; lu_byte tt; lu_byte marked


/* Common type for all collectable objects */
class GCObject {
public:
  CommonHeader;

  // Inline accessors
  GCObject* getNext() const noexcept { return next; }
  void setNext(GCObject* n) noexcept { next = n; }
  lu_byte getType() const noexcept { return tt; }
  lu_byte getMarked() const noexcept { return marked; }
  void setMarked(lu_byte m) noexcept { marked = m; }
  bool isMarked() const noexcept { return marked != 0; }

  // Phase 20: GC color and age methods (requires lgc.h constants)
  // These will be defined after lgc.h is included
  inline bool isWhite() const noexcept;
  inline bool isBlack() const noexcept;
  inline bool isGray() const noexcept;
  inline lu_byte getAge() const noexcept;
  inline void setAge(lu_byte age) noexcept;
  inline bool isOld() const noexcept;
};

/*
** CRTP Base class for all GC-managed objects
** Provides common GC fields and operations without vtable overhead
** Usage: class Table : public GCBase<Table> { ... };
*/
template<typename Derived>
class GCBase {
protected:
    GCObject* next_;
    lu_byte tt_;
    lu_byte marked_;

public:
    // GC operations
    constexpr GCObject* getNext() const noexcept { return next_; }
    constexpr void setNext(GCObject* n) noexcept { next_ = n; }

    constexpr lu_byte getType() const noexcept { return tt_; }
    constexpr void setType(lu_byte t) noexcept { tt_ = t; }

    constexpr lu_byte getMarked() const noexcept { return marked_; }
    constexpr void setMarked(lu_byte m) noexcept { marked_ = m; }

    constexpr bool isMarked() const noexcept { return marked_ != 0; }

    // Cast to GCObject* for compatibility with existing C code
    GCObject* toGCObject() noexcept {
        return reinterpret_cast<GCObject*>(static_cast<Derived*>(this));
    }
    const GCObject* toGCObject() const noexcept {
        return reinterpret_cast<const GCObject*>(static_cast<const Derived*>(this));
    }
};

constexpr bool iscollectable(const TValue* o) noexcept { return (rawtt(o) & BIT_ISCOLLECTABLE) != 0; }

inline GCObject* gcvalue(const TValue* o) noexcept { return o->gcValue(); }

#define gcvalueraw(v)	((v).gc)

/* setgcovalue now defined as inline function below */

/* collectable object has the same tag as the original value (inline version) */
inline bool righttt(const TValue* obj) noexcept { return ttypetag(obj) == gcvalue(obj)->tt; }

/* }================================================================== */


/*
** {==================================================================
** TValue assignment functions
** ===================================================================
*/

/*
** NOTE: setobj(), setobjs2s(), setobj2s() are inline functions defined
** in lgc.h (after all dependencies) because they need G() from lstate.h
** and isdead() from lgc.h.
**
** setobjt2t, setobj2n, setobj2t are simple aliases to setobj.
*/

#define setobjt2t setobj
#define setobj2n setobj
#define setobj2t setobj

/* }================================================================== */


/*
** {==================================================================
** Numbers
** ===================================================================
*/

/* Variant tags for numbers */
#define LUA_VNUMINT	makevariant(LUA_TNUMBER, 0)  /* integer numbers */
#define LUA_VNUMFLT	makevariant(LUA_TNUMBER, 1)  /* float numbers */

constexpr bool ttisnumber(const TValue* o) noexcept { return checktype(o, LUA_TNUMBER); }
constexpr bool ttisfloat(const TValue* o) noexcept { return checktag(o, LUA_VNUMFLT); }
constexpr bool ttisinteger(const TValue* o) noexcept { return checktag(o, LUA_VNUMINT); }

// TValue::numberValue() implementation (needs LUA_VNUMINT constant)
inline lua_Number TValue::numberValue() const noexcept {
  return (tt_ == LUA_VNUMINT) ? static_cast<lua_Number>(value_.i) : value_.n;
}

inline lua_Number nvalue(const TValue* o) noexcept { return o->numberValue(); }

inline lua_Number fltvalue(const TValue* o) noexcept { return o->floatValue(); }
inline lua_Integer ivalue(const TValue* o) noexcept { return o->intValue(); }

#define fltvalueraw(v)	((v).n)
#define ivalueraw(v)	((v).i)

inline void setfltvalue(TValue* obj, lua_Number x) noexcept { obj->setFloat(x); }
inline void chgfltvalue(TValue* obj, lua_Number x) noexcept { obj->changeFloat(x); }
inline void setivalue(TValue* obj, lua_Integer x) noexcept { obj->setInt(x); }
inline void chgivalue(TValue* obj, lua_Integer x) noexcept { obj->changeInt(x); }

/* }================================================================== */


/*
** {==================================================================
** Strings
** ===================================================================
*/

/* Variant tags for strings */
#define LUA_VSHRSTR	makevariant(LUA_TSTRING, 0)  /* short strings */
#define LUA_VLNGSTR	makevariant(LUA_TSTRING, 1)  /* long strings */

constexpr bool ttisstring(const TValue* o) noexcept { return checktype(o, LUA_TSTRING); }
constexpr bool ttisshrstring(const TValue* o) noexcept { return checktag(o, ctb(LUA_VSHRSTR)); }
constexpr bool ttislngstring(const TValue* o) noexcept { return checktag(o, ctb(LUA_VLNGSTR)); }

#define tsvalueraw(v)	(gco2ts((v).gc))

inline TString* tsvalue(const TValue* o) noexcept { return o->stringValue(); }

/* setsvalue now defined as inline function below */

/* set a string to the stack */
#define setsvalue2s(L,o,s)	setsvalue(L,s2v(o),s)

/* set a string to a new object */
#define setsvalue2n	setsvalue


/* Kinds of long strings (stored in 'shrlen') */
#define LSTRREG		-1  /* regular long string */
#define LSTRFIX		-2  /* fixed external long string */
#define LSTRMEM		-3  /* external long string with deallocation */


/*
** Header for a string value.
*/
class TString {
public:
  CommonHeader;  // GC fields: next, tt, marked
  lu_byte extra;  /* reserved words for short strings; "has hash" for longs */
  ls_byte shrlen;  /* length for short strings, negative for long strings */
  unsigned int hash;
  union {
    size_t lnglen;  /* length for long strings */
    TString *hnext;  /* linked list for hash table */
  } u;
  char *contents;  /* pointer to content in long strings */
  lua_Alloc falloc;  /* deallocation function for external strings */
  void *ud;  /* user data for external strings */

  // Type checks
  bool isShort() const noexcept { return shrlen >= 0; }
  bool isLong() const noexcept { return shrlen < 0; }

  // Accessors
  size_t length() const noexcept {
    return isShort() ? static_cast<size_t>(shrlen) : u.lnglen;
  }
  unsigned int getHash() const noexcept { return hash; }
  const char* c_str() const noexcept {
    return isShort() ? cast_charp(&contents) : contents;
  }

  // Hash table operations
  TString* getNext() const noexcept { return u.hnext; }
  void setNext(TString* next_str) noexcept { u.hnext = next_str; }

  // Method declarations (implemented in lstring.cpp)
  unsigned hashLongStr();
  bool equals(TString* other);

  // Static factory-like functions (still use luaS_* for now)
  // static TString* create(lua_State* L, const char* str, size_t len);
};


/* Check if string is short (wrapper for backward compatibility) */
inline bool strisshr(const TString* ts) noexcept { return ts->isShort(); }

/* Check if string is external (fixed or with custom deallocator) */
inline bool isextstr(const TValue* v) noexcept {
	return ttislngstring(v) && tsvalue(v)->shrlen != LSTRREG;
}

/*
** Get the actual string (array of bytes) from a 'TString'. (Generic
** version and specialized versions for long and short strings.)
*/
inline char* rawgetshrstr(TString* ts) noexcept {
	return cast_charp(&ts->contents);
}
inline const char* rawgetshrstr(const TString* ts) noexcept {
	return cast_charp(&ts->contents);
}
#define getshrstr(ts)	check_exp(strisshr(ts), rawgetshrstr(ts))
#define getlngstr(ts)	check_exp(!strisshr(ts), (ts)->contents)
#define getstr(ts) 	(strisshr(ts) ? rawgetshrstr(ts) : (ts)->contents)


/* get string length from 'TString *ts' */
#define tsslen(ts)  \
	(strisshr(ts) ? cast_sizet((ts)->shrlen) : (ts)->u.lnglen)

/*
** Get string and length */
#define getlstr(ts, len)  \
	(strisshr(ts) \
	? (cast_void((len) = cast_sizet((ts)->shrlen)), rawgetshrstr(ts)) \
	: (cast_void((len) = (ts)->u.lnglen), (ts)->contents))

/* }================================================================== */


/*
** {==================================================================
** Userdata
** ===================================================================
*/


/*
** Light userdata should be a variant of userdata, but for compatibility
** reasons they are also different types.
*/
#define LUA_VLIGHTUSERDATA	makevariant(LUA_TLIGHTUSERDATA, 0)

#define LUA_VUSERDATA		makevariant(LUA_TUSERDATA, 0)

constexpr bool ttislightuserdata(const TValue* o) noexcept { return checktag(o, LUA_VLIGHTUSERDATA); }
constexpr bool ttisfulluserdata(const TValue* o) noexcept { return checktag(o, ctb(LUA_VUSERDATA)); }

inline void* pvalue(const TValue* o) noexcept { return o->pointerValue(); }

inline Udata* uvalue(const TValue* o) noexcept { return o->userdataValue(); }

#define pvalueraw(v)	((v).p)

/* setpvalue and setuvalue now defined as inline functions below */


/* Ensures that addresses after this type are always fully aligned. */
typedef union UValue {
  TValue uv;
  LUAI_MAXALIGN;  /* ensures maximum alignment for udata bytes */
} UValue;


/*
** Header for userdata with user values;
** memory area follows the end of this structure.
*/
class Udata {
public:
  CommonHeader;
  unsigned short nuvalue;  /* number of user values */
  size_t len;  /* number of bytes */
  struct Table *metatable;
  GCObject *gclist;
  UValue uv[1];  /* user values */

  // Inline accessors
  size_t getLen() const noexcept { return len; }
  unsigned short getNumUserValues() const noexcept { return nuvalue; }
  Table* getMetatable() const noexcept { return metatable; }
  void setMetatable(Table* mt) noexcept { metatable = mt; }
  UValue* getUserValue(int idx) noexcept { return &uv[idx]; }
  const UValue* getUserValue(int idx) const noexcept { return &uv[idx]; }
  // Note: getMemory() uses macro udatamemoffset which requires Udata0 to be defined
  inline void* getMemory() noexcept;
  inline const void* getMemory() const noexcept;
};


/*
** Header for userdata with no user values. These userdata do not need
** to be gray during GC, and therefore do not need a 'gclist' field.
** To simplify, the code always use 'Udata' for both kinds of userdata,
** making sure it never accesses 'gclist' on userdata with no user values.
** This structure here is used only to compute the correct size for
** this representation. (The 'bindata' field in its end ensures correct
** alignment for binary data following this header.)
*/
typedef struct Udata0 {
  CommonHeader;
  unsigned short nuvalue;  /* number of user values */
  size_t len;  /* number of bytes */
  struct Table *metatable;
  union {LUAI_MAXALIGN;} bindata;
} Udata0;


/* compute the offset of the memory area of a userdata */
#define udatamemoffset(nuv) \
       ((nuv) == 0 ? offsetof(Udata0, bindata)  \
		   : offsetof(Udata, uv) + (sizeof(UValue) * (nuv)))

/* get the address of the memory block inside 'Udata' */
#define getudatamem(u)	(cast_charp(u) + udatamemoffset((u)->nuvalue))

/* compute the size of a userdata */
#define sizeudata(nuv,nb)	(udatamemoffset(nuv) + (nb))

// Implementation of Udata::getMemory() now that Udata0 is defined
inline void* Udata::getMemory() noexcept {
  return getudatamem(this);
}
inline const void* Udata::getMemory() const noexcept {
  return getudatamem(const_cast<Udata*>(this));
}

/* }================================================================== */


/*
** {==================================================================
** Prototypes
** ===================================================================
*/

#define LUA_VPROTO	makevariant(LUA_TPROTO, 0)


typedef l_uint32 Instruction;


/*
** Description of an upvalue for function prototypes
*/
class Upvaldesc {
public:
  TString *name;  /* upvalue name (for debug information) */
  lu_byte instack;  /* whether it is in stack (register) */
  lu_byte idx;  /* index of upvalue (in stack or in outer function's list) */
  lu_byte kind;  /* kind of corresponding variable */

  // Inline accessors
  TString* getName() const noexcept { return name; }
  bool isInStack() const noexcept { return instack != 0; }
  lu_byte getIndex() const noexcept { return idx; }
  lu_byte getKind() const noexcept { return kind; }
};


/*
** Description of a local variable for function prototypes
** (used for debug information)
*/
class LocVar {
public:
  TString *varname;
  int startpc;  /* first point where variable is active */
  int endpc;    /* first point where variable is dead */

  // Inline accessors
  TString* getVarName() const noexcept { return varname; }
  int getStartPC() const noexcept { return startpc; }
  int getEndPC() const noexcept { return endpc; }
  bool isActive(int pc) const noexcept { return startpc <= pc && pc < endpc; }
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
public:
  int pc;
  int line;

  // Inline accessors
  int getPC() const noexcept { return pc; }
  int getLine() const noexcept { return line; }
};


/*
** Flags in Prototypes
*/
#define PF_ISVARARG	1
#define PF_FIXED	2  /* prototype has parts in fixed memory */


/*
** Function Prototypes
*/
class Proto {
public:
  CommonHeader;
  lu_byte numparams;  /* number of fixed (named) parameters */
  lu_byte flag;
  lu_byte maxstacksize;  /* number of registers needed by this function */
  int sizeupvalues;  /* size of 'upvalues' */
  int sizek;  /* size of 'k' */
  int sizecode;
  int sizelineinfo;
  int sizep;  /* size of 'p' */
  int sizelocvars;
  int sizeabslineinfo;  /* size of 'abslineinfo' */
  int linedefined;  /* debug information  */
  int lastlinedefined;  /* debug information  */
  TValue *k;  /* constants used by the function */
  Instruction *code;  /* opcodes */
  struct Proto **p;  /* functions defined inside the function */
  Upvaldesc *upvalues;  /* upvalue information */
  ls_byte *lineinfo;  /* information about source lines (debug information) */
  AbsLineInfo *abslineinfo;  /* idem */
  LocVar *locvars;  /* information about local variables (debug information) */
  TString  *source;  /* used for debug information */
  GCObject *gclist;

  // Inline accessors
  lu_byte getNumParams() const noexcept { return numparams; }
  lu_byte getMaxStackSize() const noexcept { return maxstacksize; }
  int getCodeSize() const noexcept { return sizecode; }
  int getConstantsSize() const noexcept { return sizek; }
  int getUpvaluesSize() const noexcept { return sizeupvalues; }
  int getProtosSize() const noexcept { return sizep; }
  TString* getSource() const noexcept { return source; }
  bool isVarArg() const noexcept { return flag != 0; }
  Instruction* getCode() const noexcept { return code; }
  TValue* getConstants() const noexcept { return k; }

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

#define LUA_VUPVAL	makevariant(LUA_TUPVAL, 0)


/* Variant tags for functions */
#define LUA_VLCL	makevariant(LUA_TFUNCTION, 0)  /* Lua closure */
#define LUA_VLCF	makevariant(LUA_TFUNCTION, 1)  /* light C function */
#define LUA_VCCL	makevariant(LUA_TFUNCTION, 2)  /* C closure */

constexpr bool ttisfunction(const TValue* o) noexcept { return checktype(o, LUA_TFUNCTION); }
constexpr bool ttisLclosure(const TValue* o) noexcept { return checktag(o, ctb(LUA_VLCL)); }
constexpr bool ttislcf(const TValue* o) noexcept { return checktag(o, LUA_VLCF); }
constexpr bool ttisCclosure(const TValue* o) noexcept { return checktag(o, ctb(LUA_VCCL)); }
constexpr bool ttisclosure(const TValue* o) noexcept { return ttisLclosure(o) || ttisCclosure(o); }


#define isLfunction(o)	ttisLclosure(o)

inline Closure* clvalue(const TValue* o) noexcept { return o->closureValue(); }
inline LClosure* clLvalue(const TValue* o) noexcept { return o->lClosureValue(); }
inline CClosure* clCvalue(const TValue* o) noexcept { return o->cClosureValue(); }

inline lua_CFunction fvalue(const TValue* o) noexcept { return o->functionValue(); }

#define fvalueraw(v)	((v).f)

/* setclLvalue now defined as inline function below */

#define setclLvalue2s(L,o,cl)	setclLvalue(L,s2v(o),cl)

/* setfvalue now defined as inline function below */

/* setclCvalue now defined as inline function below */


/*
** Upvalues for Lua closures
*/
class UpVal {
public:
  CommonHeader;
  union {
    TValue *p;  /* points to stack or to its own value */
    ptrdiff_t offset;  /* used while the stack is being reallocated */
  } v;
  union {
    struct {  /* (when open) */
      struct UpVal *next;  /* linked list */
      struct UpVal **previous;
    } open;
    TValue value;  /* the value (when closed) */
  } u;

  // Inline accessors
  bool isOpen() const noexcept { return v.p != &u.value; }
  TValue* getValue() noexcept { return v.p; }
  const TValue* getValue() const noexcept { return v.p; }

  // Methods (implemented in lfunc.cpp)
  void unlink();
};



#define ClosureHeader \
	CommonHeader; lu_byte nupvalues; GCObject *gclist

class CClosure {
public:
  ClosureHeader;
  lua_CFunction f;
  TValue upvalue[1];  /* list of upvalues */

  // Inline accessors
  lua_CFunction getFunction() const noexcept { return f; }
  lu_byte getNumUpvalues() const noexcept { return nupvalues; }
  TValue* getUpvalue(int idx) noexcept { return &upvalue[idx]; }
  const TValue* getUpvalue(int idx) const noexcept { return &upvalue[idx]; }
};

class LClosure {
public:
  ClosureHeader;
  struct Proto *p;
  UpVal *upvals[1];  /* list of upvalues */

  // Inline accessors
  Proto* getProto() const noexcept { return p; }
  lu_byte getNumUpvalues() const noexcept { return nupvalues; }
  UpVal* getUpval(int idx) const noexcept { return upvals[idx]; }
  void setUpval(int idx, UpVal* uv) noexcept { upvals[idx] = uv; }

  // Methods (implemented in lfunc.cpp)
  void initUpvals(lua_State* L);
};


typedef union Closure {
  CClosure c;
  LClosure l;
} Closure;


#define getproto(o)	(clLvalue(o)->p)

/* }================================================================== */


/*
** {==================================================================
** Tables
** ===================================================================
*/

#define LUA_VTABLE	makevariant(LUA_TTABLE, 0)

constexpr bool ttistable(const TValue* o) noexcept { return checktag(o, ctb(LUA_VTABLE)); }

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
  tt_ = ctb(s->tt);
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
  tt_ = ctb(gc->tt);
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

/* Note: sethvalue and other setter macros are now defined as inline functions above in C++ */

#define sethvalue2s(L,o,h)	sethvalue(L,s2v(o),h)


/*
** Nodes for Hash tables: A pack of two TValue's (key-value pairs)
** plus a 'next' field to link colliding entries. The distribution
** of the key's fields ('key_tt' and 'key_val') not forming a proper
** 'TValue' allows for a smaller size for 'Node' both in 4-byte
** and 8-byte alignments.
*/
typedef union Node {
  struct NodeKey {
    Value value_;  /* value */
    lu_byte tt_;   /* value type tag */
    lu_byte key_tt;  /* key type */
    int next;  /* for chaining */
    Value key_val;  /* key value */
  } u;
  TValue i_val;  /* direct access to node's value as a proper 'TValue' */
} Node;


/* copy a value into a key */
#define setnodekey(node,obj) \
	{ Node *n_=(node); const TValue *io_=(obj); \
	  n_->u.key_val = io_->value_; n_->u.key_tt = io_->tt_; }


/* copy a value from a key */
#define getnodekey(L,obj,node) \
	{ TValue *io_=(obj); const Node *n_=(node); \
	  io_->value_ = n_->u.key_val; io_->tt_ = n_->u.key_tt; \
	  checkliveness(L,io_); }



// Table class - using CRTP for GC management
// NOTE: For now keeping CommonHeader instead of inheriting to maintain macro compatibility
// Will gradually migrate macros to use methods
class Table {
public:
  CommonHeader;  // GC fields: next, tt, marked
  lu_byte flags;  /* 1<<p means tagmethod(p) is not present */
  lu_byte lsizenode;  /* log2 of number of slots of 'node' array */
  unsigned int asize;  /* number of slots in 'array' array */
  Value *array;  /* array part */
  Node *node;
  Table *metatable;
  GCObject *gclist;

  // Inline accessors
  unsigned int arraySize() const noexcept { return asize; }
  unsigned int nodeSize() const noexcept { return (1u << lsizenode); }
  Table* getMetatable() const noexcept { return metatable; }
  void setMetatable(Table* mt) noexcept { metatable = mt; }

  // Flag operations (inline for performance)
  // Note: BITDUMMY = (1 << 6), defined in ltable.h
  bool isDummy() const noexcept { return (flags & (1 << 6)) != 0; }
  void setDummy() noexcept { flags |= (1 << 6); }
  void setNoDummy() noexcept { flags &= cast_byte(~(1 << 6)); }
  // invalidateTMCache uses maskflags from ltm.h, so can't inline here - use macro instead

  // Node accessors (Phase 19: Table macro reduction)
  Node* getNode(unsigned int i) noexcept { return &node[i]; }
  const Node* getNode(unsigned int i) const noexcept { return &node[i]; }

  // Method declarations (implemented in ltable.cpp)
  lu_byte get(const TValue* key, TValue* res);
  lu_byte getInt(lua_Integer key, TValue* res);
  lu_byte getShortStr(TString* key, TValue* res);
  lu_byte getStr(TString* key, TValue* res);
  const TValue* HgetShortStr(TString* key);

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
};


/*
** Macros to manipulate keys inserted in nodes
*/
#define keytt(node)		((node)->u.key_tt)
#define keyval(node)		((node)->u.key_val)

#define keyisnil(node)		(keytt(node) == LUA_TNIL)
#define keyisinteger(node)	(keytt(node) == LUA_VNUMINT)
#define keyival(node)		(keyval(node).i)
#define keyisshrstr(node)	(keytt(node) == ctb(LUA_VSHRSTR))
#define keystrval(node)		(gco2ts(keyval(node).gc))

#define setnilkey(node)		(keytt(node) = LUA_TNIL)

#define keyiscollectable(n)	(keytt(n) & BIT_ISCOLLECTABLE)

#define gckey(n)	(keyval(n).gc)
#define gckeyN(n)	(keyiscollectable(n) ? gckey(n) : NULL)


/*
** Dead keys in tables have the tag DEADKEY but keep their original
** gcvalue. This distinguishes them from regular keys but allows them to
** be found when searched in a special way. ('next' needs that to find
** keys removed from a table during a traversal.)
*/
#define setdeadkey(node)	(keytt(node) = LUA_TDEADKEY)
#define keyisdead(node)		(keytt(node) == LUA_TDEADKEY)

/* }================================================================== */



/*
** 'module' operation for hashing (size is always a power of 2)
*/
#define lmod(s,size) \
	(check_exp((size&(size-1))==0, (cast_uint(s) & cast_uint((size)-1))))


#define twoto(x)	(1u<<(x))
#define sizenode(t)	(twoto((t)->lsizenode))


/* size of buffer for 'luaO_utf8esc' function */
#define UTF8BUFFSZ	8


/* macro to call 'luaO_pushvfstring' correctly */
#define pushvfstring(L, argp, fmt, msg)	\
  { va_start(argp, fmt); \
  msg = luaO_pushvfstring(L, fmt, argp); \
  va_end(argp); \
  if (msg == NULL) luaD_throw(L, LUA_ERRMEM);  /* only after 'va_end' */ }


LUAI_FUNC int luaO_utf8esc (char *buff, l_uint32 x);
LUAI_FUNC lu_byte luaO_ceillog2 (unsigned int x);
LUAI_FUNC lu_byte luaO_codeparam (unsigned int p);
LUAI_FUNC l_mem luaO_applyparam (lu_byte p, l_mem x);

LUAI_FUNC int luaO_rawarith (lua_State *L, int op, const TValue *p1,
                             const TValue *p2, TValue *res);
LUAI_FUNC void luaO_arith (lua_State *L, int op, const TValue *p1,
                           const TValue *p2, StkId res);
LUAI_FUNC size_t luaO_str2num (const char *s, TValue *o);
LUAI_FUNC unsigned luaO_tostringbuff (const TValue *obj, char *buff);
LUAI_FUNC lu_byte luaO_hexavalue (int c);
LUAI_FUNC void luaO_tostring (lua_State *L, TValue *obj);
LUAI_FUNC const char *luaO_pushvfstring (lua_State *L, const char *fmt,
                                                       va_list argp);
LUAI_FUNC const char *luaO_pushfstring (lua_State *L, const char *fmt, ...);
LUAI_FUNC void luaO_chunkid (char *out, const char *source, size_t srclen);


#endif

