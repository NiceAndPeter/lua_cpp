# LuaAllocator - Standard C++ Allocator for Lua Memory Management

## Overview

This document describes the `LuaAllocator` and `LuaVector` utilities that provide standard-conforming C++ containers integrated with Lua's memory management system.

## Components

### 1. LuaAllocator<T> (src/memory/luaallocator.h)

A fully standard-conforming C++17 allocator that uses Lua's memory management system.

**Key Features:**
- Respects Lua's memory limits and GC accounting
- Triggers emergency GC on allocation failure
- Zero overhead compared to manual `luaM_*` calls
- Compatible with all standard containers (vector, deque, list, map, etc.)

**Usage Example:**
```cpp
#include "luaallocator.h"
#include <vector>

void example(lua_State* L) {
    // Create a vector with Lua's allocator
    std::vector<int, LuaAllocator<int>> vec{LuaAllocator<int>(L)};

    vec.push_back(42);
    vec.push_back(84);

    // Memory is automatically tracked by Lua's GC
    // Vector is freed when it goes out of scope
}
```

**Technical Details:**
- Allocation uses `luaM_malloc_` (with GC accounting)
- Deallocation uses `luaM_free_` (with GC debt adjustment)
- Throws `std::bad_alloc` on allocation failure (after emergency GC)
- Fully rebindable for container element types

### 2. LuaVector<T> (src/memory/LuaVector.h)

A convenient wrapper around `std::vector` with `LuaAllocator`.

**Key Features:**
- RAII-based automatic memory management
- Standard vector interface
- Exception-safe
- Works with STL algorithms

**Usage Example:**
```cpp
#include "LuaVector.h"

void example(lua_State* L) {
    LuaVector<int> numbers(L);

    numbers.reserve(1000);
    for (int i = 0; i < 1000; i++) {
        numbers.push_back(i);
    }

    // Access elements
    int first = numbers[0];
    int last = numbers.back();

    // Use with algorithms
    std::sort(numbers.begin(), numbers.end());
}
```

## Testing

### Standalone Test (test_luaallocator.cpp)

Comprehensive test suite demonstrating:
1. Basic allocation/deallocation
2. Vector growth and reallocation
3. Different types (primitives, structs)
4. Memory accounting
5. Exception safety

**Run the test:**
```bash
./build/test_luaallocator
```

**Expected output:**
```
=== LuaAllocator Test Suite ===

Test 1: Basic vector operations... PASSED
Test 2: Vector growth and reallocation... PASSED
Test 3: Different types (double, struct)... PASSED
Test 4: Memory accounting... PASSED
Test 5: Exception safety... PASSED

=== All tests completed ===
```

### Integrated Test (T.testvector)

A test function integrated into Lua's test infrastructure (src/testing/ltests.cpp).

**Usage from Lua:**
```lua
local T = require('testing')

-- Test with 1000 elements
local bytes_allocated = T.testvector(1000)
print("Memory allocated:", bytes_allocated, "bytes")
```

**Implementation:**
- Creates a `LuaVector<int>` with n elements
- Verifies all elements are correct
- Measures memory allocation
- Returns bytes allocated

## Integration Points

### Where to Use

**Good candidates for LuaVector:**
1. **Temporary arrays during compilation/parsing**
   - Growing arrays that are built up then discarded
   - Local buffers in compiler functions

2. **Internal data structures**
   - Non-GC managed helper structures
   - Algorithm working buffers

3. **New code development**
   - Modern C++ approach with automatic memory management
   - Exception-safe resource handling

### Where NOT to Use

**Avoid LuaVector for:**
1. **GC-managed objects**
   - Table arrays, Proto fields, etc.
   - Requires manual memory management for GC traversal

2. **Hot-path VM code**
   - lvm.cpp, ldo.cpp critical paths
   - Benchmark first to verify no regression

3. **Public API structures**
   - C compatibility required
   - Fixed ABI

4. **Fixed-size stack arrays**
   - Use native C arrays for small, fixed sizes
   - `char buffer[256]` is more efficient

