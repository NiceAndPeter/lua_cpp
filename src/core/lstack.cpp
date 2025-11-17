/*
** $Id: lstack.c $
** Lua Stack Management
** See Copyright Notice in lua.h
*/

#define lstack_c
#define LUA_CORE

#include "lprefix.h"

#include <algorithm>
#include <climits>
#include <cstring>

#include "lua.h"

#include "lstack.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"


/*
** Constants for stack management
*/

/* Some stack space for error handling */
inline constexpr int STACKERRSPACE = 200;

/*
** LUAI_MAXSTACK limits the size of the Lua stack.
** It must fit into INT_MAX/2.
*/
#if !defined(LUAI_MAXSTACK)
#if 1000000 < (std::numeric_limits<int>::max() / 2)
#define LUAI_MAXSTACK           1000000
#else
#define LUAI_MAXSTACK           (std::numeric_limits<int>::max() / 2u)
#endif
#endif

/* Maximum stack size that respects size_t */
#define MAXSTACK_BYSIZET  ((MAX_SIZET / sizeof(StackValue)) - STACKERRSPACE)

/* Minimum between LUAI_MAXSTACK and MAXSTACK_BYSIZET */
#define MAXSTACK	cast_int(LUAI_MAXSTACK < MAXSTACK_BYSIZET  \
			        ? LUAI_MAXSTACK : MAXSTACK_BYSIZET)

/* Stack size with extra space for error handling */
#define ERRORSTACKSIZE	(MAXSTACK + STACKERRSPACE)


/*
** In ISO C, any pointer use after the pointer has been deallocated is
** undefined behavior. So, before a stack reallocation, all pointers
** should be changed to offsets, and after the reallocation they should
** be changed back to pointers. As during the reallocation the pointers
** are invalid, the reallocation cannot run emergency collections.
** Alternatively, we can use the old address after the deallocation.
** That is not strict ISO C, but seems to work fine everywhere.
** The following macro chooses how strict is the code.
*/
#if !defined(LUAI_STRICT_ADDRESS)
#define LUAI_STRICT_ADDRESS	1
#endif


/*
** Conditional stack movement for debugging
*/
#if !defined(HARDSTACKTESTS)
#define condmovestack(L,pre,pos)	((void)0)
#else
/* realloc stack keeping its size */
#define condmovestack(L,pre,pos)  \
  { int sz_ = (L)->getStackSubsystem().getSize(); pre; \
    (L)->getStackSubsystem().realloc(L, sz_, 0); pos; }
#endif


/*
** ==================================================================
** Stack Initialization and Cleanup
** ==================================================================
*/

/*
** Initialize a new stack (called from lstate.cpp)
** L is used for memory allocation (may be different from owning thread)
*/
void LuaStack::init(lua_State* L) {
  /* allocate stack array */
  stack.p = luaM_newvector(L, BASIC_STACK_SIZE + EXTRA_STACK, StackValue);
  tbclist.p = stack.p;

  /* erase new stack */
  std::for_each_n(stack.p, BASIC_STACK_SIZE + EXTRA_STACK, [](StackValue& sv) {
    setnilvalue(s2v(&sv));
  });

  stack_last.p = stack.p + BASIC_STACK_SIZE;
  top.p = stack.p + 1;  /* will be set properly by caller */
}


/*
** Free stack memory (called from lstate.cpp)
*/
void LuaStack::free(lua_State* L) {
  if (stack.p == NULL)
    return;  /* stack not completely built yet */
  /* free stack */
  luaM_freearray(L, stack.p, cast_sizet(getSize() + EXTRA_STACK));
}


/*
** ==================================================================
** Stack Usage Calculation
** ==================================================================
*/

/*
** Compute how much of the stack is being used, by computing the
** maximum top of all call frames in the stack and the current top.
*/
int LuaStack::inUse(const lua_State* L) const {
  const CallInfo* ci_iter;
  int res;
  StkId lim = top.p;

  for (ci_iter = L->getCI(); ci_iter != NULL; ci_iter = ci_iter->getPrevious()) {
    if (lim < ci_iter->topRef().p)
      lim = ci_iter->topRef().p;
  }

  lua_assert(lim <= stack_last.p + EXTRA_STACK);
  res = cast_int(lim - stack.p) + 1;  /* part of stack in use */

  if (res < LUA_MINSTACK)
    res = LUA_MINSTACK;  /* ensure a minimum size */

  return res;
}


/*
** ==================================================================
** Pointer Adjustment for Reallocation
** ==================================================================
*/

#if LUAI_STRICT_ADDRESS

/*
** Change all pointers to the stack into offsets (before reallocation).
*/
void LuaStack::relPointers(lua_State* L) {
  CallInfo* ci;
  UpVal* up;

  top.offset = save(top.p);
  tbclist.offset = save(tbclist.p);

  for (up = L->getOpenUpval(); up != NULL; up = up->getOpenNext())
    up->setOffset(save(up->getLevel()));

  for (ci = L->getCI(); ci != NULL; ci = ci->getPrevious()) {
    ci->topRef().offset = save(ci->topRef().p);
    ci->funcRef().offset = save(ci->funcRef().p);
  }
}


