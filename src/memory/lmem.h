/*
** $Id: lmem.h $
** Interface to Memory Manager
** See Copyright Notice in lua.h
*/

#ifndef lmem_h
#define lmem_h


#include <cstddef>

#include "llimits.h"
#include "lua.h"


#define luaM_error(L)	(L)->doThrow(LUA_ERRMEM)


/*
** This macro tests whether it is safe to multiply 'n' by the size of
** type 't' without overflows. Because 'e' is always constant, it avoids
** the runtime division MAX_SIZET/(e).
** (The macro is somewhat complex to avoid warnings:  The 'sizeof'
** comparison avoids a runtime comparison when overflow cannot occur.
** The compiler should be able to optimize the real test by itself, but
** when it does it, it may give a warning about "comparison is always
** false due to limited range of data type"; the +1 tricks the compiler,
** avoiding this warning but also this optimization.)
*/
#define luaM_testsize(n,e)  \
	(sizeof(n) >= sizeof(size_t) && cast_sizet((n)) + 1 > MAX_SIZET/(e))

#define luaM_checksize(L,n,e)  \
	(luaM_testsize(n,e) ? luaM_toobig(L) : cast_void(0))


/*
** Computes the minimum between 'n' and 'MAX_SIZET/sizeof(t)', so that
** the result is not larger than 'n' and cannot overflow a 'size_t'
** when multiplied by the size of type 't'. (Assumes that 'n' is an
** 'int' and that 'int' is not larger than 'size_t'.)
*/
#define luaM_limitN(n,t)  \
  ((cast_sizet(n) <= MAX_SIZET/sizeof(t)) ? (n) :  \
     cast_int((MAX_SIZET/sizeof(t))))


/*
** Arrays of chars do not need any test
*/
#define luaM_reallocvchar(L,b,on,n)  \
  cast_charp(luaM_saferealloc_(L, (b), (on)*sizeof(char), (n)*sizeof(char)))

#define luaM_freemem(L, b, s)	luaM_free_(L, (b), (s))

/* Forward declarations of underlying memory functions */
LUAI_FUNC l_noret luaM_toobig (lua_State *L);
LUAI_FUNC void *luaM_realloc_ (lua_State *L, void *block, size_t oldsize,
                                                          size_t size);
LUAI_FUNC void *luaM_saferealloc_ (lua_State *L, void *block, size_t oldsize,
                                                              size_t size);
LUAI_FUNC void luaM_free_ (lua_State *L, void *block, size_t osize);
LUAI_FUNC void *luaM_growaux_ (lua_State *L, void *block, int nelems,
                               int *size, unsigned size_elem, int limit,
                               const char *what);
LUAI_FUNC void *luaM_shrinkvector_ (lua_State *L, void *block, int *nelem,
                                    int final_n, unsigned size_elem);
LUAI_FUNC void *luaM_malloc_ (lua_State *L, size_t size, int tag);

/*
** Template-based memory management functions for type safety.
** These replace the old macros with proper C++ templates.
*/

/* Free a single object of type T */
template<typename T>
inline void luaM_free(lua_State* L, T* b) noexcept {
	luaM_free_(L, static_cast<void*>(b), sizeof(T));
}

/* Free an array of n objects of type T */
template<typename T>
inline void luaM_freearray(lua_State* L, T* b, size_t n) noexcept {
	luaM_free_(L, static_cast<void*>(b), n * sizeof(T));
}

/* Allocate a single object of type T */
template<typename T>
inline T* luaM_new(lua_State* L) {
	return static_cast<T*>(luaM_malloc_(L, sizeof(T), 0));
}

/* Allocate an array of n objects of type T */
template<typename T>
inline T* luaM_newvector(lua_State* L, size_t n) {
	return static_cast<T*>(luaM_malloc_(L, cast_sizet(n) * sizeof(T), 0));
}

/* Allocate an array with size check */
template<typename T>
inline T* luaM_newvectorchecked(lua_State* L, size_t n) {
	luaM_checksize(L, n, sizeof(T));
	return luaM_newvector<T>(L, n);
}

#define luaM_newobject(L,tag,s)	luaM_malloc_(L, (s), tag)

/* Allocate a block of size bytes (char array) */
inline char* luaM_newblock(lua_State* L, size_t size) {
	return luaM_newvector<char>(L, size);
}

/* Reallocate an array from oldn to n elements */
template<typename T>
inline T* luaM_reallocvector(lua_State* L, T* v, size_t oldn, size_t n) {
	return static_cast<T*>(luaM_realloc_(L, v, cast_sizet(oldn) * sizeof(T),
	                                             cast_sizet(n) * sizeof(T)));
}

/* Grow a vector, updating size and checking limit */
template<typename T>
inline void luaM_growvector(lua_State* L, T*& v, int nelems, int& size, int limit, const char* e) {
	v = static_cast<T*>(luaM_growaux_(L, v, nelems, &size, sizeof(T),
	                                   luaM_limitN(limit, T), e));
}

/* Shrink a vector to final_n elements, updating size */
template<typename T>
inline void luaM_shrinkvector(lua_State* L, T*& v, int& size, int final_n) {
	v = static_cast<T*>(luaM_shrinkvector_(L, v, &size, final_n, sizeof(T)));
}

/* Note: Function declarations moved above template definitions */

#endif

