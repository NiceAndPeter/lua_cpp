/*
** $Id: lobject_core.h $
** Core GC object types and TValue helpers
** See Copyright Notice in lua.h
*/


#ifndef lobject_core_h
#define lobject_core_h


#include <cstdarg>

#include "llimits.h"
#include "lua.h"
#include "ltvalue.h"  /* TValue class */

/* Forward declarations */
enum class GCAge : lu_byte;
class Table;
class lua_State;

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
** Collectable Objects
** ===================================================================
*/

/*
** CommonHeader macro deprecated - GC objects now inherit from GCBase
*/


/* Common type for all collectable objects */
class GCObject {
protected:
  mutable GCObject* next;     /* GC list linkage (mutable for GC bookkeeping) */
  lu_byte tt;                  /* Type tag (immutable) */
  mutable lu_byte marked;      /* GC mark bits (mutable for GC bookkeeping) */

public:
  // Inline accessors
  GCObject* getNext() const noexcept { return next; }
  void setNext(GCObject* n) const noexcept { next = n; }  /* const - next is mutable */
  // Pointer-to-pointer for efficient GC list manipulation (allows in-place removal)
  GCObject** getNextPtr() const noexcept { return &next; }  /* const - next is mutable */
  lu_byte getType() const noexcept { return tt; }
  void setType(lu_byte t) noexcept { tt = t; }
  lu_byte getMarked() const noexcept { return marked; }
  void setMarked(lu_byte m) const noexcept { marked = m; }  /* const - marked is mutable */
  bool isMarked() const noexcept { return marked != 0; }

  // Marked field bit manipulation methods (const - marked is mutable)
  void setMarkedBit(int bit) const noexcept {
    lua_assert(bit >= 0 && bit < 8);  /* lu_byte is 8 bits */
    marked |= cast_byte(1 << bit);
  }
  void clearMarkedBit(int bit) const noexcept {
    lua_assert(bit >= 0 && bit < 8);  /* lu_byte is 8 bits */
    marked &= cast_byte(~(1 << bit));
  }
  void clearMarkedBits(int mask) const noexcept { marked &= cast_byte(~mask); }

  // Marked field bit manipulation helpers (for backward compatibility)
  lu_byte& getMarkedRef() const noexcept { return marked; }  /* const - marked is mutable */

  // GC color and age methods (defined in lgc.h after constants are available)
  inline bool isWhite() const noexcept;
  inline bool isBlack() const noexcept;
  inline bool isGray() const noexcept;
  inline GCAge getAge() const noexcept;
  inline void setAge(GCAge age) const noexcept;  /* const - marked is mutable */
  inline bool isOld() const noexcept;

  // GC operations (implemented in lgc.cpp)
  void fix(lua_State* L) const;  /* const - only modifies mutable GC fields */
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
    constexpr void setNext(GCObject* n) const noexcept { next = n; }  /* const - next is mutable */

    constexpr lu_byte getType() const noexcept { return tt; }
    constexpr void setType(lu_byte t) noexcept { tt = t; }

    constexpr lu_byte getMarked() const noexcept { return marked; }
    constexpr void setMarked(lu_byte m) const noexcept { marked = m; }  /* const - marked is mutable */

    constexpr bool isMarked() const noexcept { return marked != 0; }

    // GC color and age methods (defined in lgc.h after constants)
    inline void setAge(GCAge age) const noexcept;  /* const - marked is mutable */
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
  Table *metatable;
  GCObject *gclist;
  UValue uv[1];  /* user values */

public:
  // Phase 50: Constructor - initializes all fields to safe defaults
  Udata() noexcept {
    nuvalue = 0;
    len = 0;
    metatable = nullptr;
    gclist = nullptr;
    // Note: uv array will be initialized by caller if needed
  }

  // Phase 50: Destructor - trivial (GC handles deallocation)
  // MUST be empty (not = default) for variable-size objects
  ~Udata() noexcept {}

  // Phase 50: Special placement new for variable-size objects
  static void* operator new(size_t /*size*/, void* ptr) noexcept {
    return ptr;  // Just return the pointer, no allocation
  }

  // Phase 50: Placement new operator - integrates with Lua's GC (implemented in lgc.h)
  static void* operator new(size_t size, lua_State* L, lu_byte tt, size_t extra = 0);

  // Disable regular new/delete (must use placement new with GC)
  static void* operator new(size_t) = delete;
  static void operator delete(void*) = delete;

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
  Table *metatable;
  union {LUAI_MAXALIGN;} bindata;
} Udata0;


/* compute the offset of the memory area of a userdata */
// Phase 49: Convert macro to constexpr function
// offsetof for non-standard-layout types (classes with GCBase inheritance)
// This triggers -Winvalid-offsetof but is safe because we control the memory layout
constexpr inline size_t udatamemoffset(int nuv) noexcept {
	return (nuv == 0) ? offsetof(Udata0, bindata)
	                  : Udata::uvOffset() + (sizeof(UValue) * static_cast<size_t>(nuv));
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
  return getudatamem(this);  // Calls const overload
}

/* }================================================================== */


#endif
