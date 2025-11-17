/*
** $Id: lstack.h $
** Lua Stack Management
** See Copyright Notice in lua.h
*/

#ifndef lstack_h
#define lstack_h

#include "lobject.h"
#include "llimits.h"

/*
** Forward declarations
*/
class lua_State;
class CallInfo;
class UpVal;

/*
** LuaStack - Stack management subsystem for lua_State
**
** RESPONSIBILITY:
** This class encapsulates all stack-related operations for a Lua thread.
** It manages the dynamic stack that holds Lua values during execution.
**
** DESIGN:
** - Single Responsibility: Handles ONLY stack management
** - Zero-cost abstraction: All accessors are inline
** - Private fields: Full encapsulation with accessor methods
** - Owned by lua_State: lua_State delegates stack operations to this subsystem
**
** STACK STRUCTURE:
** The Lua stack is a dynamically-sized array of StackValue slots:
**
**   stack.p ───────┬─────────────┐
**                  │ slot 0      │ (function being called)
**                  ├─────────────┤
**                  │ slot 1      │ (first argument/local)
**                  ├─────────────┤
**                  │ ...         │
**                  ├─────────────┤
**   top.p ─────────┤             │ (first free slot)
**                  ├─────────────┤
**                  │ ...         │ (available space)
**                  ├─────────────┤
**   stack_last.p ──┤             │ (end of usable stack)
**                  ├─────────────┤
**                  │ EXTRA_STACK │ (reserved for error handling)
**                  └─────────────┘
**
** DYNAMIC REALLOCATION:
** The stack grows automatically when more space is needed. During reallocation,
** ALL pointers into the stack become invalid and must be adjusted.
**
** POINTER PRESERVATION:
** Use save()/restore() to convert pointers to offsets before reallocation,
** then convert back to pointers after reallocation.
**
** TO-BE-CLOSED VARIABLES:
** The tbclist field tracks variables that need cleanup (__close metamethod)
** when they go out of scope.
*/
class LuaStack {
private:
  StkIdRel top;         /* first free slot in the stack */
  StkIdRel stack_last;  /* end of stack (last element + 1) */
  StkIdRel stack;       /* stack base */
  StkIdRel tbclist;     /* list of to-be-closed variables */

public:
  /*
  ** Field accessors - return references to allow .p and .offset access
  */

  /* Top pointer accessors */
  inline StkIdRel& getTop() noexcept { return top; }
  inline const StkIdRel& getTop() const noexcept { return top; }
  inline void setTop(StkIdRel t) noexcept { top = t; }

  /* Stack base pointer accessors */
  inline StkIdRel& getStack() noexcept { return stack; }
  inline const StkIdRel& getStack() const noexcept { return stack; }
  inline void setStack(StkIdRel s) noexcept { stack = s; }

  /* Stack limit pointer accessors */
  inline StkIdRel& getStackLast() noexcept { return stack_last; }
  inline const StkIdRel& getStackLast() const noexcept { return stack_last; }
  inline void setStackLast(StkIdRel sl) noexcept { stack_last = sl; }

  /* To-be-closed list pointer accessors */
  inline StkIdRel& getTbclist() noexcept { return tbclist; }
  inline const StkIdRel& getTbclist() const noexcept { return tbclist; }
  inline void setTbclist(StkIdRel tbc) noexcept { tbclist = tbc; }

  /*
  ** Computed properties
  */

  /* Get current stack size (number of usable slots) */
  inline int getSize() const noexcept {
    return cast_int(stack_last.p - stack.p);
  }

  /* Check if there is space for n more elements */
  inline bool hasSpace(int n) const noexcept {
    return stack_last.p - top.p > n;
  }

  /*
  ** Pointer preservation methods
  **
  ** These methods convert stack pointers to/from offsets, allowing them
  ** to survive stack reallocation. Always use these before/after reallocating.
  */

  /* Convert stack pointer to offset from base */
  inline ptrdiff_t save(StkId pt) const noexcept {
    return cast_charp(pt) - cast_charp(stack.p);
  }

  /* Convert offset to stack pointer */
  inline StkId restore(ptrdiff_t n) const noexcept {
    return cast(StkId, cast_charp(stack.p) + n);
  }

  /*
  ** Stack operation methods
  ** Implemented in lstack.cpp
  */

  /* Increment top with stack check */
  void incTop(lua_State* L);

  /* Shrink stack to reasonable size */
  void shrink(lua_State* L);

  /* Grow stack by at least n elements */
  int grow(lua_State* L, int n, int raiseerror);

  /* Reallocate stack to exact size */
  int realloc(lua_State* L, int newsize, int raiseerror);

  /* Calculate how much of the stack is currently in use */
  int inUse(const lua_State* L) const;

  /*
  ** Stack initialization and cleanup
  */

  /* Initialize a new stack (allocates memory) */
  void init(lua_State* L);

  /* Free stack memory */
  void free(lua_State* L);

  /*
  ** Pointer adjustment for reallocation
  ** These are called internally by realloc()
  */

  /* Convert all stack pointers to offsets (before realloc) */
  void relPointers(lua_State* L);

  /* Convert all offsets back to pointers (after realloc) */
  void correctPointers(lua_State* L, StkId oldstack);
};


#endif
