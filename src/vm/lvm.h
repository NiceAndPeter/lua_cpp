/*
** $Id: lvm.h $
** Lua virtual machine
** See Copyright Notice in lua.h
*/

#ifndef lvm_h
#define lvm_h


#include <cfloat>

#include "ldo.h"
#include "lgc.h"
#include "lobject.h"
#include "ltm.h"
#include "ltable.h"


inline constexpr bool cvt2str(const TValue* o) noexcept {
#if !defined(LUA_NOCVTN2S)
	return ttisnumber(o);
#else
	(void)o;  /* suppress unused parameter warning */
	return false;  /* no conversion from numbers to strings */
#endif
}


inline constexpr bool cvt2num(const TValue* o) noexcept {
#if !defined(LUA_NOCVTS2N)
	return ttisstring(o);
#else
	(void)o;  /* suppress unused parameter warning */
	return false;  /* no conversion from strings to numbers */
#endif
}


/*
** You can define LUA_FLOORN2I if you want to convert floats to integers
** by flooring them (instead of raising an error if they are not
** integral values)
*/
#if !defined(LUA_FLOORN2I)
#define LUA_FLOORN2I		F2Imod::F2Ieq
#endif


/*
** Rounding modes for float->integer coercion
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
** 'l_intfitsf' checks whether a given integer is in the range that
** can be converted to a float without rounding. Used in comparisons.
*/

/* number of bits in the mantissa of a float (kept as macro for preprocessor #if) */
#define NBM		(l_floatatt(MANT_DIG))

/*
** Check whether some integers may not fit in a float, testing whether
** (maxinteger >> NBM) > 0. (That implies (1 << NBM) <= maxinteger.)
** (The shifts are done in parts, to avoid shifting by more than the size
** of an integer. In a worst case, NBM == 113 for long double and
** sizeof(long) == 32.)
*/
#if ((((LUA_MAXINTEGER >> (NBM / 4)) >> (NBM / 4)) >> (NBM / 4)) \
	>> (NBM - (3 * (NBM / 4))))  >  0

/* limit for integers that fit in a float */
inline constexpr lua_Unsigned MAXINTFITSF = (static_cast<lua_Unsigned>(1) << NBM);

/* check whether 'i' is in the interval [-MAXINTFITSF, MAXINTFITSF] */
inline constexpr bool l_intfitsf(lua_Integer i) noexcept {
	return (MAXINTFITSF + l_castS2U(i)) <= (2 * MAXINTFITSF);
}

#else  /* all integers fit in a float precisely */

inline constexpr bool l_intfitsf(lua_Integer i) noexcept {
	(void)i;  /* suppress unused parameter warning */
	return true;
}

#endif


/* Forward declarations for conversion functions (defined in lvm_conversion.cpp) */
LUAI_FUNC int luaV_tonumber_ (const TValue *obj, lua_Number *n);
LUAI_FUNC int luaV_tointeger (const TValue *obj, lua_Integer *p, F2Imod mode);
LUAI_FUNC int luaV_tointegerns (const TValue *obj, lua_Integer *p, F2Imod mode);


/* convert an object to a float (including string coercion) */
inline bool tonumber(const TValue* o, lua_Number* n) noexcept {
	if (ttisfloat(o)) {
		*n = fltvalue(o);
		return true;
	}
	return luaV_tonumber_(o, n);
}


/* convert an object to a float (without string coercion) */
inline bool tonumberns(const TValue* o, lua_Number& n) noexcept {
	if (ttisfloat(o)) {
		n = fltvalue(o);
		return true;
	}
	if (ttisinteger(o)) {
		n = cast_num(ivalue(o));
		return true;
	}
	return false;
}


/* convert an object to an integer (including string coercion) */
inline bool tointeger(const TValue* o, lua_Integer* i) noexcept {
	if (l_likely(ttisinteger(o))) {
		*i = ivalue(o);
		return true;
	}
	return luaV_tointeger(o, i, LUA_FLOORN2I);
}


/* convert an object to an integer (without string coercion) */
inline bool tointegerns(const TValue* o, lua_Integer* i) noexcept {
	if (l_likely(ttisinteger(o))) {
		*i = ivalue(o);
		return true;
	}
	return luaV_tointegerns(o, i, LUA_FLOORN2I);
}


/* Note: intop cannot be a function template because 'op' is an operator, not a value.
   This must remain a macro to support operator token pasting. */
