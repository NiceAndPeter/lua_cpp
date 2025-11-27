/*
** $Id: lvm_string.c $
** String operations for Lua VM
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
#include "lstring.h"
#include "ltm.h"
#include "lvm.h"
#include "lvirtualmachine.h"


/*
** Main operation for concatenation: concat 'total' values in the stack,
** from 'L->getTop().p - total' up to 'L->getTop().p - 1'.
** Wrapper: implementation moved to VirtualMachine::concat()
*/
void luaV_concat (lua_State *L, int total) {
  L->getVM().concat(total);
}


/*
** Main operation 'ra = #rb'.
** Wrapper: implementation moved to VirtualMachine::objlen()
*/
void luaV_objlen (lua_State *L, StkId ra, const TValue *rb) {
  L->getVM().objlen(ra, rb);
}
