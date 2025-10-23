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
#include "ltvalue.h"  /* TValue class */

/* Forward declarations */
enum class GCAge : lu_byte;

/*
** Extra types for collectable non-values
*/
inline constexpr int LUA_TUPVAL = LUA_NUMTYPES;      /* upvalues */
inline constexpr int LUA_TPROTO = (LUA_NUMTYPES+1);  /* function prototypes */
inline constexpr int LUA_TDEADKEY = (LUA_NUMTYPES+2);  /* removed keys in tables */



/*
** number of all possible types (including LUA_TNONE but excluding DEADKEY)
*/
inline constexpr int LUA_TOTALTYPES = (LUA_TPROTO + 2);


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
#define ABSTKEYCONSTANT		{NULL}, LUA_VABSTKEY


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
** Collectable Objects
** ===================================================================
*/

/*
** CommonHeader macro deprecated - GC objects now inherit from GCBase
*/


/* Common type for all collectable objects */
class GCObject {
protected:
  GCObject* next;
  lu_byte tt;
  lu_byte marked;

public:
  // Inline accessors
  GCObject* getNext() const noexcept { return next; }
  void setNext(GCObject* n) noexcept { next = n; }
  // Pointer-to-pointer for efficient GC list manipulation (allows in-place removal)
  GCObject** getNextPtr() noexcept { return &next; }
  lu_byte getType() const noexcept { return tt; }
  void setType(lu_byte t) noexcept { tt = t; }
  lu_byte getMarked() const noexcept { return marked; }
  void setMarked(lu_byte m) noexcept { marked = m; }
  bool isMarked() const noexcept { return marked != 0; }

  // Marked field bit manipulation methods
  void setMarkedBit(int bit) noexcept { marked |= cast_byte(1 << bit); }
  void clearMarkedBit(int bit) noexcept { marked &= cast_byte(~(1 << bit)); }
  void clearMarkedBits(int mask) noexcept { marked &= cast_byte(~mask); }

  // Marked field bit manipulation helpers (for backward compatibility)
  lu_byte& getMarkedRef() noexcept { return marked; }

  // GC color and age methods (defined in lgc.h after constants are available)
  inline bool isWhite() const noexcept;
  inline bool isBlack() const noexcept;
  inline bool isGray() const noexcept;
  inline GCAge getAge() const noexcept;
  inline void setAge(GCAge age) noexcept;
  inline bool isOld() const noexcept;

  // GC operations (implemented in lgc.cpp)
  void fix(lua_State* L);
  void checkFinalizer(lua_State* L, Table* mt);
};

/*
** CRTP (Curiously Recurring Template Pattern) Base class for all GC-managed objects
**
** DESIGN PATTERN:
** CRTP is a C++ idiom where a class X derives from a template base class using X itself
** as a template parameter: class X : public Base<X>
**
** Benefits compared to traditional polymorphism (virtual functions):
** 1. Zero runtime overhead - no vtable pointer, no virtual function indirection
** 2. Compile-time polymorphism - compiler can inline all method calls
** 3. Type safety - each derived class gets its own type-specific methods
** 4. Same memory layout as plain C struct - maintains C API compatibility
**
** Usage in Lua's GC system:
** - Table : public GCBase<Table>
** - TString : public GCBase<TString>
** - Proto : public GCBase<Proto>
** - LClosure : public GCBase<LClosure>
** - CClosure : public GCBase<CClosure>
** - UpVal : public GCBase<UpVal>
** - Udata : public GCBase<Udata>
** - lua_State : public GCBase<lua_State>  (thread object)
**
** CRITICAL INVARIANT:
** Memory layout MUST match GCObject exactly:
**   GCObject *next; lu_byte tt; lu_byte marked
**
** This allows safe casting between GCObject* and Derived* without pointer adjustment.
** The static_assert in each derived class verifies this invariant at compile time.
**
** PERFORMANCE:
** This design eliminated the vtable overhead from the original Lua C implementation
** while gaining C++ type safety and encapsulation. All color-checking methods
** (isWhite, isBlack, isGray) compile to simple bit tests with no function call overhead.
**
** See claude.md for detailed discussion of this architectural decision.
*/
template<typename Derived>
class GCBase: public GCObject {
public:
    // Accessor methods (preferred over direct field access)
    constexpr GCObject* getNext() const noexcept { return next; }
    constexpr void setNext(GCObject* n) noexcept { next = n; }

