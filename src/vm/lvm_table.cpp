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
#include "lvirtualmachine.h"


/*
** Limit for table tag-method (metamethod) chains to prevent infinite loops.
** When __index or __newindex metamethods redirect to other tables/objects,
** this limit ensures we don't loop forever if there's a cycle in the chain.
*/
inline constexpr int MAXTAGLOOP = 2000;


/*
** Finish the table access 'val = t[key]' and return the tag of the result.
** Wrapper: implementation moved to VirtualMachine::finishGet()
*/
LuaT luaV_finishget (lua_State *L, const TValue *t, TValue *key,
                                      StkId val, LuaT tag) {
  return L->getVM().finishGet(t, key, val, tag);
}


/*
** Finish a table assignment 't[key] = val'.
** Wrapper: implementation moved to VirtualMachine::finishSet()
*/
void luaV_finishset (lua_State *L, const TValue *t, TValue *key,
                      TValue *val, int hres) {
  L->getVM().finishSet(t, key, val, hres);
}
