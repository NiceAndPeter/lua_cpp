/*
** $Id: lvm_comparison.c $
** Comparison operations for Lua VM
** See Copyright Notice in lua.h
*/

#define lvm_c
#define LUA_CORE

#include "lprefix.h"

#include <cstring>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"


/*
** Compare two strings 'ts1' x 'ts2', returning an integer less-equal-
** -greater than zero if 'ts1' is less-equal-greater than 'ts2'.
** The code is a little tricky because it allows '\0' in the strings
** and it uses 'strcoll' (to respect locales) for each segment
** of the strings. Note that segments can compare equal but still
** have different lengths.
*/
[[nodiscard]] int l_strcmp (const TString *ts1, const TString *ts2) {
  size_t rl1;  /* real length */
  const char *s1 = getlstr(ts1, rl1);
  size_t rl2;
  const char *s2 = getlstr(ts2, rl2);
  for (;;) {  /* for each segment */
    int temp = strcoll(s1, s2);
    if (temp != 0)  /* not equal? */
      return temp;  /* done */
    else {  /* strings are equal up to a '\0' */
      size_t zl1 = strlen(s1);  /* index of first '\0' in 's1' */
      size_t zl2 = strlen(s2);  /* index of first '\0' in 's2' */
      if (zl2 == rl2)  /* 's2' is finished? */
        return (zl1 == rl1) ? 0 : 1;  /* check 's1' */
      else if (zl1 == rl1)  /* 's1' is finished? */
        return -1;  /* 's1' is less than 's2' ('s2' is not finished) */
      /* both strings longer than 'zl'; go on comparing after the '\0' */
      zl1++; zl2++;
      s1 += zl1; rl1 -= zl1; s2 += zl2; rl2 -= zl2;
    }
  }
}


/*
** Check whether integer 'i' is less than float 'f'. If 'i' has an
** exact representation as a float ('l_intfitsf'), compare numbers as
** floats. Otherwise, use the equivalence 'i < f <=> i < ceil(f)'.
** If 'ceil(f)' is out of integer range, either 'f' is greater than
** all integers or less than all integers.
** (The test with 'l_intfitsf' is only for performance; the else
** case is correct for all values, but it is slow due to the conversion
** from float to int.)
** When 'f' is NaN, comparisons must result in false.
**
** DESIGN RATIONALE: Lua supports both integer and float types, requiring
** careful mixed-type comparisons. Direct float conversion can lose precision
** for large integers (> 2^53 on typical platforms). Using ceiling/floor
** functions and integer comparison preserves exact semantics.
**
** Example: For a 64-bit integer 2^60, comparing as floats would round it,
** potentially giving incorrect results. Instead, we compute ceil(f) as an
** integer and compare in the integer domain where no precision is lost.
*/
[[nodiscard]] int LTintfloat (lua_Integer i, lua_Number f) {
  if (l_intfitsf(i))
    return luai_numlt(cast_num(i), f);  /* compare them as floats */
  else {  /* i < f <=> i < ceil(f) */
    lua_Integer fi;
    if (luaV_flttointeger(f, &fi, F2Imod::F2Iceil))  /* fi = ceil(f) */
      return i < fi;   /* compare them as integers */
    else  /* 'f' is either greater or less than all integers */
      return f > 0;  /* greater? */
  }
}


/*
** Check whether integer 'i' is less than or equal to float 'f'.
** See comments on previous function.
*/
[[nodiscard]] int LEintfloat (lua_Integer i, lua_Number f) {
  if (l_intfitsf(i))
    return luai_numle(cast_num(i), f);  /* compare them as floats */
  else {  /* i <= f <=> i <= floor(f) */
    lua_Integer fi;
    if (luaV_flttointeger(f, &fi, F2Imod::F2Ifloor))  /* fi = floor(f) */
      return i <= fi;   /* compare them as integers */
    else  /* 'f' is either greater or less than all integers */
      return f > 0;  /* greater? */
  }
}


/*
** Check whether float 'f' is less than integer 'i'.
** See comments on previous function.
*/
[[nodiscard]] int LTfloatint (lua_Number f, lua_Integer i) {
  if (l_intfitsf(i))
    return luai_numlt(f, cast_num(i));  /* compare them as floats */
  else {  /* f < i <=> floor(f) < i */
    lua_Integer fi;
    if (luaV_flttointeger(f, &fi, F2Imod::F2Ifloor))  /* fi = floor(f) */
      return fi < i;   /* compare them as integers */
    else  /* 'f' is either greater or less than all integers */
      return f < 0;  /* less? */
  }
}


/*
** Check whether float 'f' is less than or equal to integer 'i'.
** See comments on previous function.
*/
[[nodiscard]] int LEfloatint (lua_Number f, lua_Integer i) {
  if (l_intfitsf(i))
    return luai_numle(f, cast_num(i));  /* compare them as floats */
  else {  /* f <= i <=> ceil(f) <= i */
    lua_Integer fi;
    if (luaV_flttointeger(f, &fi, F2Imod::F2Iceil))  /* fi = ceil(f) */
      return fi <= i;   /* compare them as integers */
    else  /* 'f' is either greater or less than all integers */
      return f < 0;  /* less? */
  }
}