    constexpr lu_byte getType() const noexcept { return tt; }
    constexpr void setType(lu_byte t) noexcept { tt = t; }

    constexpr lu_byte getMarked() const noexcept { return marked; }
    constexpr void setMarked(lu_byte m) noexcept { marked = m; }

    constexpr bool isMarked() const noexcept { return marked != 0; }

    // GC color and age methods (defined in lgc.h after constants)
    inline void setAge(GCAge age) noexcept;
    inline bool isOld() const noexcept;

    // Cast to GCObject* for compatibility
    GCObject* toGCObject() noexcept {
        return reinterpret_cast<GCObject*>(static_cast<Derived*>(this));
    }
    const GCObject* toGCObject() const noexcept {
        return reinterpret_cast<const GCObject*>(static_cast<const Derived*>(this));
    }
};

constexpr bool iscollectable(const TValue* o) noexcept { return (rawtt(o) & BIT_ISCOLLECTABLE) != 0; }

constexpr bool TValue::isCollectable() const noexcept { return (tt_ & BIT_ISCOLLECTABLE) != 0; }

inline GCObject* gcvalue(const TValue* o) noexcept { return o->gcValue(); }

constexpr GCObject* gcvalueraw(const Value& v) noexcept { return v.gc; }

/* setgcovalue now defined as inline function below */

/* collectable object has the same tag as the original value (inline version) */
inline bool righttt(const TValue* obj) noexcept { return ttypetag(obj) == gcvalue(obj)->getType(); }

inline bool TValue::hasRightType() const noexcept { return typeTag() == gcValue()->getType(); }

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
** Strings
** ===================================================================
*/

/* Variant tags for strings */
inline constexpr int LUA_VSHRSTR = makevariant(LUA_TSTRING, 0);  /* short strings */
inline constexpr int LUA_VLNGSTR = makevariant(LUA_TSTRING, 1);  /* long strings */

constexpr bool ttisstring(const TValue* o) noexcept { return checktype(o, LUA_TSTRING); }
constexpr bool ttisshrstring(const TValue* o) noexcept { return checktag(o, ctb(LUA_VSHRSTR)); }
constexpr bool ttislngstring(const TValue* o) noexcept { return checktag(o, ctb(LUA_VLNGSTR)); }

constexpr bool TValue::isString() const noexcept { return checktype(this, LUA_TSTRING); }
constexpr bool TValue::isShortString() const noexcept { return checktag(this, ctb(LUA_VSHRSTR)); }
constexpr bool TValue::isLongString() const noexcept { return checktag(this, ctb(LUA_VLNGSTR)); }

inline TString* tsvalue(const TValue* o) noexcept { return o->stringValue(); }



/* Kinds of long strings (stored in 'shrlen') */
#define LSTRREG		-1  /* regular long string */
#define LSTRFIX		-2  /* fixed external long string */
#define LSTRMEM		-3  /* external long string with deallocation */


/*
** Header for a string value.
*/
// TString inherits from GCBase (CRTP)
class TString : public GCBase<TString> {
private:
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

public:
  // Type checks
  bool isShort() const noexcept { return shrlen >= 0; }
  bool isLong() const noexcept { return shrlen < 0; }
  bool isExternal() const noexcept { return isLong() && shrlen != LSTRREG; }

