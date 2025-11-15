/*
** $Id: lapi.h $
** Auxiliary functions from Lua API
** See Copyright Notice in lua.h
*/

#ifndef lapi_h
#define lapi_h


#include "llimits.h"
#include "lstate.h"


#if defined(LUA_USE_APICHECK)
#include <assert.h>
#define api_check(l,e,msg)	assert(e)
#else	/* for testing */
#define api_check(l,e,msg)	((void)(l), lua_assert((e) && msg))
#endif



/* Increments 'L->getTop().p', checking for stack overflows */
#define api_incr_top(L)  \
    (L->getTop().p++, api_check(L, L->getTop().p <= L->getCI()->topRef().p, "stack overflow"))


/*
** macros that are executed whenever program enters the Lua core
** ('lua_lock') and leaves the core ('lua_unlock')
*/
#if !defined(lua_lock)
#define lua_lock(L)	((void) 0)
#define lua_unlock(L)	((void) 0)
#endif



/*
** If a call returns too many multiple returns, the callee may not have
** stack space to accommodate all results. In this case, this function
** increases its stack space ('L->getCI()->getTop().p').
** Phase 88: Converted from macro to inline function
*/
inline void adjustresults(lua_State* L, int nres) noexcept {
	if (nres <= LUA_MULTRET && L->getCI()->topRef().p < L->getTop().p) {
		L->getCI()->topRef().p = L->getTop().p;
	}
}


/* Ensure the stack has at least 'n' elements */
#define api_checknelems(L,n) \
       api_check(L, (n) < (L->getTop().p - L->getCI()->funcRef().p), \
                         "not enough elements in the stack")


/* Ensure the stack has at least 'n' elements to be popped. (Some
** functions only update a slot after checking it for popping, but that
** is only an optimization for a pop followed by a push.)
*/
#define api_checkpop(L,n) \
	api_check(L, (n) < L->getTop().p - L->getCI()->funcRef().p &&  \
                     L->getTbclist().p < L->getTop().p - (n), \
			  "not enough free elements in the stack")

#endif
