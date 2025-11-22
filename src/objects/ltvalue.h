/*
** $Id: ltvalue.h $
** Tagged Values (TValue class)
** See Copyright Notice in lua.h
*/

#ifndef ltvalue_h
#define ltvalue_h

#include "llimits.h"
#include "lua.h"


/*
** tags for Tagged Values have the following use of bits:
** bits 0-3: actual tag (a LUA_T* constant)
** bits 4-5: variant bits
** bit 6: whether value is collectable
*/

/* add variant bits to a type */
constexpr int makevariant(int t, int v) noexcept { return (t | (v << 4)); }


/*
** Extra types for collectable non-values
*/
inline constexpr int LUA_TUPVAL = LUA_NUMTYPES;      /* upvalues */
inline constexpr int LUA_TPROTO = (LUA_NUMTYPES+1);  /* function prototypes */


/*
** {==================================================================
** Variant tags for all Lua types
** ===================================================================
*/

enum class LuaT : lu_byte {
  /* Nil variants */
  NIL = makevariant(LUA_TNIL, 0),
  EMPTY = makevariant(LUA_TNIL, 1),
  ABSTKEY = makevariant(LUA_TNIL, 2),
  NOTABLE = makevariant(LUA_TNIL, 3),

  /* Boolean variants */
  VFALSE = makevariant(LUA_TBOOLEAN, 0),
  VTRUE = makevariant(LUA_TBOOLEAN, 1),

  /* Number variants */
  NUMINT = makevariant(LUA_TNUMBER, 0),  /* integer numbers */
  NUMFLT = makevariant(LUA_TNUMBER, 1),  /* float numbers */

  /* String variants */
  SHRSTR = makevariant(LUA_TSTRING, 0),  /* short strings */
  LNGSTR = makevariant(LUA_TSTRING, 1),  /* long strings */

  /* Table variant */
  TABLE = makevariant(LUA_TTABLE, 0),

  /* Function variants */
  LCL = makevariant(LUA_TFUNCTION, 0),  /* Lua closure */
  LCF = makevariant(LUA_TFUNCTION, 1),  /* light C function */
  CCL = makevariant(LUA_TFUNCTION, 2),  /* C closure */

  /* Userdata variants */
  LIGHTUSERDATA = makevariant(LUA_TLIGHTUSERDATA, 0),
  USERDATA = makevariant(LUA_TUSERDATA, 0),

  /* Thread variant */
  THREAD = makevariant(LUA_TTHREAD, 0),

  /* Upvalue variant (collectable non-value) */
  UPVAL = makevariant(LUA_TUPVAL, 0),

  /* Proto variant (collectable non-value) */
  PROTO = makevariant(LUA_TPROTO, 0)
};

/* }================================================================== */


/*
** Rounding modes for float->integer coercion (needed by TValue conversion methods)
*/
#ifndef F2Imod_defined
#define F2Imod_defined
enum class F2Imod {
  F2Ieq,     /* no rounding; accepts only integral values */
  F2Ifloor,  /* takes the floor of the number */
  F2Iceil    /* takes the ceiling of the number */
};
#endif