/*
** return 'l < r' for non-numbers.
*/
int lua_State::lessThanOthers(const TValue *l, const TValue *r) {
  lua_assert(!ttisnumber(l) || !ttisnumber(r));
  if (ttisstring(l) && ttisstring(r))  /* both are strings? */
    return *tsvalue(l) < *tsvalue(r);  /* Use TString operator< */
  else
    return luaT_callorderTM(this, l, r, TMS::TM_LT);
}


/*
** Main operation less than; return 'l < r'.
*/
int luaV_lessthan (lua_State *L, const TValue *l, const TValue *r) {
  if (ttisnumber(l) && ttisnumber(r))  /* both operands are numbers? */
    return *l < *r;  /* Use operator< for cleaner syntax */
  else return L->lessThanOthers(l, r);
}


/*
** return 'l <= r' for non-numbers.
*/
int lua_State::lessEqualOthers(const TValue *l, const TValue *r) {
  lua_assert(!ttisnumber(l) || !ttisnumber(r));
  if (ttisstring(l) && ttisstring(r))  /* both are strings? */
    return *tsvalue(l) <= *tsvalue(r);  /* Use TString operator<= */
  else
    return luaT_callorderTM(this, l, r, TMS::TM_LE);
}


/*
** Main operation less than or equal to; return 'l <= r'.
*/
int luaV_lessequal (lua_State *L, const TValue *l, const TValue *r) {
  if (ttisnumber(l) && ttisnumber(r))  /* both operands are numbers? */
    return *l <= *r;  /* Use operator<= for cleaner syntax */
  else return L->lessEqualOthers(l, r);
}


/*
** Main operation for equality of Lua values; return 't1 == t2'.
** L == nullptr means raw equality (no metamethods)
*/
int luaV_equalobj (lua_State *L, const TValue *t1, const TValue *t2) {
  const TValue *tm;
  if (ttype(t1) != ttype(t2))  /* not the same type? */
    return 0;
  else if (ttypetag(t1) != ttypetag(t2)) {
    switch (ttypetag(t1)) {
      case LUA_VNUMINT: {  /* integer == float? */
        /* integer and float can only be equal if float has an integer
           value equal to the integer */
        lua_Integer i2;
        return (luaV_flttointeger(fltvalue(t2), &i2, F2Imod::F2Ieq) &&
                ivalue(t1) == i2);
      }
      case LUA_VNUMFLT: {  /* float == integer? */
        lua_Integer i1;  /* see comment in previous case */
        return (luaV_flttointeger(fltvalue(t1), &i1, F2Imod::F2Ieq) &&
                i1 == ivalue(t2));
      }
      case LUA_VSHRSTR: case LUA_VLNGSTR: {
        /* compare two strings with different variants: they can be
           equal when one string is a short string and the other is
           an external string  */
        return luaS_eqstr(tsvalue(t1), tsvalue(t2));
      }
      default:
        /* only numbers (integer/float) and strings (long/short) can have
           equal values with different variants */
        return 0;
    }
  }
  else {  /* equal variants */
    switch (ttypetag(t1)) {
      case LUA_VNIL: case LUA_VFALSE: case LUA_VTRUE:
        return 1;
      case LUA_VNUMINT:
        return (ivalue(t1) == ivalue(t2));
      case LUA_VNUMFLT:
        return (fltvalue(t1) == fltvalue(t2));
      case LUA_VLIGHTUSERDATA: return pvalue(t1) == pvalue(t2);
      case LUA_VSHRSTR:
        return eqshrstr(tsvalue(t1), tsvalue(t2));
      case LUA_VLNGSTR:
        return luaS_eqstr(tsvalue(t1), tsvalue(t2));
      case LUA_VUSERDATA: {
        if (uvalue(t1) == uvalue(t2)) return 1;
        else if (L == nullptr) return 0;
        tm = fasttm(L, uvalue(t1)->getMetatable(), TMS::TM_EQ);
        if (tm == nullptr)
          tm = fasttm(L, uvalue(t2)->getMetatable(), TMS::TM_EQ);
        break;  /* will try TM */
      }
      case LUA_VTABLE: {
        if (hvalue(t1) == hvalue(t2)) return 1;
        else if (L == nullptr) return 0;
        tm = fasttm(L, hvalue(t1)->getMetatable(), TMS::TM_EQ);
        if (tm == nullptr)
          tm = fasttm(L, hvalue(t2)->getMetatable(), TMS::TM_EQ);
        break;  /* will try TM */
      }
      case LUA_VLCF:
        return (fvalue(t1) == fvalue(t2));
      default:  /* functions and threads */
        return (gcvalue(t1) == gcvalue(t2));
    }
    if (tm == nullptr)  /* no TM? */
      return 0;  /* objects are different */
    else {
      int tag = luaT_callTMres(L, tm, t1, t2, L->getTop().p);  /* call TM */
      return !tagisfalse(tag);
    }
  }
}
