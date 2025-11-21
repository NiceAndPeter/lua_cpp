# ✅ HISTORICAL - LuaStack Aggressive Centralization Plan (COMPLETED)

**Status**: ✅ **COMPLETE** - Phase 94 finished (96 sites converted)
**Completion Date**: November 17, 2025
**Result**: Complete stack encapsulation, all operations through LuaStack class

---

# LuaStack Aggressive Centralization Plan

**Date**: 2025-11-17
**Original Status**: Planning Phase - AGGRESSIVE APPROACH
**Goal**: Move ALL stack responsibilities into LuaStack class

## Philosophy Change

**OLD (Conservative)**: Keep operations where they are, add convenience methods

**NEW (Aggressive)**: LuaStack owns ALL stack operations - move everything, delete old code, update all call sites

## Core Principle

**LuaStack is THE stack authority**. If it touches `top`, `stack`, `stack_last`, or `tbclist`, it belongs in LuaStack.

## Complete Inventory of Stack Operations

### Category 1: Direct Pointer Manipulation
- `top.p++` - **60+ occurrences** → LuaStack::push()
- `top.p--` - **20+ occurrences** → LuaStack::pop()
- `top.p = value` - **40+ occurrences** → LuaStack::setTopPtr()
- `top.p += n` / `top.p -= n` - **10+ occurrences** → LuaStack::adjust()

### Category 2: Index/Access Functions (lapi.cpp)
- `index2value()` - **40+ occurrences** → LuaStack::indexToValue()
- `index2stack()` - **10+ occurrences** → LuaStack::indexToStack()

### Category 3: API Macros (lapi.h)
- `api_incr_top` - **20+ occurrences** → LuaStack::pushChecked()
- `api_checknelems` - **15+ occurrences** → LuaStack::checkHasElements()
- `api_checkpop` - **10+ occurrences** → LuaStack::checkCanPop()

### Category 4: Stack Checking (ldo.h)
- `luaD_checkstack()` - **15+ occurrences** → LuaStack::ensureSpace()
- `checkstackp` - **5+ occurrences** → LuaStack::ensureSpaceP()

### Category 5: Assignment Operations (lgc.h)
- `setobj2s()` - **30+ occurrences** → LuaStack::setSlot()
- `setobjs2s()` - **10+ occurrences** → LuaStack::copySlot()

### Category 6: Stack Queries
- `stack_last.p - top.p` - **5+ occurrences** → LuaStack::getAvailable()
- `top.p - stack.p` - **5+ occurrences** → LuaStack::getDepth()
- `top.p - ci->funcRef().p` - **20+ occurrences** → LuaStack::getDepthFromFunc()

**TOTAL: 250+ call sites to migrate**

## New LuaStack Class Design

### Full Method Suite