/*
** Union of all Lua values
*/
typedef union Value {
  GCObject *gc;    /* collectable objects */
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
struct lua_State;
class GCObject;


/*
** Tagged Values. This is the basic representation of values in Lua:
** an actual value plus a tag with its type.
*/
class TValue {
private:
  Value value_;
  LuaT tt_;

public:
  // Constexpr constructor for static initialization
  constexpr TValue(Value v, LuaT t) noexcept : value_(v), tt_(t) {}
  constexpr TValue(Value v, lu_byte t) noexcept : value_(v), tt_(static_cast<LuaT>(t)) {}  /* for compatibility */

  // Default constructor
  TValue() = default;

  // Inline accessors for hot-path access
  LuaT getType() const noexcept { return tt_; }
  lu_byte getRawType() const noexcept { return static_cast<lu_byte>(tt_); }  /* for legacy code */
  const Value& getValue() const noexcept { return value_; }
  Value& getValue() noexcept { return value_; }

  // Value accessors
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

  // Type-specific value accessors (return pointers from GC union)
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

  // Setter methods (HOT PATH - performance critical!)
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

  // Conversion methods (formerly luaV_tonumber_, luaV_tointeger, luaV_tointegerns)
  // Return 1 on success, 0 on failure
  int toNumber(lua_Number* n) const;
  int toInteger(lua_Integer* p, F2Imod mode) const;
  int toIntegerNoString(lua_Integer* p, F2Imod mode) const;

  // Copy from another TValue
  void copy(const TValue* other) noexcept {
    value_ = other->getValue();
    tt_ = other->getType();
  }

  // Low-level field access (for macros during transition)
  Value& valueField() noexcept { return value_; }
  const Value& valueField() const noexcept { return value_; }
  void setType(LuaT t) noexcept { tt_ = t; }
  void setType(lu_byte t) noexcept { tt_ = static_cast<LuaT>(t); }  /* for legacy code */

  // Type checking methods (implementations below after constants are defined)
  // Nil checks
  constexpr bool isNil() const noexcept;
  constexpr bool isStrictNil() const noexcept;
  constexpr bool isAbstKey() const noexcept;
  constexpr bool isNonStrictNil() const noexcept;
  constexpr bool isEmpty() const noexcept;

  // Boolean checks
  constexpr bool isBoolean() const noexcept;
  constexpr bool isFalse() const noexcept;
  constexpr bool isTrue() const noexcept;
  constexpr bool isFalseLike() const noexcept; // nil or false

  // Number checks
  constexpr bool isNumber() const noexcept;
  constexpr bool isInteger() const noexcept;
  constexpr bool isFloat() const noexcept;

  // String checks
  constexpr bool isString() const noexcept;
  constexpr bool isShortString() const noexcept;
  constexpr bool isLongString() const noexcept;
  bool isExtString() const noexcept;

  // Userdata checks
  constexpr bool isLightUserdata() const noexcept;
  constexpr bool isFullUserdata() const noexcept;

  // Thread check
  constexpr bool isThread() const noexcept;

  // Function checks
  constexpr bool isFunction() const noexcept;
  constexpr bool isLClosure() const noexcept;
  constexpr bool isLightCFunction() const noexcept;
  constexpr bool isCClosure() const noexcept;
  constexpr bool isClosure() const noexcept;
  constexpr bool isLuaFunction() const noexcept;

  // Table check
  constexpr bool isTable() const noexcept;

  // GC checks
  constexpr bool isCollectable() const noexcept;
  bool hasRightType() const noexcept; // GC object has same tag as value

  // Low-level type accessors
  constexpr LuaT rawType() const noexcept { return tt_; }
  constexpr int baseType() const noexcept;
  constexpr LuaT typeTag() const noexcept;

  // Operator overloads (for numeric comparisons - defined after dependencies)
  // These are declared here but implemented in lobject.h after all type helpers are available
  friend bool operator<(const TValue& l, const TValue& r) noexcept;
  friend bool operator<=(const TValue& l, const TValue& r) noexcept;
  friend bool operator==(const TValue& l, const TValue& r) noexcept;
  friend bool operator!=(const TValue& l, const TValue& r) noexcept;

  // Assignment operator (simple copy, no GC barriers)
  // For assignments that require GC barriers, use setobj2t/setobj2n
  // Implementation in lgc.h after all dependencies
  TValue& operator=(const TValue& other) noexcept;
};


/* Access to TValue's internal value union */
constexpr Value& val_(TValue* o) noexcept { return o->valueField(); }
constexpr const Value& val_(const TValue* o) noexcept { return o->valueField(); }
constexpr Value& valraw(TValue* o) noexcept { return val_(o); }
constexpr const Value& valraw(const TValue* o) noexcept { return val_(o); }


/* raw type tag of a TValue */
constexpr LuaT rawtt(const TValue* o) noexcept { return o->getType(); }
constexpr lu_byte rawtt_byte(const TValue* o) noexcept { return o->getRawType(); }  /* for legacy code */

/* tag with no variants (bits 0-3) */
constexpr int novariant(int t) noexcept { return (t & 0x0F); }
constexpr int novariant(LuaT t) noexcept { return (static_cast<int>(t) & 0x0F); }

/* type tag of a TValue (bits 0-3 for tags + variant bits 4-5) */
constexpr int withvariant(int t) noexcept { return (t & 0x3F); }
constexpr LuaT withvariant(LuaT t) noexcept { return static_cast<LuaT>(static_cast<int>(t) & 0x3F); }

constexpr LuaT ttypetag(const TValue* o) noexcept { return withvariant(rawtt(o)); }
constexpr lu_byte ttypetag_byte(const TValue* o) noexcept { return static_cast<lu_byte>(withvariant(rawtt(o))); }  /* for legacy code */

/* type of a TValue */
constexpr int ttype(const TValue* o) noexcept { return novariant(rawtt(o)); }

// TValue low-level type accessor implementations
constexpr int TValue::baseType() const noexcept { return novariant(tt_); }
constexpr LuaT TValue::typeTag() const noexcept { return withvariant(tt_); }

/* Macros to test type */
constexpr bool checktag(const TValue* o, LuaT t) noexcept { return rawtt(o) == t; }
constexpr bool checktag(const TValue* o, lu_byte t) noexcept { return rawtt(o) == static_cast<LuaT>(t); }  /* overload for raw bytes */
constexpr bool checktype(const TValue* o, int t) noexcept { return ttype(o) == t; }

/* Bit mark for collectable types */
inline constexpr int BIT_ISCOLLECTABLE = (1 << 6);

/* mark a tag as collectable */
constexpr LuaT ctb(LuaT t) noexcept { return static_cast<LuaT>(static_cast<int>(t) | BIT_ISCOLLECTABLE); }
constexpr lu_byte ctb(int t) noexcept { return static_cast<lu_byte>(t | BIT_ISCOLLECTABLE); }  /* overload for base types */


/* Macros for internal tests */

/* collectable object has the same tag as the original value */
/* NOTE: righttt() defined as inline function after gcvalue() below */

/* Macros to set values */

/* set a value's tag */
inline void settt_(TValue* o, LuaT t) noexcept { o->setType(t); }
inline void settt_(TValue* o, lu_byte t) noexcept { o->setType(t); }  /* overload for raw bytes */


#endif
