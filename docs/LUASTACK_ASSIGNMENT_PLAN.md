# ✅ HISTORICAL - LuaStack Assignment & Manipulation Plan (COMPLETED)

**Status**: ✅ **COMPLETE** - All stack assignments centralized
**Completion Date**: November 2025
**Result**: Stack assignment operations fully encapsulated in LuaStack

---

# LuaStack Assignment & Manipulation - Integration Plan

**Date**: 2025-11-17
**Original Status**: Planning Phase
**Context**: Phase 93 completed - LuaStack class created with basic stack management

## Executive Summary

This plan details how to integrate **stack assignment, manipulation, and access operations** into the LuaStack class. Currently, stack operations are scattered across multiple files (lapi.cpp, ldo.h, lgc.h). Centralizing these in LuaStack improves encapsulation and follows the Single Responsibility Principle established in Phase 93.

## Current State Analysis

### 1. Stack Index/Access Functions (lapi.cpp)

**Location**: `src/core/lapi.cpp` (static functions)

```cpp
// Convert API index to TValue* (handles positive, negative, pseudo-indices)
static TValue *index2value (lua_State *L, int idx);

// Convert valid actual index to stack pointer
static StkId index2stack (lua_State *L, int idx);
```

**Usage**: ~40+ calls throughout lapi.cpp
**Purpose**: Convert Lua API indices (1-based, negative offsets) to internal stack pointers
**Dependencies**: CallInfo (for func position), global_State (for registry, nilvalue)

**Analysis**:
- These are **tightly coupled to lua_State and CallInfo** (need ci->func, ci->top)
- Not pure stack operations - involve registry, upvalues, pseudo-indices
- **Decision**: Keep in lapi.cpp, do NOT move to LuaStack (too much coupling)

### 2. Stack Manipulation Macros (lapi.h)

**Location**: `src/core/lapi.h`

```cpp
// Increment top with overflow check
#define api_incr_top(L)  \
    (L->getTop().p++, api_check(L, L->getTop().p <= L->getCI()->topRef().p, "stack overflow"))

// Check stack has at least n elements
#define api_checknelems(L,n) \
    api_check(L, (n) < (L->getTop().p - L->getCI()->funcRef().p), "not enough elements")

// Check stack has n elements to pop (considers to-be-closed vars)
#define api_checkpop(L,n) \
    api_check(L, (n) < L->getTop().p - L->getCI()->funcRef().p && \
                 L->getTbclist().p < L->getTop().p - (n), "not enough free elements")
```

**Usage**: Throughout lapi.cpp for API validation
**Purpose**: API boundary checks and assertions

**Analysis**:
- **api_incr_top**: Simple top++ with assertion - could be LuaStack method
- **api_checknelems/api_checkpop**: Require CallInfo state - keep as lua_State helpers
- **Decision**: Add pushChecked() method to LuaStack, keep validation macros in lapi.h

### 3. Stack Checking Functions (ldo.h)

**Location**: `src/core/ldo.h`

```cpp
// Ensure stack has space for n more elements
inline void luaD_checkstack(lua_State* L, int n) noexcept {
    if (l_unlikely(L->getStackLast().p - L->getTop().p <= n)) {
        L->growStack(n, 1);
    }
}

// Check stack with save/restore (macro)
#define luaD_checkstackaux(L,n,pre,pos)  \
    if (l_unlikely(L->getStackLast().p - L->getTop().p <= (n))) \
        { pre; (L)->growStack(n, 1); pos; } \
    else { condmovestack(L,pre,pos); }

// Check stack preserving pointer p
#define checkstackp(L,n,p)  \
    luaD_checkstackaux(L, n, \
        ptrdiff_t t__ = L->saveStack(p), \
        p = L->restoreStack(t__))
```

**Usage**: ~15+ calls throughout VM, compiler, API
**Purpose**: Ensure stack space before operations

**Analysis**:
- **luaD_checkstack**: Delegates to L->growStack() - natural fit for LuaStack
- **checkstackp**: Uses save/restore already in LuaStack
- **Decision**: Move ensureSpace() method to LuaStack, keep C API wrappers

### 4. Direct Stack Pointer Operations (scattered)

**Locations**: lapi.cpp, ldo.cpp, lvm.cpp, ldebug.cpp, ltm.cpp, and more

```cpp
// Increment top (40+ occurrences)
L->getTop().p++;
top.p++;

// Decrement top (20+ occurrences)
L->getTop().p--;
top.p--;

// Set top directly (40+ occurrences)
L->getTop().p = newvalue;
top.p = restore(offset);

// Pointer arithmetic
int size = cast_int(top.p - stack.p);
bool hasSpace = stack_last.p - top.p > n;
```

**Usage**: Very frequent (100+ occurrences)
**Purpose**: Direct stack manipulation