```cpp
class LuaStack {
private:
  StkIdRel top;         /* first free slot in the stack */
  StkIdRel stack_last;  /* end of stack (last element + 1) */
  StkIdRel stack;       /* stack base */
  StkIdRel tbclist;     /* list of to-be-closed variables */

public:
  // ============================================================
  // BASIC MANIPULATION
  // ============================================================

  /* Push one slot (increment top) */
  inline void push() noexcept {
    top.p++;
  }

  /* Pop one slot (decrement top) */
  inline void pop() noexcept {
    top.p--;
  }

  /* Pop n slots */
  inline void popN(int n) noexcept {
    top.p -= n;
  }

  /* Adjust top by n (positive or negative) */
  inline void adjust(int n) noexcept {
    top.p += n;
  }

  /* Set top to specific pointer */
  inline void setTopPtr(StkId ptr) noexcept {
    top.p = ptr;
  }

  /* Set top to specific offset from stack base */
  inline void setTopOffset(int offset) noexcept {
    top.p = stack.p + offset;
  }

  // ============================================================
  // API OPERATIONS (with bounds checking)
  // ============================================================

  /* Push with bounds check (replaces api_incr_top) */
  inline void pushChecked(StkId limit) noexcept {
    top.p++;
    lua_assert(top.p <= limit);
  }

  /* Check if stack has at least n elements (replaces api_checknelems) */
  inline bool checkHasElements(CallInfo* ci, int n) const noexcept {
    return (n) < (top.p - ci->funcRef().p);
  }

  /* Check if n elements can be popped (replaces api_checkpop) */
  inline bool checkCanPop(CallInfo* ci, int n) const noexcept {
    return (n) < top.p - ci->funcRef().p &&
           tbclist.p < top.p - n;
  }

  // ============================================================
  // INDEX CONVERSION (from lapi.cpp)
  // ============================================================

  /* Convert API index to TValue* (replaces index2value) */
  TValue* indexToValue(lua_State* L, int idx);

  /* Convert API index to StkId (replaces index2stack) */
  StkId indexToStack(lua_State* L, int idx);

  // ============================================================
  // SPACE CHECKING (from ldo.h)
  // ============================================================

  /* Ensure space for n elements (replaces luaD_checkstack) */
  inline int ensureSpace(lua_State* L, int n) {
    if (l_unlikely(stack_last.p - top.p <= n)) {
      return grow(L, n, 1);
    }
#if defined(HARDSTACKTESTS)
    else {
      int sz = getSize();
      realloc(L, sz, 0);
    }
#endif
    return 1;
  }

  /* Ensure space preserving pointer (replaces checkstackp) */
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

  // ============================================================
  // ASSIGNMENT OPERATIONS (from lgc.h)
  // ============================================================

  /* Assign to stack slot from TValue (replaces setobj2s) */
  inline void setSlot(lua_State* L, StackValue* dest, const TValue* src) noexcept {
    setobj(L, s2v(dest), src);
  }

  /* Copy between stack slots (replaces setobjs2s) */
  inline void copySlot(lua_State* L, StackValue* dest, StackValue* src) noexcept {
    setobj(L, s2v(dest), s2v(src));
  }

  /* Set slot to nil */
  inline void setNil(StackValue* slot) noexcept {
    setnilvalue(s2v(slot));
  }

  // ============================================================
  // QUERIES
  // ============================================================

  /* Available space before stack_last */
  inline int getAvailable() const noexcept {
    return cast_int(stack_last.p - top.p);
  }

  /* Current depth (elements from base to top) */
  inline int getDepth() const noexcept {
    return cast_int(top.p - stack.p);
  }

  /* Depth relative to function base */
  inline int getDepthFromFunc(CallInfo* ci) const noexcept {
    return cast_int(top.p - (ci->funcRef().p + 1));
  }

  /* Check if can fit n elements */
  inline bool canFit(int n) const noexcept {
    return stack_last.p - top.p > n;
  }

  // ============================================================
  // ELEMENT ACCESS
  // ============================================================

  /* Get TValue at absolute offset from stack base */
  inline TValue* at(int offset) noexcept {
    lua_assert(offset >= 0 && stack.p + offset < top.p);
    return s2v(stack.p + offset);
  }

  /* Get TValue at offset from top (-1 = top element) */
  inline TValue* fromTop(int offset) noexcept {
    lua_assert(offset <= 0 && top.p + offset >= stack.p);
    return s2v(top.p + offset);
  }

  /* Get top-most TValue (top - 1) */
  inline TValue* topValue() noexcept {
    lua_assert(top.p > stack.p);
    return s2v(top.p - 1);
  }

  // ... existing methods (init, free, grow, realloc, etc.)
};
```

## Implementation Phases

### Phase 94.1: Add ALL Methods to LuaStack (4-6 hours)

**Add to lstack.h**:
1. Basic manipulation: push(), pop(), popN(), adjust(), setTopPtr(), setTopOffset()
2. API operations: pushChecked(), checkHasElements(), checkCanPop()
3. Space checking: ensureSpace(), ensureSpaceP()
4. Assignment: setSlot(), copySlot(), setNil()
5. Queries: getAvailable(), getDepth(), getDepthFromFunc(), canFit()
6. Element access: at(), fromTop(), topValue()

**Add to lstack.cpp**:
1. Move index2value() → indexToValue() implementation
2. Move index2stack() → indexToStack() implementation

**Build and test**: Ensure zero errors

**Commit**: "Phase 94.1: Add complete method suite to LuaStack"

### Phase 94.2: Convert lapi.cpp (3-4 hours)

**Replace ~40 call sites**:
- `index2value(L, idx)` → `L->getStackSubsystem().indexToValue(L, idx)`
- `index2stack(L, idx)` → `L->getStackSubsystem().indexToStack(L, idx)`
- `api_incr_top(L)` → `L->getStackSubsystem().pushChecked(L->getCI()->topRef().p)`