  // Accessors
  size_t length() const noexcept {
    return isShort() ? static_cast<size_t>(shrlen) : u.lnglen;
  }
  ls_byte getShrlen() const noexcept { return shrlen; }
  size_t getLnglen() const noexcept { return u.lnglen; }
  unsigned int getHash() const noexcept { return hash; }
  lu_byte getExtra() const noexcept { return extra; }
  const char* c_str() const noexcept {
    return isShort() ? cast_charp(&contents) : contents;
  }
  char* getContentsPtr() noexcept { return isShort() ? cast_charp(&contents) : contents; }
  char* getContentsField() noexcept { return contents; }
  const char* getContentsField() const noexcept { return contents; }
  char* getContentsAddr() noexcept { return cast_charp(&contents); }
  const char* getContentsAddr() const noexcept { return cast_charp(&contents); }
  lua_Alloc getFalloc() const noexcept { return falloc; }
  void* getUserData() const noexcept { return ud; }

  // Setters
  void setExtra(lu_byte e) noexcept { extra = e; }
  void setShrlen(ls_byte len) noexcept { shrlen = len; }
  void setHash(unsigned int h) noexcept { hash = h; }
  void setLnglen(size_t len) noexcept { u.lnglen = len; }
  void setContents(char* c) noexcept { contents = c; }
  void setFalloc(lua_Alloc f) noexcept { falloc = f; }
  void setUserData(void* data) noexcept { ud = data; }

  // Hash table operations
  TString* getNext() const noexcept { return u.hnext; }
  void setNext(TString* next_str) noexcept { u.hnext = next_str; }

  // Helper for offset calculations
  static constexpr size_t fallocOffset() noexcept {
    // Offset of falloc field accounting for alignment
    // Must include GCObject base!
    struct OffsetHelper {
      GCObject base;
      lu_byte extra;
      ls_byte shrlen;
      unsigned int hash;
      union { size_t lnglen; TString* hnext; } u;
      char* contents;
    };
    return sizeof(OffsetHelper);
  }

  static constexpr size_t contentsOffset() noexcept {
    // Offset of contents field accounting for GCObject base and alignment
    struct OffsetHelper {
      GCObject base;
      lu_byte extra;
      ls_byte shrlen;
      unsigned int hash;
      union { size_t lnglen; TString* hnext; } u;
    };
    return sizeof(OffsetHelper);
  }

  // Method declarations (implemented in lstring.cpp)
  unsigned hashLongStr();
  bool equals(TString* other);
  void remove(lua_State* L);           // Phase 25a: from luaS_remove
  TString* normalize(lua_State* L);    // Phase 25a: from luaS_normstr

  // Static factory-like functions (still use luaS_* for now)
  // static TString* create(lua_State* L, const char* str, size_t len);
};


/* Check if string is short (wrapper for backward compatibility) */
inline bool strisshr(const TString* ts) noexcept { return ts->isShort(); }

/* Check if string is external (fixed or with custom deallocator) */
inline bool isextstr(const TValue* v) noexcept {
	return ttislngstring(v) && tsvalue(v)->isExternal();
}

inline bool TValue::isExtString() const noexcept {
	return isLongString() && stringValue()->isExternal();
}

/*
** Get the actual string (array of bytes) from a 'TString'. (Generic
** version and specialized versions for long and short strings.)
*/
inline char* rawgetshrstr(TString* ts) noexcept {
	return ts->getContentsAddr();
}
inline const char* rawgetshrstr(const TString* ts) noexcept {
	return ts->getContentsAddr();
}

/*
** String accessor functions (Phase 46: converted from macros to inline functions)
** These provide type-safe access to string contents with assertions.
*/

/* Get short string contents (asserts string is short) */
inline char* getshrstr(TString* ts) noexcept {
	lua_assert(ts->isShort());
	return ts->getContentsAddr();
}
inline const char* getshrstr(const TString* ts) noexcept {
	lua_assert(ts->isShort());
	return ts->getContentsAddr();
}

/* Get long string contents (asserts string is long) */
inline char* getlngstr(TString* ts) noexcept {
	lua_assert(ts->isLong());
	return ts->getContentsField();
}
inline const char* getlngstr(const TString* ts) noexcept {
	lua_assert(ts->isLong());
	return ts->getContentsField();
}

