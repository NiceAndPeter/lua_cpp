# Phase 135: StackValue Simplification - Failed Attempt Analysis

**Date**: 2025-12-05
**Status**: ❌ FAILED - Reverted to clean state
**Duration**: ~2 hours investigation + implementation attempt

---

## Objective

**Goal**: Simplify `union StackValue` to `typedef TValue StackValue` by moving the `delta` field to a parallel array in `lua_State`.

**Motivation**:
- Eliminate the StackValue/TValue union complexity
- Remove the `s2v()` macro conversion (249 call sites)
- Cleaner type system (StackValue = TValue directly)
- Better separation of concerns (delta is state metadata, not stack data)

**Trade-off**: +2 bytes per stack slot (parallel array for deltas)

---

## What Was Attempted

### 1. Architecture Change
```cpp
// BEFORE (union approach):
union StackValue {
  TValue val;
  struct {
    TValuefields;
    unsigned short delta;  // only for to-be-closed variables
  } tbclist;
};

// AFTER (parallel array approach):
typedef TValue StackValue;  // No union, just an alias!

// In LuaStack class:
class LuaStack {
  StackValue* stack;
  unsigned short* tbc_deltas;  // Parallel array for delta values
};
```

### 2. Implementation Steps Completed

✅ **Step 1**: Added `tbc_deltas` parallel array to `LuaStack` class (lstack.h)
- Added field: `unsigned short* tbc_deltas`
- Added accessors: `getTbcDelta(slot)`, `setTbcDelta(slot, delta)`

✅ **Step 2**: Updated stack allocation to manage both arrays (lstack.cpp)
- `LuaStack::init()`: Allocate both arrays, initialize deltas to zero
- `LuaStack::free()`: Free both arrays
- `LuaStack::realloc()`: Reallocate both arrays in parallel

✅ **Step 3**: Simplified StackValue definition (lobject.h)
- Changed from `union StackValue` to `typedef TValue StackValue`
- Removed `s2v()` macro

✅ **Step 4**: Removed all 249 `s2v()` calls across 18 files
- Used automated script: `s2v(expr)` → `expr`

✅ **Step 5**: Updated 5 delta access sites (lfunc.cpp)
- Changed `->tbclist.delta` to `L->getStackSubsystem().getTbcDelta(slot)`

---

## Why It Failed

### Root Cause: Memory Allocation Complexity

The failure occurred in the **parallel array reallocation logic** in `LuaStack::realloc()`.

#### Problem 1: Dual Allocation Failure Modes

```cpp
// In LuaStack::realloc():
newstack = luaM_reallocvector<StackValue>(L, oldstack, oldsize, newsize);
// ☝️ This THROWS on failure (via luaM_realloc_)

newdeltas = luaM_reallocvector<unsigned short>(L, olddeltas, oldsize, newsize);
// ☝️ This also THROWS on failure
```

**Issue**: If stack allocation succeeds but deltas allocation throws:
- `oldstack` is already freed (reallocated)
- `stack.p` still points to freed memory
- When exception propagates, pointer is invalid
- Memory corruption ensues

#### Problem 2: Test Framework Assertions

The test framework (`ltests.cpp`) tracks all memory allocations with a header:

```cpp
void *debug_realloc(void *ud, void *b, size_t oldsize, size_t size) {
  memHeader *block = static_cast<memHeader*>(b);
  block--;  // go to real header
  lua_assert(oldsize == block->d.size);  // ❌ ASSERTION FAILED HERE
}
```

**Why it failed**: The parallel array allocation caused a mismatch between:
- What the test framework thinks the size is (tracked in header)
- What we're telling it the size is (oldsize parameter)

This suggests our size tracking for the `tbc_deltas` array was incorrect during reallocation.

#### Problem 3: Failed Fix Attempts

**Attempt 1**: Complex error handling for partial failures
```cpp
if (newstack != nullptr && newdeltas == nullptr) {
  // Stack succeeded, deltas failed
  // Try to free old deltas... but creates size mismatch
}
```
Result: Memory leak and size tracking bugs ❌

**Attempt 2**: Use `luaM_saferealloc_` for deltas (returns nullptr instead of throwing)
```cpp
newdeltas = cast(unsigned short*, luaM_saferealloc_(L, olddeltas, ...));
if (newdeltas == nullptr) {
  // But if we return early, we leak newstack!
}
```
Result: Different failure mode - stack allocated but not tracked ❌