**Test after every 10 conversions**

**Commit**: "Phase 94.2: Convert lapi.cpp to use LuaStack methods"

### Phase 94.3: Convert API Macros to Inline Functions (2-3 hours)

**In lapi.h**:
```cpp
// OLD (DELETE):
#define api_incr_top(L) ...
#define api_checknelems(L,n) ...
#define api_checkpop(L,n) ...

// NEW:
inline void api_incr_top(lua_State* L) noexcept {
    L->getStackSubsystem().pushChecked(L->getCI()->topRef().p);
}

inline void api_check_nelems(lua_State* L, int n) noexcept {
    api_check(L, L->getStackSubsystem().checkHasElements(L->getCI(), n),
              "not enough elements in the stack");
}

inline void api_check_pop(lua_State* L, int n) noexcept {
    api_check(L, L->getStackSubsystem().checkCanPop(L->getCI(), n),
              "not enough free elements in the stack");
}
```

**Update all call sites** (~45 occurrences)

**Commit**: "Phase 94.3: Convert API macros to LuaStack methods"

### Phase 94.4: Convert Stack Checking (2-3 hours)

**In ldo.h**:
```cpp
// OLD (DELETE):
// #define luaD_checkstackaux(L,n,pre,pos) ...
// inline void luaD_checkstack(lua_State* L, int n) ...
// #define checkstackp(L,n,p) ...

// NEW (keep as thin wrappers):
inline void luaD_checkstack(lua_State* L, int n) noexcept {
    L->getStackSubsystem().ensureSpace(L, n);
}

#define checkstackp(L,n,p) \
    (p = L->getStackSubsystem().ensureSpaceP(L, n, p))
```

**Update ~15 call sites** to use LuaStack methods directly where possible

**Commit**: "Phase 94.4: Simplify stack checking to use LuaStack"

### Phase 94.5: Convert Assignment Operations (3-4 hours)

**In lgc.h**:
```cpp
// OLD (DELETE):
// inline void setobj2s(...) { ... }
// inline void setobjs2s(...) { ... }

// NEW (thin wrappers for compatibility):
inline void setobj2s(lua_State* L, StackValue* o1, const TValue* o2) noexcept {
    L->getStackSubsystem().setSlot(L, o1, o2);
}

inline void setobjs2s(lua_State* L, StackValue* o1, StackValue* o2) noexcept {
    L->getStackSubsystem().copySlot(L, o1, o2);
}
```

**Update ~40 call sites** to use LuaStack methods directly

**Commit**: "Phase 94.5: Move assignment operations to LuaStack"

### Phase 94.6: Mass Migration - Direct Pointer Ops (10-15 hours)

**Batch 1: lapi.cpp** (~20 sites)
- `L->getTop().p++` → `L->getStackSubsystem().push()`
- `L->getTop().p--` → `L->getStackSubsystem().pop()`
- `L->getTop().p = x` → `L->getStackSubsystem().setTopPtr(x)`

**Batch 2: ldo.cpp** (~15 sites)

**Batch 3: ldebug.cpp** (~10 sites)

**Batch 4: ltm.cpp** (~8 sites)

**Batch 5: lvm.cpp PART 1** (~20 sites - non-critical paths)

**Batch 6: lvm.cpp PART 2** (~30 sites - VM interpreter core)

**Batch 7: Other files** (~50+ sites - compiler, libraries, GC, etc.)

**Strategy**:
- Convert 10-20 sites at a time
- Build and test after each batch
- Benchmark after each major file
- Revert if excessive regression (>3%)

**Commits**: One commit per batch: "Phase 94.6.X: Convert [file] to LuaStack methods"

### Phase 94.7: Remove Old Code (1-2 hours)

**Delete from lapi.cpp**:
- static TValue* index2value() - replaced by LuaStack::indexToValue()
- static StkId index2stack() - replaced by LuaStack::indexToStack()

**Delete from lapi.h**:
- #define api_incr_top
- #define api_checknelems
- #define api_checkpop
(Keep thin inline wrappers if needed for compatibility)

**Delete from ldo.h**:
- #define luaD_checkstackaux
(Keep luaD_checkstack wrapper for external compatibility)

