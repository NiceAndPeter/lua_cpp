/*
** $Id: lvirtualmachine.cpp $
** Lua Virtual Machine - Implementation
** See Copyright Notice in lua.h
*/

#define lvirtualmachine_cpp
#define LUA_CORE

#include "lprefix.h"

#include "lvirtualmachine.h"
#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"

/*
** Phase 122 Part 1: Infrastructure only - implementations will be added in Part 2
** For now, these are stub implementations that call existing luaV_* functions
*/

// === EXECUTION ===

void VirtualMachine::execute(CallInfo *ci) {
    luaV_execute(L, ci);
}

void VirtualMachine::finishOp() {
    luaV_finishOp(L);
}

// === TYPE CONVERSIONS ===

int VirtualMachine::tonumber(const TValue *obj, lua_Number *n) {
    return luaV_tonumber_(obj, n);
}

int VirtualMachine::tointeger(const TValue *obj, lua_Integer *p, F2Imod mode) {
    return luaV_tointeger(obj, p, mode);
}

int VirtualMachine::tointegerns(const TValue *obj, lua_Integer *p, F2Imod mode) {
    return luaV_tointegerns(obj, p, mode);
}

int VirtualMachine::flttointeger(lua_Number n, lua_Integer *p, F2Imod mode) {
    return luaV_flttointeger(n, p, mode);
}

// === ARITHMETIC ===

lua_Integer VirtualMachine::idiv(lua_Integer m, lua_Integer n) {
    return luaV_idiv(L, m, n);
}

lua_Integer VirtualMachine::mod(lua_Integer m, lua_Integer n) {
    return luaV_mod(L, m, n);
}

lua_Number VirtualMachine::modf(lua_Number m, lua_Number n) {
    return luaV_modf(L, m, n);
}

lua_Integer VirtualMachine::shiftl(lua_Integer x, lua_Integer y) {
    return luaV_shiftl(x, y);
}

// === COMPARISONS ===

int VirtualMachine::lessThan(const TValue *l, const TValue *r) {
    return luaV_lessthan(L, l, r);
}

int VirtualMachine::lessEqual(const TValue *l, const TValue *r) {
    return luaV_lessequal(L, l, r);
}

int VirtualMachine::equalObj(const TValue *t1, const TValue *t2) {
    return luaV_equalobj(L, t1, t2);
}

// === TABLE OPERATIONS ===

LuaT VirtualMachine::finishGet(const TValue *t, TValue *key, StkId val, LuaT tag) {
    return luaV_finishget(L, t, key, val, tag);
}

void VirtualMachine::finishSet(const TValue *t, TValue *key, TValue *val, int aux) {
    luaV_finishset(L, t, key, val, aux);
}

// === STRING/OBJECT OPERATIONS ===

void VirtualMachine::concat(int total) {
    luaV_concat(L, total);
}

void VirtualMachine::objlen(StkId ra, const TValue *rb) {
    luaV_objlen(L, ra, rb);
}
