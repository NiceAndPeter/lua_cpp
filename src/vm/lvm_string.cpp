/*
** $Id: lvm_string.c $
** String operations for Lua VM
** See Copyright Notice in lua.h
*/

#define lvm_c
#define LUA_CORE

#include "lprefix.h"

#include <algorithm>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltm.h"
#include "lvm.h"


/* Function to ensure that element at 'o' is a string (converts if possible) */
inline bool tostring(lua_State* L, TValue* o) {
	if (ttisstring(o)) return true;
	if (!cvt2str(o)) return false;
	luaO_tostring(L, o);
	return true;
}

inline bool isemptystr(const TValue* o) noexcept {
	return ttisshrstring(o) && tsvalue(o)->length() == 0;
}

/* copy strings in stack from top - n up to top - 1 to buffer */
static void copy2buff (StkId top, int n, char *buff) {
  size_t tl = 0;  /* size already copied */
  do {
    TString *st = tsvalue(s2v(top - n));
    size_t l;  /* length of string being copied */
    const char *s = getlstr(st, l);
    std::copy_n(s, l, buff + tl);
    tl += l;
  } while (--n > 0);
}


/*
** Main operation for concatenation: concat 'total' values in the stack,
** from 'L->getTop().p - total' up to 'L->getTop().p - 1'.
*/
void luaV_concat (lua_State *L, int total) {
  if (total == 1)
    return;  /* "all" values already concatenated */
  do {
    StkId top = L->getTop().p;
    int n = 2;  /* number of elements handled in this pass (at least 2) */
    if (!(ttisstring(s2v(top - 2)) || cvt2str(s2v(top - 2))) ||
        !tostring(L, s2v(top - 1))) {
      luaT_tryconcatTM(L);  /* may invalidate 'top' */
      top = L->getTop().p;  /* recapture after potential GC */
    }
    else if (isemptystr(s2v(top - 1))) {  /* second operand is empty? */
      cast_void(tostring(L, s2v(top - 2)));  /* result is first operand */
      top = L->getTop().p;  /* recapture after potential GC */
    }
    else if (isemptystr(s2v(top - 2))) {  /* first operand is empty string? */
      *s2v(top - 2) = *s2v(top - 1);  /* result is second op. (operator=) */
    }
    else {
      /* at least two non-empty string values; get as many as possible */
      size_t tl = tsslen(tsvalue(s2v(top - 1)));
      TString *ts;
      /* collect total length and number of strings */
      for (n = 1; n < total && tostring(L, s2v(top - n - 1)); n++) {
        top = L->getTop().p;  /* recapture after tostring() which can trigger GC */
        size_t l = tsslen(tsvalue(s2v(top - n - 1)));
        if (l_unlikely(l >= MAX_SIZE - sizeof(TString) - tl)) {
          L->getStackSubsystem().setTopPtr(top - total);  /* pop strings to avoid wasting stack */
          luaG_runerror(L, "string length overflow");
        }
        tl += l;
      }
      if (tl <= LUAI_MAXSHORTLEN) {  /* is result a short string? */
        char buff[LUAI_MAXSHORTLEN];
        copy2buff(top, n, buff);  /* copy strings to buffer */
        ts = TString::create(L, buff, tl);
        top = L->getTop().p;  /* recapture after potential GC */
      }
      else {  /* long string; copy strings directly to final result */
        ts = TString::createLongString(L, tl);
        top = L->getTop().p;  /* recapture after potential GC */
        copy2buff(top, n, getlngstr(ts));
      }
      setsvalue2s(L, top - n, ts);  /* create result */
    }
    total -= n - 1;  /* got 'n' strings to create one new */
    L->getStackSubsystem().popN(n - 1);  /* popped 'n' strings and pushed one */
  } while (total > 1);  /* repeat until only 1 result left */
}


/*
** Main operation 'ra = #rb'.
*/
void luaV_objlen (lua_State *L, StkId ra, const TValue *rb) {
  const TValue *tm;
  switch (ttypetag(rb)) {
    case LUA_VTABLE: {
      Table *h = hvalue(rb);
      tm = fasttm(L, h->getMetatable(), TMS::TM_LEN);
      if (tm) break;  /* metamethod? break switch to call it */
      s2v(ra)->setInt(l_castU2S(luaH_getn(L, h)));  /* else primitive len */
      return;
    }
    case LUA_VSHRSTR: {
      s2v(ra)->setInt(static_cast<lua_Integer>(tsvalue(rb)->length()));
      return;
    }
    case LUA_VLNGSTR: {
      s2v(ra)->setInt(cast_st2S(tsvalue(rb)->getLnglen()));
      return;
    }
    default: {  /* try metamethod */
      tm = luaT_gettmbyobj(L, rb, TMS::TM_LEN);
      if (l_unlikely(notm(tm)))  /* no metamethod? */
        luaG_typeerror(L, rb, "get length of");
      break;
    }
  }
  luaT_callTMres(L, tm, rb, rb, ra);
}