/* Get string contents (works for both short and long strings) */
inline char* getstr(TString* ts) noexcept {
	return ts->getContentsPtr();
}
inline const char* getstr(const TString* ts) noexcept {
	return ts->c_str();
}


/* get string length from 'TString *ts' */
inline size_t tsslen(const TString* ts) noexcept {
	return ts->length();
}

/*
** Get string and length */
inline const char* getlstr(const TString* ts, size_t& len) noexcept {
	len = ts->length();
	return ts->c_str();
}

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
inline constexpr int LUA_VLIGHTUSERDATA = makevariant(LUA_TLIGHTUSERDATA, 0);

inline constexpr int LUA_VUSERDATA = makevariant(LUA_TUSERDATA, 0);

constexpr bool ttislightuserdata(const TValue* o) noexcept { return checktag(o, LUA_VLIGHTUSERDATA); }
constexpr bool ttisfulluserdata(const TValue* o) noexcept { return checktag(o, ctb(LUA_VUSERDATA)); }

constexpr bool TValue::isLightUserdata() const noexcept { return checktag(this, LUA_VLIGHTUSERDATA); }
constexpr bool TValue::isFullUserdata() const noexcept { return checktag(this, ctb(LUA_VUSERDATA)); }

inline void* pvalue(const TValue* o) noexcept { return o->pointerValue(); }

inline Udata* uvalue(const TValue* o) noexcept { return o->userdataValue(); }

constexpr void* pvalueraw(const Value& v) noexcept { return v.p; }

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
// Udata inherits from GCBase (CRTP)
class Udata : public GCBase<Udata> {
private:
  unsigned short nuvalue;  /* number of user values */
  size_t len;  /* number of bytes */
  struct Table *metatable;
  GCObject *gclist;
  UValue uv[1];  /* user values */

public:
  // Inline accessors
  size_t getLen() const noexcept { return len; }
  void setLen(size_t l) noexcept { len = l; }
  unsigned short getNumUserValues() const noexcept { return nuvalue; }
  void setNumUserValues(unsigned short n) noexcept { nuvalue = n; }
  Table* getMetatable() const noexcept { return metatable; }
  void setMetatable(Table* mt) noexcept { metatable = mt; }
  Table** getMetatablePtr() noexcept { return &metatable; }
  GCObject* getGclist() noexcept { return gclist; }
  void setGclist(GCObject* gc) noexcept { gclist = gc; }
  // For GC gray list traversal - allows efficient list manipulation
  GCObject** getGclistPtr() noexcept { return &gclist; }
  UValue* getUserValue(int idx) noexcept { return &uv[idx]; }
  const UValue* getUserValue(int idx) const noexcept { return &uv[idx]; }
  // Note: getMemory() uses function udatamemoffset which requires Udata0 to be defined
  inline void* getMemory() noexcept;
  inline const void* getMemory() const noexcept;

