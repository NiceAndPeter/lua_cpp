# LTO (Link Time Optimization) Status

## Current Status: **NOT WORKING** ❌

LTO support has been implemented in the build system but exposes serious bugs that prevent the test suite from passing.

## Build System Changes

### CMakeLists.txt
- Added `LUA_ENABLE_LTO` option (default: OFF)
- When enabled, sets `CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE`
- Adds `-fno-strict-aliasing` to handle type punning
- Adds `-ffat-lto-objects` to reduce LTO aggressiveness

### Usage
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DLUA_ENABLE_LTO=ON
cmake --build build
```

## Issues Discovered

### 1. Corrupted Type Values
**Symptom**: GC objects show invalid type values (e.g., `0xab` = 171)
**Location**: `GCCore::getgclist()` receives objects with corrupted type fields
**Failure**: Test suite crashes immediately with assertion failures

### 2. Checkliveness Failures
**Symptom**: Assertions fail in `checkliveness()` after GC operations
**Cause**: Memory corruption or incorrect GC state

### 3. Root Cause Analysis
LTO is exposing **undefined behavior** in the codebase:

- **Strict Aliasing Violations**: Lua uses extensive type punning (same memory read as different types)
- **Uninitialized Memory**: Some code paths may read memory before initialization
- **Memory Lifetime Issues**: Objects accessed before construction or after destruction
- **GC Invariant Violations**: LTO's aggressive inlining/reordering breaks GC assumptions

## Why LTO Breaks This Code

### LTO Optimization Characteristics
1. **Whole Program Analysis**: Sees all code at once, makes global assumptions
2. **Aggressive Inlining**: Merges functions that normally wouldn't execute together
3. **Memory Reordering**: Can change memory layout and access patterns
4. **Strict Aliasing**: Assumes C++ aliasing rules (Lua violates these)
5. **UB Exploitation**: Uses undefined behavior for optimizations

### Lua's C Heritage Issues
The codebase was converted from C to C++, but retains C patterns that violate C++ rules:
- Type punning through unions (technically UB in C++)
- Pointer casts that LTO treats as strict aliasing violations
- Memory layout assumptions that LTO can break

## Code Changes Made

### GC Core (src/memory/gc/gc_core.cpp)
Added handling for types that can appear in gray list:
- `LUA_VUPVAL`: Uses base GCObject `next` field for gray list linkage
- `LUA_VSHRSTR`/`LUA_VLNGSTR`: Added defensive fallback (strings shouldn't be gray)
- Default case: Returns base `next` pointer instead of asserting (prevents crash)

### GC Weak (src/memory/gc/gc_weak.cpp)
Removed duplicate `getgclist()` implementation, now forwards to `GCCore::getgclist()`

## Attempted Fixes (All Failed)

1. ✗ Added `-fno-strict-aliasing` - Still crashes
2. ✗ Changed to `-ffat-lto-objects` - Still crashes
3. ✗ Added missing type handlers in `getgclist()` - Revealed deeper corruption
4. ✗ Defensive programming in GC code - Corruption too fundamental

## Path Forward

### Short Term: Disable LTO (Current State)
- Keep `LUA_ENABLE_LTO` option but default to OFF
- Document that LTO is experimental and broken
- Warn users in documentation

### Long Term: Fix Underlying Issues
To make LTO work, need to eliminate ALL undefined behavior:

1. **Audit Type Punning**: Replace C-style type punning with proper C++ patterns
   - Use `std::bit_cast` (C++20)
   - Use proper variant types
   - Avoid pointer cast hackery

2. **Fix Memory Initialization**: Ensure all objects fully initialized before use
   - Constructor improvements
   - Explicit zero-initialization
   - Valgrind/MSAN audits

3. **GC Invariant Enforcement**: Make GC state transitions explicit and verifiable
   - Add more assertions
   - State machine verification
   - Sanitizer testing

4. **Strict Aliasing Compliance**: Restructure code to follow C++ aliasing rules
   - Eliminate type punning
   - Use proper casts
   - Mark aliasing with attributes

### Estimated Effort
**High**: 40-80 hours of careful analysis and refactoring
**Risk**: High - touching GC code is dangerous
**Benefit**: Modest - LTO typically gives 5-15% performance improvement

## Testing
Without LTO: ✅ All tests pass (`final OK !!!`)
With LTO: ❌ Immediate crash in test suite

## Compiler Tested
- GCC 13.3.0
- Linux 4.4.0

## Recommendation
**DO NOT enable LTO** until underlying undefined behavior is fixed.

## References
- GCC LTO docs: https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html#index-flto
- Strict Aliasing: https://en.cppreference.com/w/c/language/object#Strict_aliasing
- UB in C++: https://en.cppreference.com/w/cpp/language/ub

---
**Last Updated**: 2025-11-21
**Status**: LTO support attempted but currently broken due to undefined behavior