#define intop(op,v1,v2) l_castU2S(l_castS2U(v1) op l_castS2U(v2))

/* Forward declaration for luaV_equalobj (defined in lvm.cpp) */
[[nodiscard]] LUAI_FUNC int luaV_equalobj (lua_State *L, const TValue *t1, const TValue *t2);

inline int luaV_rawequalobj(const TValue* t1, const TValue* t2) noexcept {
	return *t1 == *t2;  /* Use operator== for raw equality */
}


/*
** fast track for 'gettable'
*/
template<typename F>
inline lu_byte luaV_fastget(const TValue* t, const TValue* k, TValue* res, F&& f) noexcept {
	if (!ttistable(t))
		return LUA_VNOTABLE;
	return f(hvalue(t), k, res);
}

/* Overload for TString* keys */
template<typename F>
inline lu_byte luaV_fastget(const TValue* t, TString* k, TValue* res, F&& f) noexcept {
	if (!ttistable(t))
		return LUA_VNOTABLE;
	return f(hvalue(t), k, res);
}


/*
** Special case of 'luaV_fastget' for integers, inlining the fast case
** of 'luaH_getint'.
*/
inline void luaV_fastgeti(const TValue* t, lua_Integer k, TValue* res, lu_byte& tag) noexcept {
	if (!ttistable(t))
		tag = LUA_VNOTABLE;
	else
		luaH_fastgeti(hvalue(t), k, res, tag);
}


template<typename F>
inline int luaV_fastset(const TValue* t, const TValue* k, TValue* val, F&& f) noexcept {
	if (!ttistable(t))
		return HNOTATABLE;
	return f(hvalue(t), k, val);
}

/* Overload for TString* keys */
template<typename F>
inline int luaV_fastset(const TValue* t, TString* k, TValue* val, F&& f) noexcept {
	if (!ttistable(t))
		return HNOTATABLE;
	return f(hvalue(t), k, val);
}

inline void luaV_fastseti(const TValue* t, lua_Integer k, TValue* val, int& hres) noexcept {
	if (!ttistable(t))
		hres = HNOTATABLE;
	else
		luaH_fastseti(hvalue(t), k, val, hres);
}


/*
** Finish a fast set operation (when fast set succeeds).
*/
inline void luaV_finishfastset(lua_State* L, const TValue* t, const TValue* v) noexcept {
	luaC_barrierback(L, gcvalue(t), v);
}


/*
** Shift right is the same as shift left with a negative 'y'
*/
/* Forward declaration for luaV_shiftl (full declaration below) */
[[nodiscard]] LUAI_FUNC lua_Integer luaV_shiftl (lua_Integer x, lua_Integer y);

inline lua_Integer luaV_shiftr(lua_Integer x, lua_Integer y) noexcept {
	return luaV_shiftl(x, intop(-, 0, y));
}



[[nodiscard]] LUAI_FUNC int luaV_lessthan (lua_State *L, const TValue *l, const TValue *r);
[[nodiscard]] LUAI_FUNC int luaV_lessequal (lua_State *L, const TValue *l, const TValue *r);
#ifndef luaV_flttointeger_declared
#define luaV_flttointeger_declared
LUAI_FUNC int luaV_flttointeger (lua_Number n, lua_Integer *p, F2Imod mode);
#endif
LUAI_FUNC lu_byte luaV_finishget (lua_State *L, const TValue *t, TValue *key,
                                                StkId val, lu_byte tag);
LUAI_FUNC void luaV_finishset (lua_State *L, const TValue *t, TValue *key,
                                             TValue *val, int aux);
LUAI_FUNC void luaV_finishOp (lua_State *L);
LUAI_FUNC void luaV_execute (lua_State *L, CallInfo *ci);
LUAI_FUNC void luaV_concat (lua_State *L, int total);
[[nodiscard]] LUAI_FUNC lua_Integer luaV_idiv (lua_State *L, lua_Integer x, lua_Integer y);
[[nodiscard]] LUAI_FUNC lua_Integer luaV_mod (lua_State *L, lua_Integer x, lua_Integer y);
[[nodiscard]] LUAI_FUNC lua_Number luaV_modf (lua_State *L, lua_Number x, lua_Number y);
LUAI_FUNC void luaV_objlen (lua_State *L, StkId ra, const TValue *rb);

#endif
