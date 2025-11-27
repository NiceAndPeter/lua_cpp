/*
** $Id: lvm_arithmetic.c $
** Arithmetic operations for Lua VM
** See Copyright Notice in lua.h
*/

#define lvm_c
#define LUA_CORE

#include "lprefix.h"

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lobject.h"
#include "lstate.h"
#include "lvm.h"
#include "lvirtualmachine.h"


/*
** Wrapper: implementation moved to VirtualMachine::idiv()
*/
lua_Integer luaV_idiv (lua_State *L, lua_Integer m, lua_Integer n) {
  return L->getVM().idiv(m, n);
}

/*
** Wrapper: implementation moved to VirtualMachine::mod()
*/
lua_Integer luaV_mod (lua_State *L, lua_Integer m, lua_Integer n) {
  return L->getVM().mod(m, n);
}

/*
** Wrapper: implementation moved to VirtualMachine::modf()
*/
lua_Number luaV_modf (lua_State *L, lua_Number m, lua_Number n) {
  return L->getVM().modf(m, n);
}

/*
** Wrapper: implementation moved to VirtualMachine::shiftl()
*/
lua_Integer luaV_shiftl (lua_Integer x, lua_Integer y) {
  return VirtualMachine::shiftl(x, y);
}