/*
** Change back all offsets into pointers (after reallocation).
*/
void LuaStack::correctPointers(lua_State* L, StkId oldstack) {
  CallInfo* ci;
  UpVal* up;
  UNUSED(oldstack);

  top.p = restore(top.offset);
  tbclist.p = restore(tbclist.offset);

  for (up = L->getOpenUpval(); up != NULL; up = up->getOpenNext())
    up->setVP(s2v(restore(up->getOffset())));

  for (ci = L->getCI(); ci != NULL; ci = ci->getPrevious()) {
    ci->topRef().p = restore(ci->topRef().offset);
    ci->funcRef().p = restore(ci->funcRef().offset);
    if (ci->isLua())
      ci->getTrap() = 1;  /* signal to update 'trap' in 'luaV_execute' */
  }
}

#else  /* !LUAI_STRICT_ADDRESS */

/*
** Assume that it is fine to use an address after its deallocation,
** as long as we do not dereference it.
*/
void LuaStack::relPointers(lua_State* L) {
  UNUSED(L);  /* do nothing */
}


/*
** Correct pointers into 'oldstack' to point into new stack.
*/
void LuaStack::correctPointers(lua_State* L, StkId oldstack) {
  CallInfo* ci;
  UpVal* up;
  StkId newstack = stack.p;

  if (oldstack == newstack)
    return;

  top.p = top.p - oldstack + newstack;
  tbclist.p = tbclist.p - oldstack + newstack;

  for (up = L->getOpenUpval(); up != NULL; up = up->getOpenNext())
    up->setVP(s2v(up->getLevel() - oldstack + newstack));

  for (ci = L->getCI(); ci != NULL; ci = ci->getPrevious()) {
    ci->topRef().p = ci->topRef().p - oldstack + newstack;
    ci->funcRef().p = ci->funcRef().p - oldstack + newstack;
    if (ci->isLua())
      ci->getTrap() = 1;  /* signal to update 'trap' in 'luaV_execute' */
  }
}

#endif  /* LUAI_STRICT_ADDRESS */


/*
** ==================================================================
** Stack Reallocation
** ==================================================================
*/

/*
** Reallocate stack to exact size 'newsize'.
** Returns 1 on success, 0 on failure.
*/
int LuaStack::realloc(lua_State* L, int newsize, int raiseerror) {
  int oldsize = getSize();
  int i;
  StkId newstack;
  StkId oldstack = stack.p;
  lu_byte oldgcstop = G(L)->getGCStopEm();

  lua_assert(newsize <= MAXSTACK || newsize == ERRORSTACKSIZE);

  relPointers(L);  /* change pointers to offsets */
  G(L)->setGCStopEm(1);  /* stop emergency collection */

  newstack = luaM_reallocvector(L, oldstack, oldsize + EXTRA_STACK,
                                   newsize + EXTRA_STACK, StackValue);

  G(L)->setGCStopEm(oldgcstop);  /* restore emergency collection */

  if (l_unlikely(newstack == NULL)) {  /* reallocation failed? */
    correctPointers(L, oldstack);  /* change offsets back to pointers */
    if (raiseerror)
      luaM_error(L);
    else
      return 0;  /* do not raise an error */
  }

  stack.p = newstack;
  correctPointers(L, oldstack);  /* change offsets back to pointers */
  stack_last.p = stack.p + newsize;

  /* erase new segment */
  for (i = oldsize + EXTRA_STACK; i < newsize + EXTRA_STACK; i++)
    setnilvalue(s2v(newstack + i));

  return 1;
}


/*
** Grow stack by at least 'n' elements.
** Returns 1 on success, 0 on failure.
*/
int LuaStack::grow(lua_State* L, int n, int raiseerror) {
  int size = getSize();

  if (l_unlikely(size > MAXSTACK)) {
    /* if stack is larger than maximum, thread is already using the
       extra space reserved for errors, that is, thread is handling
       a stack error; cannot grow further than that. */
    lua_assert(getSize() == ERRORSTACKSIZE);
    if (raiseerror)
      L->errorError();  /* stack error inside message handler */
    return 0;  /* if not 'raiseerror', just signal it */
  }
  else if (n < MAXSTACK) {  /* avoids arithmetic overflows */
    int newsize = size + (size >> 1);  /* tentative new size (size * 1.5) */
    int needed = cast_int(top.p - stack.p) + n;

    if (newsize > MAXSTACK)  /* cannot cross the limit */
      newsize = MAXSTACK;
    if (newsize < needed)  /* but must respect what was asked for */
      newsize = needed;
    if (l_likely(newsize <= MAXSTACK))
      return realloc(L, newsize, raiseerror);
  }

  /* else stack overflow */
  /* add extra size to be able to handle the error message */
  realloc(L, ERRORSTACKSIZE, raiseerror);
  if (raiseerror)
    luaG_runerror(L, "stack overflow");
  return 0;
}


/*
** Shrink stack to reasonable size.
** Called after function returns to free unused stack space.
*/
void LuaStack::shrink(lua_State* L) {
  int inuse = inUse(L);
  int max = (inuse > MAXSTACK / 3) ? MAXSTACK : inuse * 3;

  /* if thread is currently not handling a stack overflow and its
     size is larger than maximum "reasonable" size, shrink it */
  if (inuse <= MAXSTACK && getSize() > max) {
    int nsize = (inuse > MAXSTACK / 2) ? MAXSTACK : inuse * 2;
    realloc(L, nsize, 0);  /* ok if that fails */
  }
  else  /* don't change stack */
    condmovestack(L, (void)0, (void)0);  /* (change only for debugging) */

  luaE_shrinkCI(L);  /* shrink CI list */
}


/*
** Increment top with stack overflow check.
** Used when pushing a single value.
*/
void LuaStack::incTop(lua_State* L) {
  top.p++;
  luaD_checkstack(L, 1);
}