**Delete from lgc.h**:
- OLD implementations if fully migrated
(Keep wrappers if external code depends on them)

**Commit**: "Phase 94.7: Remove deprecated stack operation code"

### Phase 94.8: Final Cleanup & Documentation (1-2 hours)

**Update CLAUDE.md**:
- Document LuaStack complete API
- Update macro conversion stats
- Note all stack operations now in LuaStack

**Add comments to lstack.h**:
- Group methods by category
- Document each public method
- Note which old functions they replace

**Commit**: "Phase 94.8: Document complete LuaStack API"

## Migration Statistics

**Total Conversions**:
- Direct pointer ops: ~130 sites
- index2value/index2stack: ~50 sites
- API macros: ~45 sites
- Stack checking: ~20 sites
- Assignments: ~40 sites
- **GRAND TOTAL: ~285 call sites**

**Code Deletions**:
- 2 static functions (index2value, index2stack)
- 5+ macros (api_incr_top, api_checknelems, api_checkpop, luaD_checkstackaux, checkstackp)
- 2 inline functions (setobj2s, setobjs2s) - replaced by methods

**Code Additions**:
- ~25 new LuaStack methods
- Full encapsulation of stack operations

## Performance Strategy

**Critical Points**:
1. **All methods are inline** - zero function call overhead
2. **No virtual dispatch** - no vtables
3. **Same generated code** - just cleaner source
4. **Benchmark after every phase** - catch regressions early
5. **VM hot paths last** - prove performance on easier code first

**Acceptance Criteria**:
- Performance ≤4.33s (≤3% from 4.20s baseline)
- If ANY phase exceeds target, investigate and optimize
- Consider keeping some direct .p access if proven necessary

## Estimated Timeline

| Phase | Description | Hours | Risk |
|-------|-------------|-------|------|
| 94.1 | Add all methods | 4-6 | LOW |
| 94.2 | Convert lapi.cpp | 3-4 | LOW |
| 94.3 | Convert API macros | 2-3 | LOW |
| 94.4 | Convert stack checking | 2-3 | LOW-MEDIUM |
| 94.5 | Convert assignments | 3-4 | LOW-MEDIUM |
| 94.6 | Mass migration | 10-15 | MEDIUM |
| 94.7 | Remove old code | 1-2 | LOW |
| 94.8 | Documentation | 1-2 | LOW |
| **TOTAL** | **End-to-end** | **27-40** | **MEDIUM** |

## Risk Mitigation

1. **Incremental approach** - Small batches with frequent testing
2. **Benchmark early and often** - Catch performance issues immediately
3. **Commit frequently** - Easy rollback if needed
4. **VM code last** - Prove approach on easier code first
5. **Keep wrappers initially** - Easier migration, remove later

## Success Criteria

- ✅ ALL stack operations in LuaStack class
- ✅ Zero macros for stack operations (all inline functions)
- ✅ index2value/index2stack deleted from lapi.cpp
- ✅ setobj2s/setobjs2s delegating to LuaStack
- ✅ Performance ≤4.33s (≤3% from baseline)
- ✅ All tests pass (final OK !!!)
- ✅ Zero build warnings
- ✅ Complete stack encapsulation

## Differences from Conservative Plan

| Aspect | Conservative | Aggressive |
|--------|--------------|------------|
| index2value/index2stack | Keep in lapi.cpp | Move to LuaStack |
| setobj2s/setobjs2s | Keep in lgc.h | Wrap in LuaStack |
| API macros | Keep as macros | Convert to inline functions |
| Direct .p access | Keep in hot paths | Convert ALL sites |
| Migration scope | Selective (~50 sites) | Complete (~285 sites) |
| Old code | Keep wrappers | Delete after migration |
| Timeline | 5-7 hours | 27-40 hours |

## Conclusion

This aggressive plan achieves **complete stack encapsulation**:

1. **Single source of truth** - All stack ops in LuaStack
2. **Zero macros** - All inline functions for type safety
3. **Clean deletion** - Old code removed after migration
4. **Better maintainability** - One place to look for stack logic
5. **Same performance** - All inline, zero overhead

**Next Step**: Begin Phase 94.1 - Add complete method suite to LuaStack

---

**Last Updated**: 2025-11-17
**Status**: Ready for implementation