  // Static method to compute UV offset (needed for udatamemoffset function)
  static constexpr size_t uvOffset() noexcept { return offsetof(Udata, uv); }
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
// Udata0 inherits from GCBase (CRTP)
typedef struct Udata0 : public GCBase<Udata0> {
  unsigned short nuvalue;  /* number of user values */
  size_t len;  /* number of bytes */
  struct Table *metatable;
  union {LUAI_MAXALIGN;} bindata;
} Udata0;


/* compute the offset of the memory area of a userdata */
// Phase 49: Convert macro to constexpr function
// offsetof for non-standard-layout types (classes with GCBase inheritance)
// This triggers -Winvalid-offsetof but is safe because we control the memory layout
constexpr inline size_t udatamemoffset(int nuv) noexcept {
	return (nuv == 0) ? offsetof(Udata0, bindata)
	                  : Udata::uvOffset() + (sizeof(UValue) * nuv);
}

/* get the address of the memory block inside 'Udata' */
// Phase 49: Convert macro to inline function with const overload
inline char* getudatamem(Udata* u) noexcept {
	return cast_charp(u) + udatamemoffset(u->getNumUserValues());
}
inline const char* getudatamem(const Udata* u) noexcept {
	return cast_charp(u) + udatamemoffset(u->getNumUserValues());
}

/* compute the size of a userdata */
// Phase 49: Convert macro to constexpr function
constexpr inline size_t sizeudata(int nuv, size_t nb) noexcept {
	return udatamemoffset(nuv) + nb;
}

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
#define PF_ISVARARG	1
#define PF_FIXED	2  /* prototype has parts in fixed memory */


/*
** Function Prototypes
*/
// Proto inherits from GCBase (CRTP)
class Proto : public GCBase<Proto> {
private:
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

public:
  // Inline accessors
  lu_byte getNumParams() const noexcept { return numparams; }
  lu_byte getFlag() const noexcept { return flag; }
  lu_byte getMaxStackSize() const noexcept { return maxstacksize; }
  int getCodeSize() const noexcept { return sizecode; }
  int getConstantsSize() const noexcept { return sizek; }
  int getUpvaluesSize() const noexcept { return sizeupvalues; }
  int getProtosSize() const noexcept { return sizep; }
  int getLineInfoSize() const noexcept { return sizelineinfo; }
  int getLocVarsSize() const noexcept { return sizelocvars; }
  int getAbsLineInfoSize() const noexcept { return sizeabslineinfo; }
  int getLineDefined() const noexcept { return linedefined; }
  int getLastLineDefined() const noexcept { return lastlinedefined; }
  TString* getSource() const noexcept { return source; }
  bool isVarArg() const noexcept { return flag != 0; }
  Instruction* getCode() const noexcept { return code; }
  TValue* getConstants() const noexcept { return k; }
  Proto** getProtos() const noexcept { return p; }
  Upvaldesc* getUpvalues() const noexcept { return upvalues; }
  ls_byte* getLineInfo() const noexcept { return lineinfo; }
  AbsLineInfo* getAbsLineInfo() const noexcept { return abslineinfo; }
  LocVar* getLocVars() const noexcept { return locvars; }
  GCObject* getGclist() const noexcept { return gclist; }

  // Inline setters
  void setNumParams(lu_byte n) noexcept { numparams = n; }
  void setFlag(lu_byte f) noexcept { flag = f; }
  void setMaxStackSize(lu_byte s) noexcept { maxstacksize = s; }
  void setCodeSize(int s) noexcept { sizecode = s; }
  void setConstantsSize(int s) noexcept { sizek = s; }
  void setUpvaluesSize(int s) noexcept { sizeupvalues = s; }
  void setProtosSize(int s) noexcept { sizep = s; }
  void setLineInfoSize(int s) noexcept { sizelineinfo = s; }
  void setLocVarsSize(int s) noexcept { sizelocvars = s; }
  void setAbsLineInfoSize(int s) noexcept { sizeabslineinfo = s; }
  void setLineDefined(int l) noexcept { linedefined = l; }
  void setLastLineDefined(int l) noexcept { lastlinedefined = l; }
  void setSource(TString* s) noexcept { source = s; }
  void setCode(Instruction* c) noexcept { code = c; }
  void setConstants(TValue* constants) noexcept { k = constants; }
  void setProtos(Proto** protos) noexcept { p = protos; }
  void setUpvalues(Upvaldesc* uv) noexcept { upvalues = uv; }
  void setLineInfo(ls_byte* li) noexcept { lineinfo = li; }
  void setAbsLineInfo(AbsLineInfo* ali) noexcept { abslineinfo = ali; }
  void setLocVars(LocVar* lv) noexcept { locvars = lv; }
  void setGclist(GCObject* gc) noexcept { gclist = gc; }

  // Pointer accessors for serialization and GC
  TString** getSourcePtr() noexcept { return &source; }
  GCObject** getGclistPtr() noexcept { return &gclist; }