## Performance

**Characteristics:**
- Zero allocation overhead vs. manual `luaM_*` calls
- Inline accessor functions
- No vtable overhead (no virtual functions)
- GC integration maintains existing performance characteristics

**Benchmarking:**
When using in performance-critical code:
1. Build in Release mode
2. Run benchmark: `for i in 1 2 3 4 5; do ./build/lua all.lua | grep "total time:"; done`
3. Verify performance ≤ 2.21s (≤1% regression from 2.17s baseline)

## Future Opportunities

### Potential Conversions

**Parser/Compiler Structures:**
- `Dyndata::actvar` (Vardesc array)
- `Labellist` (goto/label lists)
- Temporary code generation buffers

**Buffer Structures:**
- `Mbuffer` (character buffer)
- Lexer token buffers

**Advantages:**
- Automatic cleanup (exception-safe)
- Bounds checking in debug mode
- Standard algorithms support
- Cleaner code

**Considerations:**
- Requires careful testing
- Must benchmark for performance
- Need to verify GC integration

## Examples

### Example 1: Temporary Buffer

```cpp
#include "LuaVector.h"

void processData(lua_State* L, const char* input, size_t len) {
    // Create temporary buffer
    LuaVector<char> buffer(L);
    buffer.reserve(len * 2);  // Reserve space

    // Process input
    for (size_t i = 0; i < len; i++) {
        buffer.push_back(input[i]);
        if (input[i] == '\n') {
            buffer.push_back('\r');  // Add carriage return
        }
    }

    // Use buffer...
    processBuffer(buffer.data(), buffer.size());

    // Automatic cleanup when buffer goes out of scope
}
```

### Example 2: Building an Array

```cpp
#include "LuaVector.h"

Proto* generateCode(lua_State* L, /* ... */) {
    LuaVector<Instruction> code(L);

    // Build code incrementally
    code.push_back(CREATE_ABCk(OP_LOADK, 0, 0, 0));
    code.push_back(CREATE_ABC(OP_RETURN, 0, 1, 0));

    // Allocate Proto and copy
    Proto* p = luaF_newproto(L);
    p->getCodeRef() = /* copy from code.data() */;

    return p;
}
```

### Example 3: With Algorithms

```cpp
#include "LuaVector.h"
#include <algorithm>

void sortAndUnique(lua_State* L, int* data, size_t n) {
    // Copy to LuaVector
    LuaVector<int> vec(L);
    vec.assign(data, data + n);

    // Use STL algorithms
    std::sort(vec.begin(), vec.end());
    auto last = std::unique(vec.begin(), vec.end());
    vec.erase(last, vec.end());

    // Results in vec
    for (int value : vec) {
        process(value);
    }
}
```

## Implementation Notes

### Memory Accounting

All allocations through `LuaAllocator` are tracked by Lua's GC:
- Allocation: `GCdebt` is decreased
- Deallocation: `GCdebt` is increased
- Emergency GC triggered on allocation failure

### Exception Safety

The allocator provides strong exception safety:
- On allocation failure, triggers emergency GC
- If GC doesn't free enough memory, throws `std::bad_alloc`
- RAII ensures cleanup even with exceptions

### Rebinding

The allocator supports rebinding for internal container use:
```cpp
// Container can rebind to different types
std::vector<int, LuaAllocator<int>> vec1(LuaAllocator<int>(L));

// Internally, vector may create LuaAllocator<SomeInternalType>
// This works because LuaAllocator is properly rebindable
```

## Conclusion

`LuaAllocator` and `LuaVector` provide a modern C++ approach to memory management in the Lua codebase while maintaining full integration with Lua's GC system. They offer:

- **Safety**: RAII, exception-safe, bounds checking
- **Convenience**: Standard container interface
- **Performance**: Zero overhead vs. manual memory management
- **Integration**: Full GC accounting and memory limits

Use them for new code and consider them for refactoring opportunities in non-critical paths.