**Attempt 3**: Allocate deltas FIRST, then stack
```cpp
newdeltas = luaM_saferealloc_(...);  // Safe, doesn't throw
if (newdeltas == nullptr) return 0;

newstack = luaM_reallocvector(...);  // May throw, but deltas already allocated
```
Result: Tests crashed even earlier (in api.lua, not memerr.lua), suggesting deeper issues ❌

---

## Technical Deep Dive

### The Memory Allocation Trap

Lua's memory allocator has two modes:

1. **`luaM_realloc_()`**: Throws `LUA_ERRMEM` on failure (cannot return nullptr)
2. **`luaM_saferealloc_()`**: Returns nullptr on failure (for emergency allocation)

**The trap**: You cannot mix these safely for parallel arrays because:
- If both throw: Fine, exception propagates, nothing changes
- If first throws: Fine, second never runs
- If first succeeds, second throws: **💥 CORRUPTION** - first allocation freed old memory but we never updated pointers

### Why The Test Failed

The `memerr.lua` test deliberately causes allocation failures to test error handling:

```lua
-- memerr.lua
collectgarbage("stop")
for i = 1, 1000 do
  -- Cause allocation failure on iteration N
  -- Lua must handle it gracefully
end
```

When the deltas array allocation failed:
1. Stack was reallocated successfully → `oldstack` freed
2. Deltas allocation failed → tried to free `olddeltas` with wrong size
3. Memory allocator assertion: "You said oldsize was X, but I have Y in my header"

### Size Tracking Bug

The bug was in handling null `tbc_deltas`:

```cpp
// If tbc_deltas was nullptr from a previous failure:
int old_deltas_size = (olddeltas != nullptr) ? (oldsize + EXTRA_STACK) : 0;

// But luaM_reallocvector expects:
// - Non-null pointer → oldsize is the ACTUAL size we allocated
// - Null pointer → oldsize MUST be 0 (nothing to free)

// Passing 0 when pointer is nullptr is correct...
// BUT if deltas wasn't nullptr, we need to track its actual size!
// Problem: What if stack size changed but deltas allocation failed?
// Then deltas size != stack size, and we lose track.
```

This creates an **invariant violation**: The parallel arrays must ALWAYS be the same size, but partial allocation failures break this invariant.

---

## Lessons Learned

### 1. Parallel Arrays Are Hard

Managing two parallel arrays with the same lifecycle is complex when:
- Allocations can fail independently
- Memory tracking is external (test framework)
- Error handling must be exception-safe
- Size invariants must be maintained across failures

**Alternative**: Single allocation for both arrays (struct of arrays approach), but this changes the layout significantly.

### 2. Don't Mix Throwing and Non-Throwing Allocators

Using `luaM_realloc_` for one array and `luaM_saferealloc_` for the other creates:
- Asymmetric error handling
- Memory leak opportunities
- Pointer invalidation bugs

**Better**: Either both throw or both return nullptr (consistent failure mode).

### 3. s2v() Removal Was Premature

Removing all 249 `s2v()` calls with a script before verifying the core allocation logic was stable meant:
- Hard to test incrementally
- Large surface area for bugs
- Difficult to isolate issues

**Better**: Test core allocation first, then remove s2v() calls incrementally.

### 4. Test Framework Is Strict

The debug allocator in `ltests.cpp` tracks EVERY allocation with a header:
- Size must match exactly
- Free must provide correct oldsize
- Realloc must provide correct oldsize

This is excellent for catching bugs but means our implementation must be **perfect** in its size bookkeeping.

---

## Why This Phase Is Not Worth Pursuing

### Cost-Benefit Analysis

**Costs**:
1. **High complexity**: Parallel array management with exception-safe allocation
2. **Fragile invariants**: Arrays must always be same size, hard to maintain
3. **Memory overhead**: +2 bytes per stack slot (12.5% increase)
4. **Testing burden**: Must handle all failure modes correctly
5. **Implementation time**: ~6-10 hours for robust solution

**Benefits**:
1. ~~Remove s2v() macro~~ (Macro is trivial: `#define s2v(o) ((TValue*)(o))`)
2. ~~Cleaner type system~~ (Union is well-understood, not a real problem)
3. ~~Separation of concerns~~ (Delta field is only used for TBC variables, rare case)

**Verdict**: ❌ **Not worth it** - Costs significantly outweigh benefits.

### The Union Approach Is Actually Good

The original `union StackValue` design is clever:
- **Zero memory overhead** for normal values (no TBC)
- **Simple allocation** (single array)
- **No parallel bookkeeping** needed
- **Exception-safe** by default (single allocation point)
- **Well-tested** (existed in Lua 5.4+)