  // Reference accessors for luaM_growvector macro (both size and pointer)
  int& getCodeSizeRef() noexcept { return sizecode; }
  int& getConstantsSizeRef() noexcept { return sizek; }
  int& getUpvaluesSizeRef() noexcept { return sizeupvalues; }
  int& getProtosSizeRef() noexcept { return sizep; }
  int& getLineInfoSizeRef() noexcept { return sizelineinfo; }
  int& getLocVarsSizeRef() noexcept { return sizelocvars; }
  int& getAbsLineInfoSizeRef() noexcept { return sizeabslineinfo; }

  Instruction*& getCodeRef() noexcept { return code; }
  TValue*& getConstantsRef() noexcept { return k; }
  Proto**& getProtosRef() noexcept { return p; }
  Upvaldesc*& getUpvaluesRef() noexcept { return upvalues; }
  ls_byte*& getLineInfoRef() noexcept { return lineinfo; }
  AbsLineInfo*& getAbsLineInfoRef() noexcept { return abslineinfo; }
  LocVar*& getLocVarsRef() noexcept { return locvars; }

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

#define isLfunction(o)	ttisLclosure(o)

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
      struct UpVal *next;  /* linked list */
      struct UpVal **previous;
    } open;
    TValue value;  /* the value (when closed) */
  } u;

public:
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
    return cast(StkId, v.p);
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
  struct Proto *p;
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


#define getproto(o)	(clLvalue(o)->getProto())

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
    return isKeyCollectable() ? u.key_val.gc : NULL;
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
  lu_byte flags;  /* 1<<p means tagmethod(p) is not present */
  lu_byte lsizenode;  /* log2 of number of slots of 'node' array */
  unsigned int asize;  /* number of slots in 'array' array */
  Value *array;  /* array part */
  Node *node;
  Table *metatable;
  GCObject *gclist;

public:
  // Inline accessors
  lu_byte getFlags() const noexcept { return flags; }
  void setFlags(lu_byte f) noexcept { flags = f; }

  // Flags field bit manipulation methods
  void setFlagBits(int mask) noexcept { flags |= cast_byte(mask); }
  void clearFlagBits(int mask) noexcept { flags &= cast_byte(~mask); }

  // Flags field reference accessor (for backward compatibility)
  lu_byte& getFlagsRef() noexcept { return flags; }

  lu_byte getLsizenode() const noexcept { return lsizenode; }
  void setLsizenode(lu_byte ls) noexcept { lsizenode = ls; }

  unsigned int arraySize() const noexcept { return asize; }
  void setArraySize(unsigned int sz) noexcept { asize = sz; }

  Value* getArray() noexcept { return array; }
  const Value* getArray() const noexcept { return array; }
  void setArray(Value* arr) noexcept { array = arr; }

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
    return cast(unsigned*, array);
  }

  inline const unsigned int* getLenHint() const noexcept {
    return cast(const unsigned*, array);
  }

  inline lu_byte* getArrayTag(lua_Unsigned k) noexcept {
    return cast(lu_byte*, array) + sizeof(unsigned) + k;
  }

  inline const lu_byte* getArrayTag(lua_Unsigned k) const noexcept {
    return cast(const lu_byte*, array) + sizeof(unsigned) + k;
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

  // Phase 33: Constructor/destructor/factory pattern
  Table();  // Constructor for initialization
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
#define lmod(s,size) \
	(check_exp((size&(size-1))==0, (cast_uint(s) & cast_uint((size)-1))))


/* Phase 44.1: twoto now Table::powerOfTwo(x) static method */
/* Phase 44.1: sizenode now Table::nodeSize() method */


/* size of buffer for 'luaO_utf8esc' function */
inline constexpr int UTF8BUFFSZ = 8;


/* macro to call 'luaO_pushvfstring' correctly */
#define pushvfstring(L, argp, fmt, msg)	\
  { va_start(argp, fmt); \
  msg = luaO_pushvfstring(L, fmt, argp); \
  va_end(argp); \
  if (msg == NULL) (L)->doThrow(LUA_ERRMEM);  /* only after 'va_end' */ }


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

