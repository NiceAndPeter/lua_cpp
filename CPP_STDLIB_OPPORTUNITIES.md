# C++ Standard Library Opportunities

**Analysis Date**: 2025-11-16
**Status**: Opportunities identified, implementation pending
**Performance Constraint**: ≤4.24s (≤1% regression from 4.20s baseline)

---

## Overview

This document identifies opportunities to replace C standard library usage with modern C++23 standard library equivalents. The project is already using C++23, but many files still use C headers and functions that have better C++ alternatives.

**Key Benefits**:
- Type safety (no void* casts)
- Exception safety (RAII)
- Better optimization potential
- Clearer intent
- Compiler diagnostics
- Zero-cost abstractions

**Key Constraints**:
- Must not break performance (≤1% regression)
- Must preserve C API compatibility (public headers unchanged)
- Must integrate with existing Lua allocator system
- Hot-path code requires careful benchmarking

---

## Priority 1: Low-Hanging Fruit (HIGH CONFIDENCE)

### 1.1 C Headers → C++ Headers

**Effort**: LOW (1-2 hours)
**Risk**: VERY LOW
**Performance Impact**: None (identical semantics)

**Current State**: Many files use C headers instead of C++ equivalents.

**Changes**:
```cpp
// Replace these C headers:
#include <string.h>   → #include <cstring>
#include <stdlib.h>   → #include <cstdlib>
#include <stdio.h>    → #include <cstdio>
#include <math.h>     → #include <cmath>
#include <limits.h>   → #include <climits>
#include <float.h>    → #include <cfloat>
#include <stddef.h>   → #include <cstddef>
#include <stdarg.h>   → #include <cstdarg>
#include <ctype.h>    → #include <cctype>
#include <locale.h>   → #include <clocale>
#include <assert.h>   → #include <cassert>
```

