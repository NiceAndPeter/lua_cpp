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
#include "lapi.h"
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


/* Phase 126.2: Convert condmovestack macro to inline function
** Conditional stack movement for debugging
*/
#if !defined(HARDSTACKTESTS)
template<typename Pre, typename Pos>
inline void condmovestack(lua_State* L, Pre&& pre, Pos&& pos) noexcept {
	cast_void(L); cast_void(pre); cast_void(pos);
}
#else
/* realloc stack keeping its size */
template<typename Pre, typename Pos>
inline void condmovestack(lua_State* L, Pre&& pre, Pos&& pos) {
	int sz_ = L->getStackSubsystem().getSize();
	pre();
	L->getStackSubsystem().realloc(L, sz_, 0);
	pos();
}
#endif


/*
** ==================================================================
** Stack Initialization and Cleanup
** ==================================================================
*/

/*
** Initialize a new stack (called from lstate.cpp)
** L is used for memory allocation (may be different from owning thread)
**
** SINGLE-BLOCK ALLOCATION:
** Allocates values and deltas as ONE contiguous block to ensure
** exception-safe, atomic allocation (no partial failure states).
*/
void LuaStack::init(lua_State* L) {
  constexpr int init_size = BASIC_STACK_SIZE + EXTRA_STACK;

  /* Calculate sizes for single-block allocation */
  size_t values_bytes = sizeof(StackValue) * init_size;
  size_t deltas_bytes = sizeof(unsigned short) * init_size;
  size_t total_bytes = values_bytes + deltas_bytes;

  /* Allocate single block for both arrays (atomic operation!) */
  char* block = luaM_newvector<char>(L, total_bytes);

  /* Split block into two sections */
  stack.p = reinterpret_cast<StackValue*>(block);
  tbc_deltas = reinterpret_cast<unsigned short*>(block + values_bytes);
  stack_size = init_size;

  tbclist.p = stack.p;

  /* Initialize delta array to zero */
  std::memset(tbc_deltas, 0, deltas_bytes);

  /* erase new stack */
  std::for_each_n(stack.p, init_size, [](StackValue& sv) {
    setnilvalue(s2v(&sv));
  });

  stack_last.p = stack.p + BASIC_STACK_SIZE;
  top.p = stack.p + 1;  /* will be set properly by caller */
}