**Analysis**:
- **Most are in VM hot paths** (lvm.cpp) - performance critical
- Current accessor pattern (getTop().p++) is already efficient
- Adding method wrappers might harm readability without benefit
- **Decision**:
  - Add **convenience methods** for common patterns (push(), pop(), setTop())
  - Keep direct .p access for hot paths and complex operations
  - Gradually migrate non-hot-path code to methods

### 5. Stack Assignment Operations (lgc.h)

**Location**: `src/memory/lgc.h`

```cpp
// Assign to stack from TValue (with GC barrier check)
inline void setobj2s(lua_State* L, StackValue* o1, const TValue* o2) noexcept {
    setobj(L, s2v(o1), o2);
}

// Assign stack to stack
inline void setobjs2s(lua_State* L, StackValue* o1, StackValue* o2) noexcept {
    setobj(L, s2v(o1), s2v(o2));
}
```

**Usage**: ~30 occurrences across VM, GC, API
**Purpose**: Stack value assignment with GC awareness

**Analysis**:
- **Involves GC barriers** (black→white checks)
- Calls setobj() which is in GC system
- **Not pure stack operations** - GC integration
- **Decision**: Keep in lgc.h (GC responsibility), do NOT move to LuaStack

## Proposed LuaStack Additions

### Phase 94.1: Stack Manipulation Methods

Add convenience methods for common operations:

```cpp
class LuaStack {
public:
    // ... existing members ...

    /*
    ** Stack pointer manipulation methods
    */

    /* Increment top pointer (assumes space checked) */
    inline void push() noexcept {
        top.p++;
    }

    /* Decrement top pointer */
    inline void pop() noexcept {
        top.p--;
    }

    /* Pop n elements from stack */
    inline void popN(int n) noexcept {
        top.p -= n;
    }

    /* Set top to specific pointer value */
    inline void setTopPtr(StkId newTop) noexcept {
        top.p = newTop;
    }

    /* Increment top with bounds check (for API) */
    inline void pushChecked(lua_State* L, StkId limit) noexcept {
        top.p++;
        lua_assert(top.p <= limit);  // In debug builds
    }

    /* Get distance from stack base to top (in elements) */
    inline int getDepth() const noexcept {
        return cast_int(top.p - stack.p);
    }

    /* Get available space (how many elements fit before stack_last) */
    inline int getAvailable() const noexcept {
        return cast_int(stack_last.p - top.p);
    }

    /* Check if there is space for n elements (does not grow) */
    inline bool canFit(int n) const noexcept {
        return stack_last.p - top.p > n;
    }

    /* Ensure space for n elements (grows if needed) */
    int ensureSpace(lua_State* L, int n, int raiseerror = 1);
};
```

**Benefits**:
- Clearer intent (push() vs top.p++)
- Centralized bounds checking (pushChecked)
- Easier to add instrumentation/debugging later
- Still inline = zero overhead

**Migration Strategy**:
- Add methods to LuaStack
- Gradually convert call sites (non-hot paths first)
- Keep direct .p access in VM hot paths
- Benchmark after each batch

### Phase 94.2: Stack Checking Integration

Move luaD_checkstack logic into LuaStack:

```cpp
// In lstack.h
class LuaStack {
public:
    /* Ensure space for n elements, growing stack if necessary */
    inline int ensureSpace(lua_State* L, int n, int raiseerror = 1) {
        if (l_unlikely(stack_last.p - top.p <= n)) {
            return grow(L, n, raiseerror);
        }
#if defined(HARDSTACKTESTS)
        else {
            int sz = getSize();
            realloc(L, sz, 0);  // Test stack movement
        }
#endif
        return 1;  // Success
    }

    /* Ensure space preserving a pointer (returns new pointer) */
    template<typename T>
    inline T* ensureSpaceP(lua_State* L, int n, T* ptr) {
        if (l_unlikely(stack_last.p - top.p <= n)) {
            ptrdiff_t offset = save(reinterpret_cast<StkId>(ptr));
            grow(L, n, 1);
            return reinterpret_cast<T*>(restore(offset));
        }
#if defined(HARDSTACKTESTS)
        else {
            ptrdiff_t offset = save(reinterpret_cast<StkId>(ptr));
            int sz = getSize();
            realloc(L, sz, 0);
            return reinterpret_cast<T*>(restore(offset));
        }
#endif
        return ptr;
    }
};

// In ldo.h - keep C API wrappers
inline void luaD_checkstack(lua_State* L, int n) noexcept {
    L->getStackSubsystem().ensureSpace(L, n);
}

#define checkstackp(L,n,p) \
    (p = L->getStackSubsystem().ensureSpaceP(L, n, p))
```

**Benefits**:
- Stack checking logic lives in LuaStack
- Maintains existing API
- Template method for type-safe pointer preservation

### Phase 94.3: Stack Access Helpers (Optional)

Add optional convenience methods for stack element access:

```cpp
class LuaStack {
public:
    /* Get TValue at offset from stack base (0-indexed) */
    inline TValue* at(int offset) noexcept {
        lua_assert(offset >= 0 && stack.p + offset < top.p);
        return s2v(stack.p + offset);
    }

    /* Get TValue at offset from top (-1 = top-most element) */
    inline TValue* fromTop(int offset) noexcept {
        lua_assert(offset < 0 && top.p + offset >= stack.p);
        return s2v(top.p + offset);
    }

    /* Get pointer to top-most element (top - 1) */
    inline TValue* topValue() noexcept {
        lua_assert(top.p > stack.p);
        return s2v(top.p - 1);
    }
};
```

**Analysis**:
- Nice for readability: `stack.at(5)` vs `s2v(L->getStack().p + 5)`
- But adds another layer of indirection conceptually
- **Decision**: Optional phase, evaluate need after Phase 94.1-94.2

## What NOT to Move

### Keep in Current Locations

1. **index2value() / index2stack()** (lapi.cpp)
   - Reason: Tightly coupled to CallInfo, pseudo-indices, registry
   - Not pure stack operations

2. **setobj2s() / setobjs2s()** (lgc.h)
   - Reason: Part of GC barrier system
   - Stack is just the destination, GC is the concern

3. **api_checknelems / api_checkpop** (lapi.h)
   - Reason: API-specific validation involving CallInfo
   - Keep at API boundary

4. **Most direct .p access in VM** (lvm.cpp)
   - Reason: Hot path performance, complex expressions
   - Keep direct access for clarity and performance

## Implementation Phases

### Phase 94.1: Basic Manipulation Methods ⏳

**Estimated Time**: 2-3 hours
**Risk**: LOW

1. Add methods to LuaStack class (lstack.h):
   - push(), pop(), popN()
   - setTopPtr()
   - getDepth(), getAvailable(), canFit()

2. Build and test (ensure zero errors)

3. Benchmark (ensure ≤4.33s)

4. Commit: "Phase 94.1: Add stack manipulation methods to LuaStack"

### Phase 94.2: Stack Space Checking ⏳

**Estimated Time**: 3-4 hours
**Risk**: LOW-MEDIUM

1. Add ensureSpace() method to LuaStack

2. Add ensureSpaceP() template method

3. Update luaD_checkstack() in ldo.h to use ensureSpace()

4. Update checkstackp macro to use ensureSpaceP()

5. Build and test

6. Benchmark (critical - stack checking is on hot paths)

7. Commit: "Phase 94.2: Move stack checking to LuaStack::ensureSpace()"

### Phase 94.3: Gradual Migration (Optional) ⏳

**Estimated Time**: 10-15 hours
**Risk**: LOW

1. Identify non-hot-path top.p++ → stack.push()

2. Identify non-hot-path top.p-- → stack.pop()

3. Convert in batches of 10-20 sites

4. Build, test, benchmark after each batch

5. Commit after each batch

**Note**: This phase is optional and should be evaluated based on actual readability/maintainability benefits vs. migration cost.

## Performance Constraints

**Critical**: Maintain performance target ≤4.33s (≤3% from 4.20s baseline)

**Hot Paths to Watch**:
- lvm.cpp (bytecode interpreter) - most critical
- ldo.cpp (call/return) - very critical
- luaD_checkstack() - called frequently
- Stack pointer arithmetic in VM

**Mitigation**:
- All new methods are inline
- No virtual dispatch (no vtable overhead)
- Keep direct .p access where needed
- Benchmark after every phase

## Success Criteria

- ✅ All methods inline (zero-cost abstraction)
- ✅ Performance ≤4.33s (≤3% regression)
- ✅ All tests pass (final OK !!!)
- ✅ Zero build warnings
- ✅ Improved code clarity (where methods used)
- ✅ No C API breakage

## Open Questions

1. **Should we migrate all top.p++ to push()?**
   - Pro: Clearer intent, easier debugging
   - Con: 100+ call sites, limited benefit in hot paths
   - **Recommendation**: Migrate selectively (non-hot paths)

2. **Should we add at()/fromTop() helpers?**
   - Pro: More readable
   - Con: Adds conceptual layer
   - **Recommendation**: Defer until proven need

3. **Should we wrap all stack operations eventually?**
   - Pro: Complete encapsulation
   - Con: May hurt VM readability
   - **Recommendation**: Pragmatic approach - wrap where it helps

## Related Work

- **Phase 93**: LuaStack class creation (COMPLETED ✅)
- **Phase 90**: FuncState SRP refactoring
- **Phase 91**: global_State SRP refactoring
- **Phase 92**: Proto SRP refactoring

## Conclusion

**Recommendation**: Proceed with Phase 94.1 and 94.2

These phases add useful convenience methods without major code churn, maintain performance, and improve LuaStack's API. Phase 94.3 (migration) should be evaluated later based on demonstrated value.

**Key Principle**: LuaStack should own **stack structure operations** (grow, shrink, check space, push/pop). It should NOT own **GC operations** (barriers) or **API operations** (index translation, validation).

---

**Last Updated**: 2025-11-17
**Next Step**: Review plan with user, then proceed with Phase 94.1