**Files Affected** (~40 files):
- src/vm/lvm.cpp
- src/objects/ltable.cpp
- src/objects/lstring.cpp
- src/objects/lobject.cpp
- src/objects/lfunc.cpp
- src/serialization/lundump.cpp
- src/serialization/ldump.cpp
- src/serialization/lzio.cpp
- src/testing/ltests.cpp
- src/auxiliary/lauxlib.cpp
- src/libraries/*.cpp (all library files)
- And more...

**Implementation**:
1. Search and replace in all .cpp files
2. Update corresponding .h files if needed
3. Keep public API headers (lua.h, lauxlib.h) unchanged
4. Build and test
5. Benchmark

**Expected Result**: Zero functional change, cleaner code.

---

### 1.2 INT_MAX/UINT_MAX → std::numeric_limits

**Effort**: LOW (2-3 hours)
**Risk**: LOW
**Performance Impact**: None (compile-time constants)

**Current State**: Code uses INT_MAX, UINT_MAX, SIZE_MAX macros from limits.h.

**Found in**:
- src/objects/ltable.cpp:208, 218-239
- src/compiler/lparser.cpp
- src/compiler/lcode.cpp
- src/libraries/loslib.cpp
- src/libraries/lstrlib.cpp
- src/libraries/ltablib.cpp
- src/libraries/lutf8lib.cpp
- src/objects/lstring.cpp
- src/serialization/lundump.cpp
- And more...

**Changes**:
```cpp
// Before
#include <limits.h>
if (ui <= cast_uint(INT_MAX))

// After
#include <limits>
if (ui <= std::numeric_limits<int>::max())
```

**Additional Benefits**:
```cpp
// Type-safe, constexpr, works for any type
std::numeric_limits<int>::min()
std::numeric_limits<int>::max()
std::numeric_limits<unsigned>::max()
std::numeric_limits<size_t>::max()
std::numeric_limits<lua_Integer>::max()
std::numeric_limits<lua_Number>::epsilon()
std::numeric_limits<lua_Number>::infinity()
std::numeric_limits<lua_Number>::quiet_NaN()
```

**Implementation**:
1. Add `#include <limits>` to llimits.h
2. Create inline constexpr helpers if needed
3. Replace macros systematically
4. Build and test
5. Benchmark

---

### 1.3 memcpy(dest, src, n * sizeof(char)) → std::copy_n

**Effort**: MEDIUM (4-6 hours)
**Risk**: MEDIUM (need to verify all uses)
**Performance Impact**: None (compiles to same code)

**Current State**: 15+ instances of `memcpy` copying characters, often with `sizeof(char)`.

**Found in**:
- src/objects/lstring.cpp:274, 293 (string creation)
- src/vm/lvm.cpp:706 (concatenation)
- src/auxiliary/lauxlib.cpp:581, 600, 655 (buffer operations)
- src/objects/lobject.cpp:681, 689, 697, 701, 718 (formatting)
- src/libraries/lstrlib.cpp:154, 156, 159 (string library)
- src/testing/ltests.cpp:1474

**Changes**:
```cpp
// Before (from lstring.cpp:274)
memcpy(getshrstr(ts), str, l * sizeof(char));

// After
std::copy_n(str, l, getshrstr(ts));

// Before (from ltable.cpp:656) - copying Value objects
memcpy(np - tomove, op - tomove, tomoveb);

// After - need std::copy for non-trivial types
std::copy(op - tomove, op, np - tomove);
```

**Benefits**:
- Type-safe (no sizeof errors)
- Works with non-trivial types
- Iterator-based (more C++ idiomatic)
- Better compiler diagnostics

**Implementation Strategy**:
1. Start with char* copies (safest)
2. Use `std::copy_n(src, count, dest)` for simple cases
3. Use `std::copy(begin, end, dest)` for range copies
4. Benchmark each file after changes
5. Consider `std::memcpy` wrapper if needed for POD types

---

## Priority 2: Medium Opportunities (GOOD CANDIDATES)

### 2.1 memcpy (general) → std::copy / std::memcpy

**Effort**: MEDIUM (3-4 hours)
**Risk**: MEDIUM
**Performance Impact**: Should be identical for POD types

**Current State**:
- ltable.cpp:656 - copying Value array
- Additional instances in various files

**Changes**:
```cpp
// For POD types, use namespaced version
std::memcpy(dest, src, size);

// For arrays of objects, use algorithms
std::copy(first, last, dest);
std::copy_n(first, count, dest);
std::move(first, last, dest);  // for move-capable types
```

**Implementation**:
1. Categorize uses by type (POD vs objects)
2. Replace with appropriate algorithm
3. Benchmark hot paths carefully
4. Keep memcpy for truly performance-critical code

---

### 2.2 Manual Loops → std::algorithms

**Effort**: MEDIUM (varies by case)
**Risk**: LOW
**Performance Impact**: Usually identical or better

**Potential Candidates** (need to search):
- Loops that could use `std::find`, `std::find_if`
- Loops that could use `std::count`, `std::count_if`
- Loops that could use `std::transform`
- Loops that could use `std::accumulate`
- Loops that could use `std::all_of`, `std::any_of`, `std::none_of`

**Example Pattern**:
```cpp
// Before
int count = 0;
for (int i = 0; i < n; i++) {
    if (predicate(array[i])) count++;
}

// After
int count = std::count_if(array, array + n, predicate);
```

**Implementation**:
1. Search for common loop patterns
2. Identify algorithm replacements
3. Refactor incrementally
4. Benchmark

---

### 2.3 String Operations → std::string_view (read-only)

**Effort**: MEDIUM-HIGH (6-10 hours)
**Risk**: MEDIUM
**Performance Impact**: Potentially better (no copies)

**Current State**: Many functions take `const char*` + `size_t` pairs.

**Potential Benefits**:
- Single parameter instead of two
- No null-termination requirement
- Substring operations without allocation
- Standard string algorithms

**Example**:
```cpp
// Before
void processString(const char* str, size_t len);

// After
void processString(std::string_view str);

// Call sites unchanged if using C++17 deduction
processString({str, len});
```

**Caution**:
- Need to ensure Lua strings are valid for lifetime
- Public API must remain unchanged
- May not be suitable for all cases

**Implementation**:
1. Start with internal utility functions
2. Gradually expand to more code
3. Never change public API
4. Benchmark carefully

---

## Priority 3: Advanced Opportunities (CONSIDER CAREFULLY)

### 3.1 std::array for Fixed-Size Arrays

**Effort**: MEDIUM
**Risk**: LOW-MEDIUM
**Performance Impact**: None (same layout as C array)

**Benefits**:
- Bounds checking in debug mode
- Size information embedded
- Standard container interface
- Works with algorithms

**Candidates** (need to identify):
- Fixed-size stack buffers
- Character buffers for formatting
- Small lookup tables

**Example**:
```cpp
// Before
char buff[100];

// After
std::array<char, 100> buff;
// OR
constexpr size_t BUFF_SIZE = 100;
std::array<char, BUFF_SIZE> buff;
```

**Implementation**:
1. Search for fixed-size array declarations
2. Evaluate case-by-case
3. Replace where beneficial
4. Keep C arrays for hot paths or C compatibility

---

### 3.2 std::optional for Nullable Returns

**Effort**: MEDIUM-HIGH
**Risk**: MEDIUM
**Performance Impact**: Usually none (same size as T* for pointers)

**Benefits**:
- Explicit nullability
- Type-safe
- Monadic operations (transform, and_then, or_else)
- No sentinel values

**Current Patterns** (need to identify):
- Functions returning nullptr on failure
- Functions using -1/0 as sentinel
- Out-parameters for optional results

**Example**:
```cpp
// Before
TString* findString(const char* str, size_t len) {
    // returns nullptr if not found
}

// After
std::optional<TString*> findString(std::string_view str) {
    // returns std::nullopt if not found
}
```

**Caution**:
- Cannot change public API
- May not be worth overhead for hot paths
- Need to consider existing error handling

---

### 3.3 std::span for Array Views

**Effort**: MEDIUM-HIGH
**Risk**: MEDIUM
**Performance Impact**: None (just pointer + size)

**Benefits**:
- Single parameter for array + size
- Bounds checking support
- Subspan operations
- Standard interface

**Candidates** (need to identify):
- Functions taking `T*` + `size_t`
- Array parameters
- Buffer operations

**Example**:
```cpp
// Before
void processArray(const Value* array, size_t size);

// After
void processArray(std::span<const Value> array);

// Can create subviews
auto subset = array.subspan(offset, count);
```

**Implementation**:
1. Identify array+size parameter pairs
2. Start with internal functions
3. Use std::span for new code
4. Benchmark

---

### 3.4 std::variant for Tagged Unions

**Effort**: HIGH
**Risk**: HIGH
**Performance Impact**: Potentially negative (vtable overhead?)

**Current State**: TValue uses tagged union pattern with tt field.

**Caution**:
- TValue is in hot path, performance critical
- Current implementation is well-optimized
- std::variant adds safety but may add overhead
- **DO NOT CHANGE without extensive benchmarking**

**Recommendation**: Keep current implementation. The tagged union pattern is appropriate for Lua's value representation.

---

## Priority 4: Already Using (Continue Expanding)

### 4.1 LuaVector (std::vector wrapper) ✅

**Status**: Already implemented
**Location**: src/memory/LuaVector.h
**Usage**: Integrates std::vector with Lua allocator

**Opportunities**:
- Identify more places to use LuaVector
- Document usage patterns
- Ensure consistent adoption in new code

---

### 4.2 LuaAllocator ✅

**Status**: Already implemented
**Location**: src/memory/luaallocator.h
**Usage**: C++ allocator interface for Lua memory management

**Opportunities**:
- Use with more STL containers
- Consider std::unordered_map, std::set with LuaAllocator
- Document integration patterns

---

### 4.3 Exceptions ✅

**Status**: Already using
**Usage**: Replaced setjmp/longjmp with C++ exceptions

**Opportunities**:
- Ensure all error paths use exceptions
- Document exception guarantees
- Consider std::exception hierarchy

---

## Implementation Roadmap

### Phase 1: C Headers (SAFE, HIGH VALUE)
**Estimated Time**: 2-3 hours
**Risk**: Very Low
**Tasks**:
1. Replace all C headers with C++ equivalents
2. Build and test
3. Benchmark
4. Commit

### Phase 2: std::numeric_limits (SAFE, MEDIUM VALUE)
**Estimated Time**: 3-4 hours
**Risk**: Low
**Tasks**:
1. Add std::numeric_limits helpers to llimits.h
2. Replace INT_MAX, UINT_MAX, SIZE_MAX usage
3. Build and test
4. Benchmark
5. Commit

### Phase 3: memcpy → std::copy_n (char copies) (MEDIUM RISK, MEDIUM VALUE)
**Estimated Time**: 4-6 hours
**Risk**: Medium
**Tasks**:
1. Replace `memcpy(dest, src, n * sizeof(char))` with `std::copy_n`
2. Do one file at a time
3. Benchmark after each file
4. Commit successful changes
5. Revert if performance regression

### Phase 4: Evaluate Additional Opportunities (RESEARCH)
**Estimated Time**: 4-6 hours
**Risk**: N/A
**Tasks**:
1. Search for algorithm opportunities
2. Identify std::array candidates
3. Document findings
4. Prioritize next phases

### Phase 5+: Based on Phase 4 results

---

## Performance Testing Protocol

For each change:

1. **Build**:
   ```bash
   cmake --build build --clean-first
   ```

2. **Functional Test**:
   ```bash
   cd testes && ../build/lua all.lua
   # Expected: "final OK !!!"
   ```

3. **Benchmark** (5 runs):
   ```bash
   cd testes
   for i in 1 2 3 4 5; do \
       ../build/lua all.lua 2>&1 | grep "total time:"; \
   done
   ```

4. **Evaluate**:
   - Target: ≤4.24s (≤1% regression from 4.20s)
   - If >4.24s: REVERT immediately
   - If ≤4.20s: Excellent!
   - If 4.20-4.24s: Acceptable

5. **Commit** (if passed):
   ```bash
   git add <files>
   git commit -m "Phase N: Use C++ stdlib - [specific change]"
   ```

---

## Files by Priority

### High Priority (start here):
1. **src/memory/llimits.h** - Add std::limits, update header includes
2. **src/objects/lstring.cpp** - memcpy → std::copy_n (3 instances)
3. **src/objects/ltable.cpp** - INT_MAX → std::numeric_limits, memcpy review
4. **src/objects/lobject.cpp** - memcpy → std::copy_n (5 instances)
5. **src/auxiliary/lauxlib.cpp** - memcpy → std::copy_n (3 instances)

### Medium Priority:
6. **src/vm/lvm.cpp** - Header cleanup, memcpy review
7. **src/libraries/lstrlib.cpp** - memcpy → std::copy_n (3 instances)
8. **src/serialization/** - Header cleanup
9. **src/compiler/** - std::numeric_limits

### Lower Priority:
10. **src/testing/ltests.cpp** - Test code, less critical

---

## Summary

**Total Identified Opportunities**: ~100+ changes across 40+ files

**Immediate Low-Hanging Fruit**:
1. ✅ C headers → C++ headers (~40 files, 2-3 hours)
2. ✅ INT_MAX → std::numeric_limits (~20 files, 3-4 hours)
3. ✅ memcpy (char) → std::copy_n (~15 instances, 4-6 hours)

**Total Quick Wins**: ~10 hours of work, high confidence, low risk

**Next Steps**:
1. Start with Phase 1 (C headers)
2. Proceed to Phase 2 (numeric_limits)
3. Carefully approach Phase 3 (memcpy)
4. Research and document Phase 4+

**Expected Benefits**:
- More idiomatic C++23 code
- Better type safety
- Clearer intent
- Improved compiler diagnostics
- Foundation for future modernization
- Zero performance regression

---

**Last Updated**: 2025-11-16
**Status**: Ready for implementation
**Next Action**: Begin Phase 1 (C headers replacement)