/*
** Free stack memory (called from lstate.cpp)
**
** SINGLE-BLOCK DEALLOCATION:
** Frees the entire block (values + deltas) that was allocated in init().
*/
void LuaStack::free(lua_State* L) {
  if (stack.p == nullptr)
    return;  /* stack not completely built yet */

  /* Calculate total size of single block */
  size_t values_bytes = sizeof(StackValue) * stack_size;
  size_t deltas_bytes = sizeof(unsigned short) * stack_size;
  size_t total_bytes = values_bytes + deltas_bytes;

  /* Free single block (cast stack.p back to char*) */
  char* block = reinterpret_cast<char*>(stack.p);
  luaM_freemem(L, block, total_bytes);

  /* Reset pointers and size */
  stack.p = nullptr;
  tbc_deltas = nullptr;
  stack_size = 0;
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

  for (ci_iter = L->getCI(); ci_iter != nullptr; ci_iter = ci_iter->getPrevious()) {
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

  for (up = L->getOpenUpval(); up != nullptr; up = up->getOpenNext())
    up->setOffset(save(up->getLevel()));

  for (ci = L->getCI(); ci != nullptr; ci = ci->getPrevious()) {
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

  for (up = L->getOpenUpval(); up != nullptr; up = up->getOpenNext())
    up->setVP(s2v(restore(up->getOffset())));

  for (ci = L->getCI(); ci != nullptr; ci = ci->getPrevious()) {
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

  for (up = L->getOpenUpval(); up != nullptr; up = up->getOpenNext())
    up->setVP(s2v(up->getLevel() - oldstack + newstack));

  for (ci = L->getCI(); ci != nullptr; ci = ci->getPrevious()) {
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
**
** SINGLE-BLOCK REALLOCATION:
** Reallocates the entire block (values + deltas) atomically.
** This is the critical function that Phase 135 failed on - we solve it
** by using a single allocation instead of dual parallel arrays.
*/
int LuaStack::realloc(lua_State* L, int newsize, int raiseerror) {
  int oldsize_usable = getSize();  /* usable size (excludes EXTRA_STACK) */
  int oldsize_allocated = stack_size;  /* allocated size (includes EXTRA_STACK) */
  int newsize_allocated = newsize + EXTRA_STACK;
  StkId oldstack = stack.p;
  lu_byte oldgcstop = G(L)->getGCStopEm();

  lua_assert(newsize <= MAXSTACK || newsize == ERRORSTACKSIZE);
  lua_assert(stack.p != nullptr);

  /* Calculate old and new block sizes */
  size_t old_values_bytes = sizeof(StackValue) * oldsize_allocated;
  size_t old_deltas_bytes = sizeof(unsigned short) * oldsize_allocated;
  size_t old_total_bytes = old_values_bytes + old_deltas_bytes;

  size_t new_values_bytes = sizeof(StackValue) * newsize_allocated;
  size_t new_deltas_bytes = sizeof(unsigned short) * newsize_allocated;
  size_t new_total_bytes = new_values_bytes + new_deltas_bytes;

  relPointers(L);  /* change pointers to offsets */
  G(L)->setGCStopEm(1);  /* stop emergency collection */

  /* Reallocate single block (atomic operation - both arrays or neither!) */
  char* old_block = reinterpret_cast<char*>(oldstack);
  char* new_block = luaM_reallocvector<char>(L, old_block, old_total_bytes, new_total_bytes);

  G(L)->setGCStopEm(oldgcstop);  /* restore emergency collection */

  if (l_unlikely(new_block == nullptr)) {  /* reallocation failed? */
    correctPointers(L, oldstack);  /* change offsets back to pointers */
    if (raiseerror)
      luaM_error(L);
    else
      return 0;  /* do not raise an error */
  }

  /* Split new block into values and deltas sections */
  StackValue* newstack = reinterpret_cast<StackValue*>(new_block);
  unsigned short* new_deltas = reinterpret_cast<unsigned short*>(new_block + new_values_bytes);

  /* Update pointers and size */
  stack.p = newstack;
  tbc_deltas = new_deltas;
  stack_size = newsize_allocated;

  correctPointers(L, oldstack);  /* change offsets back to pointers */
  stack_last.p = stack.p + newsize;

  /* erase new TValue segment */
  for (int i = oldsize_usable + EXTRA_STACK; i < newsize_allocated; i++)
    setnilvalue(s2v(newstack + i));

  /* initialize new delta segment to zero */
  if (newsize_allocated > oldsize_allocated) {
    size_t new_delta_slots = newsize_allocated - oldsize_allocated;
    std::memset(new_deltas + oldsize_allocated, 0, new_delta_slots * sizeof(unsigned short));
  }

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
    int newsize;
    /* Check for overflow in size * 1.5 calculation */
    if (size > INT_MAX / 3 * 2) {
      /* size + (size >> 1) could overflow, use MAXSTACK */
      newsize = MAXSTACK;
    } else {
      newsize = size + (size >> 1);  /* tentative new size (size * 1.5) */
    }

    /* Safe calculation of needed space */
    ptrdiff_t stack_used = top.p - stack.p;
    lua_assert(stack_used >= 0 && stack_used <= INT_MAX);
    int needed;
    if (stack_used > INT_MAX - n) {
      /* needed calculation would overflow, use MAXSTACK */
      needed = MAXSTACK;
    } else {
      needed = cast_int(stack_used) + n;
    }

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
    condmovestack(L, [](){}, [](){});  /* (change only for debugging) */

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


/*
** ==================================================================
** INDEX CONVERSION OPERATIONS (Phase 94.1)
** ==================================================================
** Convert Lua API indices to internal stack pointers.
** Moved from index2value() and index2stack() in lapi.cpp.
*/

/*
** Convert an acceptable index to a pointer to its respective value.
** Non-valid indices return the special nil value 'G(L)->getNilValue()'.
**
** Replaces index2value() from lapi.cpp.
*/
TValue* LuaStack::indexToValue(lua_State* L, int idx) {
  CallInfo *ci = L->getCI();
  if (idx > 0) {
    StkId o = ci->funcRef().p + idx;
    api_check(L, idx <= ci->topRef().p - (ci->funcRef().p + 1), "unacceptable index");
    if (o >= top.p) return G(L)->getNilValue();
    else return s2v(o);
  }
  else if (!ispseudo(idx)) {  /* negative index */
    api_check(L, idx != 0 && -idx <= top.p - (ci->funcRef().p + 1),
                 "invalid index");
    return s2v(top.p + idx);
  }
  else if (idx == LUA_REGISTRYINDEX)
    return G(L)->getRegistry();
  else {  /* upvalues */
    idx = LUA_REGISTRYINDEX - idx;
    api_check(L, idx <= MAXUPVAL + 1, "upvalue index too large");
    if (ttisCclosure(s2v(ci->funcRef().p))) {  /* C closure? */
      CClosure *func = clCvalue(s2v(ci->funcRef().p));
      return (idx <= func->getNumUpvalues()) ? func->getUpvalue(idx-1)
                                      : G(L)->getNilValue();
    }
    else {  /* light C function or Lua function (through a hook)?) */
      api_check(L, ttislcf(s2v(ci->funcRef().p)), "caller not a C function");
      return G(L)->getNilValue();  /* no upvalues */
    }
  }
}


/*
** Convert a valid actual index (not a pseudo-index) to its address.
**
** Replaces index2stack() from lapi.cpp.
*/
StkId LuaStack::indexToStack(lua_State* L, int idx) {
  CallInfo *ci = L->getCI();
  if (idx > 0) {
    StkId o = ci->funcRef().p + idx;
    api_check(L, o < top.p, "invalid index");
    return o;
  }
  else {    /* non-positive index */
    api_check(L, idx != 0 && -idx <= top.p - (ci->funcRef().p + 1),
                 "invalid index");
    api_check(L, !ispseudo(idx), "invalid index");
    return top.p + idx;
  }
}


/*
** ==================================================================
** API OPERATION HELPERS (Phase 94.1)
** ==================================================================
** Helper methods for Lua C API validation.
*/

/*
** Check if stack has at least n elements (replaces api_checknelems).
*/
bool LuaStack::checkHasElements(CallInfo* ci, int n) const noexcept {
  return (n) < (top.p - ci->funcRef().p);
}


/*
** Check if n elements can be popped (replaces api_checkpop).
** Also verifies no to-be-closed variables would be affected.
*/
bool LuaStack::checkCanPop(CallInfo* ci, int n) const noexcept {
  return (n) < top.p - ci->funcRef().p &&
         tbclist.p < top.p - n;
}


/*
** ==================================================================
** STACK QUERY HELPERS (Phase 94.1)
** ==================================================================
*/

/*
** Get depth relative to function base (current function's local variables).
*/
int LuaStack::getDepthFromFunc(CallInfo* ci) const noexcept {
  return cast_int(top.p - (ci->funcRef().p + 1));
}


/*
** ==================================================================
** ASSIGNMENT OPERATIONS (Phase 94.1)
** ==================================================================
** Assign values to stack slots with GC awareness.
*/

/*
** Assign to stack slot from TValue.
*/
void LuaStack::setSlot(StackValue* dest, const TValue* src) noexcept {
  *s2v(dest) = *src;
}


/*
** Copy between stack slots.
*/
void LuaStack::copySlot(StackValue* dest, StackValue* src) noexcept {
  *s2v(dest) = *s2v(src);
}


/*
** Set slot to nil.
*/
void LuaStack::setNil(StackValue* slot) noexcept {
  setnilvalue(s2v(slot));
}
