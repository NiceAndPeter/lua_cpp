/*
** $Id: lfunc.h $
** Auxiliary functions to manipulate prototypes and closures
** See Copyright Notice in lua.h
*/

#ifndef lfunc_h
#define lfunc_h


#include "lobject.h"


// Phase 88: Convert sizeCclosure and sizeLclosure macros to inline constexpr functions
// These are simple forwarding calls to static methods
inline constexpr lu_mem sizeCclosure(int n) noexcept {
	return CClosure::sizeForUpvalues(n);
}

inline constexpr lu_mem sizeLclosure(int n) noexcept {
	return LClosure::sizeForUpvalues(n);
}

/* Phase 44.4: isintwups macro replaced with lua_State method:
** - isintwups(L) → L->isInTwups()
*/

/*
** maximum number of upvalues in a closure (both C and Lua). (Value
** must fit in a VM register.)
*/
inline constexpr int MAXUPVAL = 255;

/* Phase 44.3: UpVal macros replaced with methods:
** - upisopen(up) → up->isOpen()
** - uplevel(up) → up->getLevel()
*/

/*
** maximum number of misses before giving up the cache of closures
** in prototypes
*/
inline constexpr int MAXMISS = 10;



/* special status to close upvalues preserving the top of the stack */
inline constexpr int CLOSEKTOP = (LUA_ERRERR + 1);


LUAI_FUNC Proto *luaF_newproto (lua_State *L);
/* Phase 26: Removed luaF_initupvals - now LClosure::initUpvals() method */
LUAI_FUNC UpVal *luaF_findupval (lua_State *L, StkId level);
LUAI_FUNC void luaF_newtbcupval (lua_State *L, StkId level);
LUAI_FUNC void luaF_closeupval (lua_State *L, StkId level);
LUAI_FUNC StkId luaF_close (lua_State *L, StkId level, TStatus status, int yy);
LUAI_FUNC void luaF_unlinkupval (UpVal *uv);
LUAI_FUNC lu_mem luaF_protosize (Proto *p);
LUAI_FUNC void luaF_freeproto (lua_State *L, Proto *f);
LUAI_FUNC const char *luaF_getlocalname (const Proto *func, int local_number,
                                         int pc);


#endif
