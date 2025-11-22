/*
** $Id: lvm_table.c $
** Table access operations for Lua VM
** See Copyright Notice in lua.h
*/

#define lvm_c
#define LUA_CORE

#include "lprefix.h"

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"


/*
** Limit for table tag-method (metamethod) chains to prevent infinite loops.
** When __index or __newindex metamethods redirect to other tables/objects,
** this limit ensures we don't loop forever if there's a cycle in the chain.
*/
inline constexpr int MAXTAGLOOP = 2000;


/*
** Finish the table access 'val = t[key]' and return the tag of the result.
**
** This function is called when the fast path for table access (luaV_fastget)
** fails to find a value. It handles:
** 1. Non-table types: looks for __index metamethod
** 2. Tables without the key: looks for __index metamethod
** 3. Metamethod chains: follows __index chain until finding a value
**
** The loop allows __index to point to another table (or object with __index),
** creating a chain of metamethod lookups similar to prototype-based inheritance.
** Example: obj.__index = parent; parent.__index = grandparent
**
** PERFORMANCE: This is the slow path. The hot path (direct table access) is
** handled inline in the VM main loop via luaV_fastget macro.
*/
LuaT luaV_finishget (lua_State *L, const TValue *t, TValue *key,
                                      StkId val, LuaT tag) {
  const TValue *tm;  /* metamethod */
  for (int loop = 0; loop < MAXTAGLOOP; loop++) {
    if (tag == LuaT::NOTABLE) {  /* 't' is not a table? */
      lua_assert(!ttistable(t));
      tm = luaT_gettmbyobj(L, t, TMS::TM_INDEX);
      if (l_unlikely(notm(tm)))
        luaG_typeerror(L, t, "index");  /* no metamethod */
      /* else will try the metamethod */
    }
    else {  /* 't' is a table */
      tm = fasttm(L, hvalue(t)->getMetatable(), TMS::TM_INDEX);  /* table's metamethod */
      if (tm == nullptr) {  /* no metamethod? */
        setnilvalue(s2v(val));  /* result is nil */
        return LuaT::NIL;
      }
      /* else will try the metamethod */
    }
    if (ttisfunction(tm)) {  /* is metamethod a function? */
      tag = luaT_callTMres(L, tm, t, key, val);  /* call it */
      return tag;  /* return tag of the result */
    }
    t = tm;  /* else try to access 'tm[key]' */
    tag = luaV_fastget(t, key, s2v(val), luaH_get);
    if (!tagisempty(tag))
      return tag;  /* done */
    /* else repeat (tail call 'luaV_finishget') */
  }
  luaG_runerror(L, "'__index' chain too long; possible loop");
  return LuaT::NIL;  /* to avoid warnings */
}


/*
** Finish a table assignment 't[key] = val'.
**
** Called when the fast path for table assignment (luaV_fastset) fails.
** Handles __newindex metamethods similar to how luaV_finishget handles __index.
**
** About anchoring the table before the call to 'luaH_finishset':
** This call may trigger an emergency collection. When loop>0,
** the table being accessed is a field in some metatable. If this
** metatable is weak and the table is not anchored, this collection
** could collect that table while it is being updated.
**
** ANCHORING MECHANISM: We temporarily push the table onto the stack to ensure
** the GC sees it as a live object during the allocation that may happen in
** luaH_finishset. This is critical for weak tables accessed through metamethod
** chains, as they might otherwise be collected mid-operation.
**
** GC BARRIER: After successful assignment, we call luaC_barrierback to maintain
** the garbage collector's tri-color invariant (see lgc.cpp for details).
*/
void luaV_finishset (lua_State *L, const TValue *t, TValue *key,
                      TValue *val, int hres) {
  for (int loop = 0; loop < MAXTAGLOOP; loop++) {
    const TValue *tm;  /* '__newindex' metamethod */
    if (hres != HNOTATABLE) {  /* is 't' a table? */
      auto *h = hvalue(t);  /* save 't' table */
      tm = fasttm(L, h->getMetatable(), TMS::TM_NEWINDEX);  /* get metamethod */
      if (tm == nullptr) {  /* no metamethod? */
        sethvalue2s(L, L->getTop().p, h);  /* anchor 't' */
        L->getStackSubsystem().push();  /* assume EXTRA_STACK */
        luaH_finishset(L, h, key, val, hres);  /* set new value */
        L->getStackSubsystem().pop();
        invalidateTMcache(h);
        luaC_barrierback(L, obj2gco(h), val);
        return;
      }
      /* else will try the metamethod */
    }
    else {  /* not a table; check metamethod */
      tm = luaT_gettmbyobj(L, t, TMS::TM_NEWINDEX);
      if (l_unlikely(notm(tm)))
        luaG_typeerror(L, t, "index");
    }
    /* try the metamethod */
    if (ttisfunction(tm)) {
      luaT_callTM(L, tm, t, key, val);
      return;
    }
    t = tm;  /* else repeat assignment over 'tm' */
    hres = luaV_fastset(t, key, val, luaH_pset);
    if (hres == HOK) {
      luaV_finishfastset(L, t, val);
      return;  /* done */
    }
    /* else 'return luaV_finishset(L, t, key, val, slot)' (loop) */
  }
  luaG_runerror(L, "'__newindex' chain too long; possible loop");
}
