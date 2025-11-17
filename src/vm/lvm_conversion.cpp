/*
** $Id: lvm_conversion.c $
** Type conversion operations for Lua VM
** See Copyright Notice in lua.h
*/

#define lvm_c
#define LUA_CORE

#include "lprefix.h"

#include <cmath>

#include "lua.h"

#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "lvm.h"


/*
** Try to convert a value from string to a number value.
** If the value is not a string or is a string not representing
** a valid numeral (or if coercions from strings to numbers
** are disabled via macro 'cvt2num'), do not modify 'result'
** and return 0.
*/
static int l_strton (const TValue *obj, TValue *result) {
  lua_assert(obj != result);
  if (!cvt2num(obj))  /* is object not a string? */
    return 0;
  else {
    TString *st = tsvalue(obj);
    size_t stlen;
    const char *s = getlstr(st, stlen);
    return (luaO_str2num(s, result) == stlen + 1);
  }
}


/*
** Try to convert a value to a float. The float case is already handled
** by the macro 'tonumber'.
*/
int luaV_tonumber_ (const TValue *obj, lua_Number *n) {
  TValue v;
  if (ttisinteger(obj)) {
    *n = cast_num(ivalue(obj));
    return 1;
  }
  else if (l_strton(obj, &v)) {  /* string coercible to number? */
    *n = nvalue(&v);  /* convert result of 'luaO_str2num' to a float */
    return 1;
  }
  else
    return 0;  /* conversion failed */
}


/*
** try to convert a float to an integer, rounding according to 'mode'.
*/
int luaV_flttointeger (lua_Number n, lua_Integer *p, F2Imod mode) {
  lua_Number f = l_floor(n);
  if (n != f) {  /* not an integral value? */
    if (mode == F2Imod::F2Ieq) return 0;  /* fails if mode demands integral value */
    else if (mode == F2Imod::F2Iceil)  /* needs ceiling? */
      f += 1;  /* convert floor to ceiling (remember: n != f) */
  }
  return lua_numbertointeger(f, p);
}


/*
** try to convert a value to an integer, rounding according to 'mode',
** without string coercion.
** ("Fast track" handled by macro 'tointegerns'.)
*/
int luaV_tointegerns (const TValue *obj, lua_Integer *p, F2Imod mode) {
  if (ttisfloat(obj))
    return luaV_flttointeger(fltvalue(obj), p, mode);
  else if (ttisinteger(obj)) {
    *p = ivalue(obj);
    return 1;
  }
  else
    return 0;
}


/*
** try to convert a value to an integer.
*/
int luaV_tointeger (const TValue *obj, lua_Integer *p, F2Imod mode) {
  TValue v;
  if (l_strton(obj, &v))  /* does 'obj' point to a numerical string? */
    obj = &v;  /* change it to point to its corresponding number */
  return luaV_tointegerns(obj, p, mode);
}


/*
** TValue conversion methods (wrappers for compatibility)
*/
int TValue::toNumber(lua_Number* n) const {
  return luaV_tonumber_(this, n);
}

int TValue::toInteger(lua_Integer* p, F2Imod mode) const {
  return luaV_tointeger(this, p, mode);
}

int TValue::toIntegerNoString(lua_Integer* p, F2Imod mode) const {
  return luaV_tointegerns(this, p, mode);
}