The `s2v()` macro is a tiny price to pay for this simplicity:
```cpp
#define s2v(o) ((TValue*)(o))  // Just a cast, zero runtime cost
```

---

## Recommendations

### What To Do Instead

1. **Leave StackValue as is** - The union design is sound
2. **Keep s2v() macro** - It's clear, zero-cost, well-understood
3. **Focus on higher-value work**:
   - ✅ Identifier modernization (Phases 131, 133, 134) - DONE
   - ✅ Pointer-to-reference conversions (Phase 130) - DONE
   - 🔲 Range-based for loops (Phase 129 Part 2) - Low risk, clear benefit
   - 🔲 Additional const correctness improvements
   - 🔲 More [[nodiscard]] annotations (found 5 bugs already!)

### If You Really Must Proceed

If someone insists on pursuing this, here's what would be needed:

1. **Single allocation approach**:
   ```cpp
   struct StackData {
     StackValue values[N];
     unsigned short deltas[N];
   };
   // Allocate as one block, split into two arrays
   ```

2. **Atomic reallocation**:
   - Allocate new block (both arrays)
   - Copy old data
   - Free old block (both arrays)
   - Update pointers atomically
   - No partial failure states possible

3. **Extensive testing**:
   - All memerr.lua scenarios
   - ASAN/UBSAN clean
   - Performance benchmarks (ensure no regression)
   - Edge cases (zero size, max size, etc.)

**Estimated effort**: 2-3 days for robust implementation + testing

---

## Files Affected (Before Revert)

### Core Changes:
- `src/core/lstack.h` - Added tbc_deltas field + accessors
- `src/core/lstack.cpp` - Modified init/free/realloc
- `src/core/lstate.h` - Init tbc_deltas to nullptr
- `src/objects/lobject.h` - Changed to typedef, removed s2v()
- `src/objects/ltable.h` - Updated forward declaration
- `src/objects/lfunc.h` - Updated forward declaration
- `src/objects/lfunc.cpp` - Updated 5 delta accesses

### s2v() Removal (249 sites across 18 files):
- src/memory/gc/gc_marking.cpp (2)
- src/vm/lvirtualmachine.cpp (87)
- src/vm/lvm_loops.cpp (13)
- src/core/ldebug.cpp (15)
- src/core/lstate.h (1)
- src/core/lstate.cpp (2)
- src/core/ltm.cpp (9)
- src/core/ldo.cpp (21)
- src/core/lstack.h (3)
- src/core/lstack.cpp (12)
- src/core/lapi.cpp (56)
- src/compiler/llex.cpp (1)
- src/testing/ltests.cpp (8)
- src/objects/lobject.cpp (2)
- src/objects/lfunc.cpp (3)
- src/objects/ltable.cpp (5)

### All Changes Reverted:
```bash
git reset --hard HEAD  # Restored commit 15c34511
```

---

## Current Status (After Revert)

### ✅ System Restored

**Commit**: `15c34511` ("Some enum cleanup")
**Tests**: All passing ("final OK !!!")
**Performance**: 2.19s avg (5 runs: 2.18, 2.13, 2.14, 2.24, 2.27)
**Baseline**: ~2.11s (within 4% - acceptable variance)
**Target**: ≤4.33s (well exceeded) ✅

### Working Directory Clean
```bash
$ git status
On branch main
nothing to commit, working tree clean
```

### Phases Complete
- ✅ Phases 1-127: Core modernization
- ✅ Phase 129-1: Range-based for loops (Part 1)
- ✅ Phase 130 (ALL 6 parts): Pointer-to-reference conversions
- ✅ Phase 131: Identifier modernization (Quick Wins)
- ✅ Phase 133: Compiler expression variables
- ✅ Phase 134: VM dispatch lambda names
- ❌ Phase 135: StackValue simplification (ABANDONED)

---

## Conclusion

**Phase 135 (StackValue Simplification) is NOT RECOMMENDED** for the following reasons:

1. ❌ High complexity with parallel array management
2. ❌ Exception-safety challenges with dual allocations
3. ❌ Memory overhead (+12.5% per stack slot)
4. ❌ Fragile invariants (size tracking across failures)
5. ❌ Low actual benefit (s2v macro is trivial)
6. ✅ **Union approach is better** (simple, zero overhead, well-tested)

**Final Recommendation**: **Mark as "considered but rejected"** and move on to higher-value work.

The current codebase is in excellent shape with 50% performance improvement over baseline. Focus on incremental improvements rather than risky architectural changes.

---

**Document Version**: 1.0
**Last Updated**: 2025-12-05
**Status**: Archived - Do Not Pursue
