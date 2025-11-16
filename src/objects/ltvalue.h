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
struct GCObject;


/*
** Tagged Values. This is the basic representation of values in Lua:
** an actual value plus a tag with its type.
*/
class TValue {
private:
  Value value_;
  lu_byte tt_;

public:
  // Constexpr constructor for static initialization
  constexpr TValue(Value v, lu_byte t) noexcept : value_(v), tt_(t) {}

  // Default constructor
  TValue() = default;

  // Inline accessors for hot-path access
  lu_byte getType() const noexcept { return tt_; }
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

  // Copy from another TValue
  void copy(const TValue* other) noexcept {
    value_ = other->getValue();
    tt_ = other->getType();
  }

  // Low-level field access (for macros during transition)
  Value& valueField() noexcept { return value_; }
  const Value& valueField() const noexcept { return value_; }
  void setType(lu_byte t) noexcept { tt_ = t; }

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
  constexpr lu_byte rawType() const noexcept { return tt_; }
  constexpr int baseType() const noexcept;
  constexpr lu_byte typeTag() const noexcept;

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
constexpr lu_byte rawtt(const TValue* o) noexcept { return o->getType(); }

/* tag with no variants (bits 0-3) */
constexpr int novariant(int t) noexcept { return (t & 0x0F); }

/* type tag of a TValue (bits 0-3 for tags + variant bits 4-5) */
constexpr int withvariant(int t) noexcept { return (t & 0x3F); }

constexpr lu_byte ttypetag(const TValue* o) noexcept { return cast_byte(withvariant(rawtt(o))); }

/* type of a TValue */
constexpr int ttype(const TValue* o) noexcept { return novariant(rawtt(o)); }

// TValue low-level type accessor implementations
constexpr int TValue::baseType() const noexcept { return novariant(tt_); }
constexpr lu_byte TValue::typeTag() const noexcept { return cast_byte(withvariant(tt_)); }

/* Macros to test type */
constexpr bool checktag(const TValue* o, int t) noexcept { return rawtt(o) == t; }
constexpr bool checktype(const TValue* o, int t) noexcept { return ttype(o) == t; }

/* Bit mark for collectable types */
inline constexpr int BIT_ISCOLLECTABLE = (1 << 6);

/* mark a tag as collectable */
constexpr lu_byte ctb(int t) noexcept { return static_cast<lu_byte>(t | BIT_ISCOLLECTABLE); }


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
inline void settt_(TValue* o, lu_byte t) noexcept { o->setType(t); }


#endif
