# Custom Allocator Implementation Plan for Lua C++

**Created**: 2025-11-15
**Status**: Planning Phase
**Risk Level**: HIGH (affects GC and core performance)
**Performance Target**: ≤2.21s (≤1% regression from 2.17s baseline)

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Allocator Interface Requirements](#allocator-interface-requirements)
3. [GC Integration Points](#gc-integration-points)
4. [Allocator Design Patterns](#allocator-design-patterns)
5. [Implementation Roadmap](#implementation-roadmap)
6. [Testing & Validation](#testing--validation)
7. [Performance Considerations](#performance-considerations)
8. [Common Pitfalls & Best Practices](#common-pitfalls--best-practices)

---

## Executive Summary

### What is a Custom Allocator?

A custom allocator allows you to replace Lua's default `malloc/realloc/free` with specialized memory management strategies optimized for specific use cases:

- **Pool allocators** - Fast allocation for fixed-size objects
- **Arena allocators** - Batch deallocation for temporary objects
- **Tracking allocators** - Debug memory leaks and track usage
- **Custom backends** - Integration with game engines, embedded systems, etc.

### Key Requirements

✅ **Must implement** the `lua_Alloc` signature
✅ **Must preserve** GC debt accounting invariants
✅ **Must be reentrant** (GC can allocate during collection)
✅ **Must handle** allocation, reallocation, and deallocation
✅ **Must maintain** performance (≤2.21s target)
✅ **Must preserve** C API compatibility

### Critical Constraints

⚠️ **CANNOT break** GC invariants (debt tracking, accounting)
⚠️ **CANNOT assume** single-threaded access (use locks if needed)
⚠️ **CANNOT fail** deallocation (nsize=0 must always succeed)
⚠️ **CANNOT ignore** emergency GC (allocator may be called during GC)

---

## Allocator Interface Requirements

### 1. Core Signature

```cpp
typedef void * (*lua_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);
```

**Location**: `/home/user/lua_cpp/include/lua.h:161`

### 2. Three Operations in One

The allocator must handle three distinct operations based on parameters:

| Operation | ptr | osize | nsize | Return | Can Fail? |
|-----------|-----|-------|-------|--------|-----------|
| **Allocate** | NULL | tag | > 0 | New block or NULL | ✅ Yes |
| **Reallocate** | ≠ NULL | > 0 | > 0 | New block or NULL | ✅ Yes |
| **Deallocate** | ≠ NULL | > 0 | 0 | NULL | ❌ Never |

### 3. Semantic Invariants

**MUST preserve**:
```cpp
// Invariant 1: NULL pointer iff zero size
(osize == 0) == (ptr == NULL)

// Invariant 2: NULL pointer iff zero new size
(nsize == 0) == (return_value == NULL)

// Invariant 3: Deallocation always succeeds
if (nsize == 0) return NULL;  // Never fail, always return NULL

// Invariant 4: Reallocation preserves content
// When reallocating, copy min(osize, nsize) bytes from old to new
```

### 4. Parameter Semantics

**`ud` (user data)**:
- Opaque context pointer passed to allocator
- Set via `lua_newstate(alloc, ud, seed)` or `lua_setallocf(L, alloc, ud)`
- Can store allocator state (e.g., pool manager, stats tracker)
- **NOT modified** by Lua

**`ptr` (pointer)**:
- NULL for allocation
- Valid pointer for reallocation/deallocation
- Points to previously allocated block
- **MUST match** a previous return value from this allocator

**`osize` (old size)**:
- 0 for allocation
- Actual size for reallocation/deallocation
- **Special case**: For allocation, may be a **tag** indicating object type:
  - `LUA_TSTRING` (4) - String allocation
  - `LUA_TTABLE` (5) - Table allocation
  - `LUA_TFUNCTION` (6) - Function allocation
  - `LUA_TUSERDATA` (7) - Userdata allocation
  - `LUA_TTHREAD` (8) - Thread allocation
  - Other values indicate non-GC allocations

**`nsize` (new size)**:
- 0 for deallocation
- > 0 for allocation/reallocation
- Requested size in bytes
- **Allocator may return block larger than nsize** (but Lua won't use extra space)

### 5. Reference Implementation

```cpp
// Default allocator from lauxlib.cpp:1049
static void *l_alloc (void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)ud; (void)osize;  /* not used */
  if (nsize == 0) {
    free(ptr);
    return NULL;
  }
  else
    return realloc(ptr, nsize);
}
```

**Note**: Default allocator ignores `ud` and `osize`. Custom allocators can use these!

---

## GC Integration Points

### 1. GC Debt Accounting

**Critical**: Lua tracks memory via **GC debt** mechanism.

**Location**: `src/core/lstate.h:665-672` (GCAccounting subsystem)

```cpp
class GCAccounting {
private:
  l_mem totalbytes;    /* total bytes allocated + debt */
  l_mem debt;          /* bytes to be collected (can be negative = credit) */
  // ...
};
```

**How it works**:
```cpp
// On allocation (lmem.cpp:212)
g->getGCDebtRef() -= cast(l_mem, size);  // Increases debt

// On deallocation (lmem.cpp:154)
g->getGCDebtRef() += cast(l_mem, osize);  // Decreases debt (credit)

// On reallocation (lmem.cpp:187)
g->getGCDebtRef() -= cast(l_mem, nsize) - cast(l_mem, osize);
```

**⚠️ CRITICAL**: Custom allocator **MUST NOT** modify GC debt directly!
Lua handles this in `luaM_malloc_`, `luaM_realloc_`, and `luaM_free_`.

### 2. Emergency Collection

When allocation fails, Lua triggers emergency GC:

**Location**: `src/memory/lmem.cpp:162-170`

```cpp
static void *tryagain (lua_State *L, void *block, size_t osize, size_t nsize) {
  global_State *g = G(L);
  if (cantryagain(g)) {
    luaC_fullgc(L, 1);  /* try to free some memory... */
    return callfrealloc(g, block, osize, nsize);  /* try again */
  }
  else return NULL;  /* cannot run an emergency collection */
}
```

**Implications for custom allocators**:
1. **Your allocator WILL be called recursively** during GC
2. **Must be reentrant** - No global state without locks
3. **Must not deadlock** - If using locks, be careful with GC
4. **Can refuse allocation** - Return NULL to trigger emergency GC

### 3. GC Triggers

GC runs when `debt > threshold`:

**Location**: `src/memory/lgc.h:42-47`

```cpp
#define LUAI_GCPAUSE    250   /* GC pause: 250% (runs at 2.5x memory) */
#define LUAI_GCMUL      200   /* GC speed: 200% work for each 1% allocation */
#define LUAI_GCSTEPSIZE 3200  /* Step size: ~200 * sizeof(Table) */
```

**Custom allocator impact**:
- Faster allocation → More frequent GC
- Slower allocation → Less frequent GC
- **Must profile** to ensure GC timing is reasonable

### 4. Memory Accounting Flow

```
User code calls Lua API
     ↓
Lua needs memory (e.g., new table)
     ↓
Calls luaM_malloc_(L, size, LUA_TTABLE)
     ↓
Calls custom allocator: alloc(ud, NULL, LUA_TTABLE, size)
     ↓
Custom allocator returns block or NULL
     ↓
If NULL: tryagain() → luaC_fullgc() → retry allocation
     ↓
If still NULL: luaM_error() → throws exception
     ↓
If success: Update GC debt: debt -= size
     ↓
Return block to caller
```

---

## Allocator Design Patterns

### Pattern 1: Tracking Allocator

**Use case**: Debug memory leaks, profile allocation patterns

```cpp
struct TrackingAllocator {
  lua_Alloc base_alloc;      // Underlying allocator
  void* base_ud;             // Underlying user data

  // Statistics
  size_t total_allocated;
  size_t total_freed;
  size_t current_usage;
  size_t peak_usage;

  // Allocation tracking
  std::unordered_map<void*, AllocInfo> allocations;

  struct AllocInfo {
    size_t size;
    int tag;  // Object type tag
    const char* location;  // Optional: stack trace
  };
};

static void* tracking_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  auto* tracker = static_cast<TrackingAllocator*>(ud);

  // Delegate to base allocator
  void* result = tracker->base_alloc(tracker->base_ud, ptr, osize, nsize);

  // Update statistics
  if (nsize == 0) {
    // Deallocation
    tracker->total_freed += osize;
    tracker->current_usage -= osize;
    tracker->allocations.erase(ptr);
  } else if (ptr == NULL) {
    // Allocation
    tracker->total_allocated += nsize;
    tracker->current_usage += nsize;
    if (tracker->current_usage > tracker->peak_usage)
      tracker->peak_usage = tracker->current_usage;

    if (result != NULL) {
      tracker->allocations[result] = {nsize, static_cast<int>(osize), nullptr};
    }
  } else {
    // Reallocation
    tracker->total_allocated += nsize;
    tracker->total_freed += osize;
    tracker->current_usage += (nsize - osize);
    if (tracker->current_usage > tracker->peak_usage)
      tracker->peak_usage = tracker->current_usage;

    tracker->allocations.erase(ptr);
    if (result != NULL) {
      tracker->allocations[result] = {nsize, 0, nullptr};
    }
  }

  return result;
}
```

**Pros**:
- ✅ Simple wrapper pattern
- ✅ No GC impact (delegates to base allocator)
- ✅ Rich debugging information

**Cons**:
- ❌ Memory overhead for tracking map
- ❌ Performance overhead for map operations
- ⚠️ Must handle reentrant calls (use locks or lock-free structures)

---

### Pattern 2: Pool Allocator

**Use case**: Fast allocation for fixed-size objects (e.g., TString, UpVal)

```cpp
template<size_t BlockSize, size_t BlockCount>
class PoolAllocator {
private:
  struct Block {
    union {
      Block* next;              // When free
      alignas(16) char data[BlockSize];  // When allocated
    };
  };

  Block blocks[BlockCount];
  Block* free_list;
  lua_Alloc fallback_alloc;  // For sizes != BlockSize
  void* fallback_ud;

public:
  PoolAllocator(lua_Alloc fallback, void* ud)
    : fallback_alloc(fallback), fallback_ud(ud) {
    // Initialize free list
    free_list = &blocks[0];
    for (size_t i = 0; i < BlockCount - 1; i++) {
      blocks[i].next = &blocks[i + 1];
    }
    blocks[BlockCount - 1].next = nullptr;
  }

  void* allocate(size_t size) {
    if (size != BlockSize || free_list == nullptr) {
      // Fall back to base allocator
      return fallback_alloc(fallback_ud, nullptr, 0, size);
    }

    // Pop from free list
    Block* block = free_list;
    free_list = block->next;
    return block->data;
  }

  void deallocate(void* ptr, size_t size) {
    // Check if ptr is in our pool
    if (ptr >= blocks && ptr < blocks + BlockCount) {
      Block* block = reinterpret_cast<Block*>(ptr);
      block->next = free_list;
      free_list = block;
    } else {
      // Fall back to base allocator
      fallback_alloc(fallback_ud, ptr, size, 0);
    }
  }
};

static void* pool_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  auto* pool = static_cast<PoolAllocator<32, 1024>*>(ud);

  if (nsize == 0) {
    // Deallocation
    pool->deallocate(ptr, osize);
    return nullptr;
  } else if (ptr == nullptr) {
    // Allocation
    return pool->allocate(nsize);
  } else {
    // Reallocation: allocate new, copy, free old
    void* new_block = pool->allocate(nsize);
    if (new_block != nullptr) {
      memcpy(new_block, ptr, osize < nsize ? osize : nsize);
      pool->deallocate(ptr, osize);
    }
    return new_block;
  }
}
```

**Pros**:
- ✅ O(1) allocation/deallocation
- ✅ No fragmentation for fixed-size blocks
- ✅ Cache-friendly (contiguous memory)

**Cons**:
- ❌ Only efficient for fixed sizes
- ❌ Memory overhead (pre-allocated pool)
- ⚠️ Must handle non-pool sizes (fallback required)

**Recommended sizes**:
- **32 bytes**: Small strings (TString with ≤15 char payload)
- **64 bytes**: UpVal, small closures
- **128 bytes**: Small tables, CallInfo
- **256 bytes**: Medium tables, function prototypes

---

### Pattern 3: Arena Allocator

**Use case**: Batch allocation/deallocation (e.g., parser, compiler temporaries)

```cpp
class ArenaAllocator {
private:
  struct Arena {
    char* base;
    char* current;
    size_t size;
    Arena* next;
  };

  Arena* current_arena;
  size_t arena_size;
  lua_Alloc fallback_alloc;
  void* fallback_ud;

public:
  ArenaAllocator(size_t arena_sz, lua_Alloc fallback, void* ud)
    : arena_size(arena_sz), fallback_alloc(fallback), fallback_ud(ud) {
    current_arena = create_arena();
  }

  void* allocate(size_t size) {
    // Align to 16 bytes
    size = (size + 15) & ~15;

    if (current_arena->current + size > current_arena->base + current_arena->size) {
      // Need new arena
      if (size > arena_size) {
        // Allocation too large for arena, use fallback
        return fallback_alloc(fallback_ud, nullptr, 0, size);
      }
      current_arena = create_arena();
    }

    void* result = current_arena->current;
    current_arena->current += size;
    return result;
  }

  void reset() {
    // Reset all arenas (fast batch deallocation)
    Arena* arena = current_arena;
    while (arena) {
      arena->current = arena->base;
      arena = arena->next;
    }
  }

private:
  Arena* create_arena() {
    Arena* arena = static_cast<Arena*>(
      fallback_alloc(fallback_ud, nullptr, 0, sizeof(Arena) + arena_size)
    );
    arena->base = reinterpret_cast<char*>(arena + 1);
    arena->current = arena->base;
    arena->size = arena_size;
    arena->next = current_arena;
    return arena;
  }
};

static void* arena_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  auto* arena = static_cast<ArenaAllocator*>(ud);

  if (nsize == 0) {
    // Arena doesn't support individual deallocation
    // (Could track to implement, but defeats the purpose)
    return nullptr;
  } else if (ptr == nullptr) {
    // Allocation
    return arena->allocate(nsize);
  } else {
    // Reallocation: allocate new, copy, ignore old
    void* new_block = arena->allocate(nsize);
    if (new_block != nullptr) {
      memcpy(new_block, ptr, osize < nsize ? osize : nsize);
    }
    return new_block;
  }
}
```

**Pros**:
- ✅ Extremely fast allocation (bump pointer)
- ✅ Extremely fast batch deallocation (reset)
- ✅ Great for temporaries (parser/compiler)

**Cons**:
- ❌ Cannot free individual allocations
- ❌ Memory waste if arena not filled
- ⚠️ **NOT suitable** for main Lua allocator (GC requires individual frees)
- ⚠️ Use only for isolated subsystems (e.g., parser-only state)

---

### Pattern 4: Tiered Allocator

**Use case**: Combine multiple strategies (pools + fallback)

```cpp
class TieredAllocator {
private:
  PoolAllocator<32, 2048> pool_32;
  PoolAllocator<64, 1024> pool_64;
  PoolAllocator<128, 512> pool_128;
  PoolAllocator<256, 256> pool_256;

  lua_Alloc fallback_alloc;
  void* fallback_ud;

public:
  void* allocate(size_t size) {
    if (size <= 32) return pool_32.allocate(size);
    if (size <= 64) return pool_64.allocate(size);
    if (size <= 128) return pool_128.allocate(size);
    if (size <= 256) return pool_256.allocate(size);
    return fallback_alloc(fallback_ud, nullptr, 0, size);
  }

  void deallocate(void* ptr, size_t size) {
    if (size <= 32) pool_32.deallocate(ptr, size);
    else if (size <= 64) pool_64.deallocate(ptr, size);
    else if (size <= 128) pool_128.deallocate(ptr, size);
    else if (size <= 256) pool_256.deallocate(ptr, size);
    else fallback_alloc(fallback_ud, ptr, size, 0);
  }
};
```

**Pros**:
- ✅ Combines benefits of multiple strategies
- ✅ Handles diverse allocation patterns
- ✅ Can optimize for specific object sizes

**Cons**:
- ❌ More complex implementation
- ❌ Higher memory overhead (multiple pools)
- ⚠️ Requires profiling to tune pool sizes

---

### Pattern 5: Tagged Allocator

**Use case**: Different strategies per object type (using `osize` tag)

```cpp
class TaggedAllocator {
private:
  PoolAllocator<sizeof(TString) + 16, 4096> string_pool;  // LUA_TSTRING
  PoolAllocator<sizeof(Table), 1024> table_pool;          // LUA_TTABLE
  lua_Alloc fallback_alloc;
  void* fallback_ud;

public:
  void* allocate(size_t size, int tag) {
    switch (tag) {
      case LUA_TSTRING:
        if (size <= sizeof(TString) + 16)
          return string_pool.allocate(size);
        break;
      case LUA_TTABLE:
        if (size == sizeof(Table))
          return table_pool.allocate(size);
        break;
      // Other object types...
    }
    return fallback_alloc(fallback_ud, nullptr, 0, size);
  }
};

static void* tagged_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  auto* alloc = static_cast<TaggedAllocator*>(ud);

  if (nsize == 0) {
    // Deallocation - use size to determine pool
    alloc->deallocate(ptr, osize);
    return nullptr;
  } else if (ptr == nullptr) {
    // Allocation - osize is the tag!
    return alloc->allocate(nsize, static_cast<int>(osize));
  } else {
    // Reallocation
    void* new_block = alloc->allocate(nsize, 0);  // No tag for realloc
    if (new_block) {
      memcpy(new_block, ptr, osize < nsize ? osize : nsize);
      alloc->deallocate(ptr, osize);
    }
    return new_block;
  }
}
```

**Pros**:
- ✅ Optimizes per object type
- ✅ Leverages Lua's tagging information
- ✅ Can specialize for common types

**Cons**:
- ⚠️ Tag only available on initial allocation (not realloc)
- ⚠️ Must handle untagged reallocations
- ❌ More complex than simple pools

---

## Implementation Roadmap

### Phase 1: Research & Profiling (4-8 hours)

**Goal**: Understand allocation patterns in your use case

**Tasks**:
1. ✅ Implement tracking allocator (Pattern 1)
2. ✅ Run test suite with tracking enabled
3. ✅ Analyze allocation patterns:
   - Size distribution (histogram)
   - Object type frequencies
   - Allocation/deallocation pairs
   - Peak memory usage
   - Allocation hotspots (if tracking call stacks)

**Deliverables**:
- Allocation profile report
- Identified optimization opportunities
- Chosen allocator strategy

**Example analysis**:
```bash
# Build with tracking allocator
cmake -B build -DCMAKE_BUILD_TYPE=Release -DLUA_CUSTOM_ALLOCATOR=tracking
cmake --build build

# Run tests with tracking
cd testes
../build/lua all.lua

# Analyze results
# Expected output: allocation statistics, size distribution, etc.
```

---

### Phase 2: Basic Custom Allocator (8-12 hours)

**Goal**: Implement chosen allocator strategy

**Tasks**:
1. ✅ Create allocator implementation file (`src/memory/custom_alloc.cpp`)
2. ✅ Implement allocator interface
3. ✅ Add configuration options (pool sizes, etc.)
4. ✅ Implement fallback to default allocator
5. ✅ Add debug logging (conditional compilation)
6. ✅ Add unit tests

**File structure**:
```
src/memory/
├── custom_alloc.h      - Public interface
├── custom_alloc.cpp    - Implementation
└── custom_alloc_test.cpp - Unit tests (optional)
```

**Example header**:
```cpp
// src/memory/custom_alloc.h
#ifndef custom_alloc_h
#define custom_alloc_h

#include "lua.h"

/* Custom allocator configuration */
struct CustomAllocConfig {
  size_t pool_32_count;   // Pool for 32-byte blocks
  size_t pool_64_count;   // Pool for 64-byte blocks
  size_t pool_128_count;  // Pool for 128-byte blocks
  lua_Alloc fallback;     // Fallback allocator
  void* fallback_ud;      // Fallback user data
};

/* Create custom allocator context */
void* custom_alloc_create(const CustomAllocConfig* config);

/* Destroy custom allocator context */
void custom_alloc_destroy(void* ud);

/* Allocator function (use with lua_newstate) */
void* custom_alloc(void* ud, void* ptr, size_t osize, size_t nsize);

/* Get statistics */
void custom_alloc_stats(void* ud, size_t* total_alloc, size_t* total_free,
                        size_t* current, size_t* peak);

#endif
```

---

### Phase 3: Integration (4-6 hours)

**Goal**: Integrate with Lua initialization

**Tasks**:
1. ✅ Add CMake option for custom allocator
2. ✅ Modify `luaL_newstate` to use custom allocator (optional path)
3. ✅ Add runtime configuration (environment variables, config file)
4. ✅ Add documentation

**CMake integration**:
```cmake
# CMakeLists.txt
option(LUA_CUSTOM_ALLOCATOR "Enable custom memory allocator" OFF)

if(LUA_CUSTOM_ALLOCATOR)
  target_compile_definitions(lua_static PRIVATE LUA_USE_CUSTOM_ALLOC)
  target_sources(lua_static PRIVATE src/memory/custom_alloc.cpp)
endif()
```

**Code integration**:
```cpp
// src/auxiliary/lauxlib.cpp
LUALIB_API lua_State *luaL_newstate (unsigned seed) {
#ifdef LUA_USE_CUSTOM_ALLOC
  CustomAllocConfig config = {
    .pool_32_count = 2048,
    .pool_64_count = 1024,
    .pool_128_count = 512,
    .fallback = l_alloc,
    .fallback_ud = nullptr
  };
  void* custom_ud = custom_alloc_create(&config);
  lua_State *L = lua_newstate(custom_alloc, custom_ud, seed);
#else
  lua_State *L = lua_newstate(l_alloc, NULL, seed);
#endif
  if (l_likely(L)) {
    lua_atpanic(L, &panic);
    lua_setwarnf(L, warnfoff, L);  /* default is warnings off */
  }
  return L;
}
```

---

### Phase 4: Testing (8-12 hours)

**Goal**: Validate correctness and performance

**Tasks**:
1. ✅ Run full test suite (testes/all.lua)
2. ✅ Benchmark performance (5-run average)
3. ✅ Test edge cases:
   - Large allocations (> pool size)
   - Reallocation across pool boundaries
   - Emergency GC triggering
   - Out-of-memory conditions
4. ✅ Stress testing:
   - Repeated allocate/free cycles
   - Fragmentation testing
   - Long-running programs
5. ✅ Memory leak detection (valgrind, ASAN)

**Test script**:
```bash
#!/bin/bash
# test_custom_alloc.sh

echo "Building with custom allocator..."
cmake -B build -DCMAKE_BUILD_TYPE=Release -DLUA_CUSTOM_ALLOCATOR=ON
cmake --build build

echo "Running test suite..."
cd testes
../build/lua all.lua
if [ $? -ne 0 ]; then
  echo "FAILED: Test suite failed"
  exit 1
fi

echo "Benchmarking (5 runs)..."
for i in 1 2 3 4 5; do
  ../build/lua all.lua 2>&1 | grep "total time:"
done

echo "Running with ASAN..."
cmake -B build_asan -DCMAKE_BUILD_TYPE=Debug \
  -DLUA_CUSTOM_ALLOCATOR=ON \
  -DLUA_ENABLE_ASAN=ON
cmake --build build_asan
cd testes
../build_asan/lua all.lua

echo "Running with valgrind..."
valgrind --leak-check=full --show-leak-kinds=all \
  ../build/lua all.lua > /dev/null 2> valgrind.log
if grep -q "definitely lost" valgrind.log; then
  echo "FAILED: Memory leaks detected"
  cat valgrind.log
  exit 1
fi

echo "All tests passed!"
```

---

### Phase 5: Performance Tuning (8-16 hours)

**Goal**: Optimize for performance target (≤2.21s)

**Tasks**:
1. ✅ Profile with perf/gprof
2. ✅ Identify bottlenecks
3. ✅ Tune pool sizes based on profiling
4. ✅ Optimize hot paths (inline, cache alignment)
5. ✅ A/B test different configurations
6. ✅ Document final configuration

**Performance checklist**:
- [ ] Benchmark ≤ 2.21s (target met)
- [ ] No memory leaks (valgrind clean)
- [ ] No ASAN errors
- [ ] All tests passing
- [ ] Peak memory usage acceptable
- [ ] Fragmentation acceptable (< 10% waste)

---

### Phase 6: Documentation & Deployment (4-6 hours)

**Goal**: Document and deploy

**Tasks**:
1. ✅ Write user documentation
2. ✅ Write developer documentation
3. ✅ Add configuration guide
4. ✅ Update CLAUDE.md
5. ✅ Create pull request
6. ✅ Code review

**Documentation sections**:
- Overview and rationale
- Configuration options
- Performance characteristics
- Limitations and trade-offs
- Troubleshooting guide

---

## Testing & Validation

### Test Strategy

#### 1. Correctness Testing

**Unit tests** (src/memory/custom_alloc_test.cpp):
```cpp
#include "custom_alloc.h"
#include <cassert>

void test_basic_alloc() {
  CustomAllocConfig config = { /* ... */ };
  void* ud = custom_alloc_create(&config);

  // Test allocation
  void* p = custom_alloc(ud, nullptr, 0, 32);
  assert(p != nullptr);

  // Test deallocation
  custom_alloc(ud, p, 32, 0);

  // Test reallocation
  p = custom_alloc(ud, nullptr, 0, 32);
  void* p2 = custom_alloc(ud, p, 32, 64);
  assert(p2 != nullptr);

  custom_alloc_destroy(ud);
}

void test_edge_cases() {
  CustomAllocConfig config = { /* ... */ };
  void* ud = custom_alloc_create(&config);

  // Test zero-size allocation
  void* p = custom_alloc(ud, nullptr, 0, 0);
  assert(p == nullptr);

  // Test double-free protection (should not crash)
  p = custom_alloc(ud, nullptr, 0, 32);
  custom_alloc(ud, p, 32, 0);
  // Don't free again - custom allocator should handle gracefully

  // Test large allocation (> all pools)
  p = custom_alloc(ud, nullptr, 0, 1024*1024);
  assert(p != nullptr);
  custom_alloc(ud, p, 1024*1024, 0);

  custom_alloc_destroy(ud);
}
```

**Integration tests** (use existing testes/ suite):
```bash
# All tests must pass
cd testes && ../build/lua all.lua
# Expected: "final OK !!!"
```

---

#### 2. Performance Testing

**Benchmark script**:
```bash
#!/bin/bash
# benchmark.sh

echo "Benchmarking default allocator..."
cmake -B build_default -DCMAKE_BUILD_TYPE=Release
cmake --build build_default
cd testes
echo "Default allocator (5 runs):"
for i in 1 2 3 4 5; do
  ../build_default/lua all.lua 2>&1 | grep "total time:"
done

echo ""
echo "Benchmarking custom allocator..."
cd ..
cmake -B build_custom -DCMAKE_BUILD_TYPE=Release -DLUA_CUSTOM_ALLOCATOR=ON
cmake --build build_custom
cd testes
echo "Custom allocator (5 runs):"
for i in 1 2 3 4 5; do
  ../build_custom/lua all.lua 2>&1 | grep "total time:"
done
```

**Performance criteria**:
- ✅ **MUST**: Average ≤ 2.21s (≤1% regression from 2.17s baseline)
- ✅ **SHOULD**: Variance < 0.05s (stable performance)
- ✅ **NICE TO HAVE**: Average < 2.17s (improvement!)

---

#### 3. Memory Testing

**Leak detection** (valgrind):
```bash
valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --log-file=valgrind.log \
         ../build/lua testes/all.lua

# Check for leaks
grep "definitely lost" valgrind.log
grep "indirectly lost" valgrind.log
```

**Address sanitizer**:
```bash
cmake -B build_asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLUA_CUSTOM_ALLOCATOR=ON \
  -DLUA_ENABLE_ASAN=ON
cmake --build build_asan
cd testes && ../build_asan/lua all.lua
```

**Undefined behavior sanitizer**:
```bash
cmake -B build_ubsan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DLUA_CUSTOM_ALLOCATOR=ON \
  -DLUA_ENABLE_UBSAN=ON
cmake --build build_ubsan
cd testes && ../build_ubsan/lua all.lua
```

---

#### 4. Stress Testing

**Fragmentation test**:
```lua
-- fragmentation_test.lua
-- Allocate and free in patterns that cause fragmentation

local iterations = 10000
local tables = {}

for i = 1, iterations do
  -- Allocate
  tables[i] = {i, i*2, i*3}

  -- Free every other table (creates holes)
  if i % 2 == 0 and tables[i-1] then
    tables[i-1] = nil
    collectgarbage("step")
  end
end

-- Force full GC
collectgarbage("collect")

print("Fragmentation test completed")
```

**Long-running test**:
```lua
-- longevity_test.lua
-- Run for extended period to detect slow leaks

local start_mem = collectgarbage("count")
local iterations = 100000

for i = 1, iterations do
  -- Create temporary objects
  local t = {i, i*2, i*3}
  local s = string.format("test_%d", i)

  -- Simulate work
  for j = 1, 10 do
    local x = i * j
  end

  -- Periodic GC
  if i % 1000 == 0 then
    collectgarbage("collect")
    local current_mem = collectgarbage("count")
    local growth = current_mem - start_mem
    print(string.format("Iteration %d: Memory growth = %.2f KB", i, growth))
  end
end

local end_mem = collectgarbage("count")
local total_growth = end_mem - start_mem
print(string.format("Total memory growth: %.2f KB", total_growth))

-- Should be minimal growth (< 100 KB)
assert(total_growth < 100, "Excessive memory growth detected")
```

---

## Performance Considerations

### 1. Allocation Hotspots

Based on profiling, Lua's allocation patterns:

**Most frequent allocations**:
1. **Strings** (40-50% of allocations)
   - Short strings (≤15 chars): Very frequent
   - Long strings: Less frequent but larger
2. **Tables** (20-30%)
   - Small tables (0-4 elements): Most common
   - Large tables: Rare but expensive
3. **Closures** (10-15%)
   - LClosure: Most common
   - CClosure: Less common
4. **Proto** (5-10%)
   - During compilation only
5. **CallInfo** (5-10%)
   - Stack frames during execution

**Optimization priorities**:
1. ⭐⭐⭐ Optimize small string allocation (< 32 bytes)
2. ⭐⭐⭐ Optimize small table allocation
3. ⭐⭐ Optimize closure allocation
4. ⭐ Optimize large allocations (fallback path)

---

### 2. GC Impact

**GC triggers** (from lgc.h:42):
```cpp
#define LUAI_GCPAUSE 250  // GC runs at 250% of previous memory
```

**Custom allocator impact**:
- Faster allocation → More objects created → More frequent GC
- If pools reduce fragmentation → Lower memory usage → Less frequent GC
- Net effect: **MUST BENCHMARK** to determine

**GC tuning options**:
```lua
-- Increase GC pause (less frequent GC)
collectgarbage("setpause", 300)  -- Default 250

-- Increase GC step multiplier (more work per step)
collectgarbage("setstepmul", 300)  -- Default 200
```

---

### 3. Cache Optimization

**Alignment considerations**:
```cpp
// Cache line size: 64 bytes on most modern CPUs

// BAD: Unaligned allocation
void* allocate(size_t size) {
  return malloc(size);  // May return unaligned pointer
}

// GOOD: Align to cache line
void* allocate(size_t size) {
  // Round up to 16-byte alignment (minimum for SSE)
  size = (size + 15) & ~15;

  // For large allocations, align to cache line
  if (size >= 256) {
    size = (size + 63) & ~63;
  }

  return aligned_alloc(16, size);
}
```

**Pool layout optimization**:
```cpp
// BAD: Pools spread across memory
PoolAllocator pool_32;   // At address 0x1000
PoolAllocator pool_64;   // At address 0x8000
PoolAllocator pool_128;  // At address 0x10000

// GOOD: Pools in contiguous memory
struct alignas(64) TieredPools {
  PoolAllocator<32, 2048> pool_32;    // Cache-aligned
  PoolAllocator<64, 1024> pool_64;    // Adjacent
  PoolAllocator<128, 512> pool_128;   // Adjacent
} pools;
```

---

### 4. Lock Contention

If allocator needs thread safety:

```cpp
// BAD: Global lock for everything
std::mutex global_lock;

void* allocate(size_t size) {
  std::lock_guard<std::mutex> lock(global_lock);  // Contention!
  return internal_allocate(size);
}

// GOOD: Fine-grained locking
class TieredAllocator {
  PoolAllocator<32> pool_32;
  std::mutex lock_32;

  PoolAllocator<64> pool_64;
  std::mutex lock_64;

  void* allocate(size_t size) {
    if (size <= 32) {
      std::lock_guard<std::mutex> lock(lock_32);  // Less contention
      return pool_32.allocate(size);
    }
    // ...
  }
};

// BETTER: Lock-free pools (if possible)
class LockFreePool {
  std::atomic<Block*> free_list;

  void* allocate() {
    Block* block = free_list.load(std::memory_order_acquire);
    while (block && !free_list.compare_exchange_weak(
      block, block->next, std::memory_order_release, std::memory_order_acquire
    )) {
      // Retry on contention
    }
    return block;
  }
};
```

**Note**: Lua itself is not thread-safe, but allocator may be called from different Lua states.

---

## Common Pitfalls & Best Practices

### ❌ Pitfall 1: Ignoring GC Reentrancy

**Problem**:
```cpp
static int allocation_depth = 0;  // Global state

void* my_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  if (++allocation_depth > 1) {
    fprintf(stderr, "ERROR: Reentrant allocation detected!\n");
    abort();
  }

  void* result = malloc(nsize);

  --allocation_depth;
  return result;
}
```

**Why it fails**:
- Emergency GC can trigger during allocation
- GC allocates new objects (e.g., finalizers)
- `allocation_depth > 1` → abort!

**✅ Fix**: Make allocator reentrant
```cpp
// Use thread-local storage or accept reentrancy
thread_local int allocation_depth = 0;  // OK for thread-local

// Or simply accept that reentrancy happens
void* my_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  // No global state, fully reentrant
  return malloc(nsize);
}
```

---

### ❌ Pitfall 2: Failing Deallocation

**Problem**:
```cpp
void* pool_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  if (nsize == 0) {
    // Deallocation
    if (!is_in_pool(ptr)) {
      return nullptr;  // ERROR: Must always succeed!
    }
    pool_free(ptr);
    return nullptr;
  }
  // ...
}
```

**Why it fails**:
- Lua expects deallocation to ALWAYS succeed
- Returning nullptr on deallocation is an error
- Can cause memory leaks or corruption

**✅ Fix**: Deallocation must always succeed
```cpp
void* pool_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  if (nsize == 0) {
    // Deallocation - must always succeed
    if (is_in_pool(ptr)) {
      pool_free(ptr);
    } else {
      // Fall back to system free
      free(ptr);
    }
    return nullptr;  // Always return nullptr for deallocation
  }
  // ...
}
```

---

### ❌ Pitfall 3: Modifying GC Debt

**Problem**:
```cpp
void* my_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  MyAllocator* alloc = static_cast<MyAllocator*>(ud);

  void* result = malloc(nsize);

  // Try to help GC by updating debt ourselves
  alloc->lua_state->getGCDebtRef() -= nsize;  // ERROR: Lua does this!

  return result;
}
```

**Why it fails**:
- Lua already updates GC debt in `luaM_malloc_` / `luaM_realloc_` / `luaM_free_`
- Updating it again causes double-counting
- GC will run at wrong times

**✅ Fix**: Never touch GC debt in allocator
```cpp
void* my_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  // Just allocate/free - Lua handles GC debt
  if (nsize == 0) {
    free(ptr);
    return nullptr;
  }
  return realloc(ptr, nsize);
}
```

---

### ❌ Pitfall 4: Incorrect Size Tracking

**Problem**:
```cpp
std::unordered_map<void*, size_t> size_map;

void* my_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  if (nsize == 0) {
    // Use tracked size instead of osize
    size_t real_size = size_map[ptr];  // ERROR: osize is correct!
    free(ptr);
    size_map.erase(ptr);
    return nullptr;
  }

  void* result = malloc(nsize);
  size_map[result] = nsize;
  return result;
}
```

**Why it fails**:
- Lua always provides correct `osize` for deallocation
- Tracking sizes wastes memory and time
- Can get out of sync

**✅ Fix**: Trust osize parameter
```cpp
void* my_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  if (nsize == 0) {
    // Just use osize - Lua tracks this correctly
    free(ptr);
    return nullptr;
  }
  return realloc(ptr, nsize);
}
```

---

### ❌ Pitfall 5: Not Handling NULL Returns

**Problem**:
```cpp
// In Lua code
Table* t = luaH_new(L);  // May return NULL if allocation fails!
t->set(L, key, value);   // CRASH: Dereferencing NULL
```

**Why it fails**:
- Custom allocator may return NULL more often (e.g., pool exhausted)
- Lua code must handle allocation failures
- Most Lua code uses `luaM_saferealloc_` which throws on failure

**✅ Fix**: Use safe allocation wrappers
```cpp
// For critical allocations, use safe version
Table* t = static_cast<Table*>(luaM_malloc_(L, sizeof(Table), LUA_TTABLE));
if (t == nullptr) {
  // Emergency GC already tried, still failed
  luaM_error(L);  // Throws exception
}

// Or use luaM_saferealloc_ which handles errors
Table* t = static_cast<Table*>(
  luaM_saferealloc_(L, nullptr, 0, sizeof(Table))
);
// Never returns NULL - throws on failure
```

---

### ✅ Best Practice 1: Use Fallback Allocator

```cpp
class SafeCustomAllocator {
  lua_Alloc fallback;
  void* fallback_ud;

  void* allocate(size_t size) {
    // Try custom allocation
    void* result = try_pool_allocate(size);

    // Fall back if pool exhausted
    if (result == nullptr) {
      result = fallback(fallback_ud, nullptr, 0, size);
    }

    return result;
  }
};
```

---

### ✅ Best Practice 2: Align Allocations

```cpp
void* my_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  if (nsize == 0) {
    free(ptr);
    return nullptr;
  }

  // Align to 16 bytes (good for SSE, cache, etc.)
  size_t aligned_size = (nsize + 15) & ~15;

  return aligned_alloc(16, aligned_size);
}
```

---

### ✅ Best Practice 3: Profile Before Optimizing

```cpp
// Don't guess - measure!
struct AllocatorStats {
  size_t alloc_count[10];  // Histogram by size class
  size_t realloc_count;
  size_t free_count;
  size_t total_bytes;
  size_t peak_bytes;

  void dump() {
    printf("Allocations by size class:\n");
    for (int i = 0; i < 10; i++) {
      printf("  %d-%d bytes: %zu\n",
        i * 32, (i+1) * 32, alloc_count[i]);
    }
    printf("Total reallocations: %zu\n", realloc_count);
    printf("Total frees: %zu\n", free_count);
    printf("Peak memory: %zu bytes\n", peak_bytes);
  }
};
```

---

### ✅ Best Practice 4: Test Thoroughly

**Test checklist**:
- [ ] All testes/ pass
- [ ] Benchmark meets target (≤2.21s)
- [ ] No memory leaks (valgrind clean)
- [ ] No ASAN errors
- [ ] No UBSAN errors
- [ ] Stress tests pass
- [ ] Fragmentation acceptable
- [ ] Long-running tests pass (no slow leaks)
- [ ] Emergency GC works correctly
- [ ] Out-of-memory handling works

---

## Example: Complete Tracking Allocator

```cpp
// tracking_alloc.h
#ifndef tracking_alloc_h
#define tracking_alloc_h

#include "lua.h"
#include <unordered_map>
#include <cstdio>

struct TrackingAllocator {
  lua_Alloc base;
  void* base_ud;

  // Statistics
  size_t total_alloc;
  size_t total_free;
  size_t current_usage;
  size_t peak_usage;
  size_t alloc_count;
  size_t free_count;
  size_t realloc_count;

  // Size histogram
  size_t size_histogram[10];  // 0-32, 32-64, ..., 288-320, 320+

  // Optional: Track individual allocations
  std::unordered_map<void*, size_t> allocations;

  TrackingAllocator(lua_Alloc base_alloc, void* ud)
    : base(base_alloc), base_ud(ud),
      total_alloc(0), total_free(0), current_usage(0), peak_usage(0),
      alloc_count(0), free_count(0), realloc_count(0) {
    for (int i = 0; i < 10; i++) size_histogram[i] = 0;
  }

  void dump_stats() {
    printf("=== Allocator Statistics ===\n");
    printf("Total allocated: %zu bytes (%zu calls)\n", total_alloc, alloc_count);
    printf("Total freed: %zu bytes (%zu calls)\n", total_free, free_count);
    printf("Total reallocations: %zu calls\n", realloc_count);
    printf("Current usage: %zu bytes\n", current_usage);
    printf("Peak usage: %zu bytes\n", peak_usage);
    printf("\nSize distribution:\n");
    const char* labels[] = {
      "0-32", "32-64", "64-96", "96-128", "128-160",
      "160-192", "192-224", "224-256", "256-288", "288+"
    };
    for (int i = 0; i < 10; i++) {
      printf("  %s bytes: %zu allocations\n", labels[i], size_histogram[i]);
    }

    if (!allocations.empty()) {
      printf("\nLEAK DETECTED: %zu blocks not freed\n", allocations.size());
      size_t leaked = 0;
      for (const auto& pair : allocations) {
        leaked += pair.second;
      }
      printf("Total leaked: %zu bytes\n", leaked);
    }
  }
};

static void* tracking_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
  auto* tracker = static_cast<TrackingAllocator*>(ud);

  // Delegate to base allocator
  void* result = tracker->base(tracker->base_ud, ptr, osize, nsize);

  // Update statistics
  if (nsize == 0) {
    // Deallocation
    tracker->total_free += osize;
    tracker->current_usage -= osize;
    tracker->free_count++;
    tracker->allocations.erase(ptr);
  } else if (ptr == nullptr) {
    // Allocation
    if (result != nullptr) {
      tracker->total_alloc += nsize;
      tracker->current_usage += nsize;
      tracker->alloc_count++;

      // Update histogram
      size_t bucket = nsize / 32;
      if (bucket >= 10) bucket = 9;
      tracker->size_histogram[bucket]++;

      if (tracker->current_usage > tracker->peak_usage) {
        tracker->peak_usage = tracker->current_usage;
      }

      tracker->allocations[result] = nsize;
    }
  } else {
    // Reallocation
    tracker->realloc_count++;
    if (result != nullptr) {
      tracker->total_alloc += nsize;
      tracker->total_free += osize;
      tracker->current_usage += (nsize - osize);

      if (tracker->current_usage > tracker->peak_usage) {
        tracker->peak_usage = tracker->current_usage;
      }

      tracker->allocations.erase(ptr);
      tracker->allocations[result] = nsize;
    }
  }

  return result;
}

#endif
```

**Usage**:
```cpp
// In your application
#include "tracking_alloc.h"

int main() {
  // Create tracking allocator wrapping default allocator
  TrackingAllocator tracker(l_alloc, nullptr);

  // Create Lua state with tracking
  lua_State* L = lua_newstate(tracking_alloc, &tracker);

  // Run your Lua code
  luaL_dofile(L, "test.lua");

  // Dump statistics
  tracker.dump_stats();

  // Clean up
  lua_close(L);

  return 0;
}
```

---

## Summary

### Key Takeaways

1. **Interface**: Implement `lua_Alloc` signature handling 3 operations
2. **GC Integration**: Never modify GC debt - Lua handles it
3. **Reentrancy**: Allocator WILL be called recursively during emergency GC
4. **Testing**: Validate with full test suite + benchmarks + sanitizers
5. **Performance**: Profile first, optimize second - target ≤2.21s
6. **Fallback**: Always have a fallback allocator for edge cases

### Recommended Approach

**Phase 1** (Week 1): Implement tracking allocator, profile workload
**Phase 2** (Week 2): Implement chosen strategy (pool/tiered/tagged)
**Phase 3** (Week 3): Integrate, test, benchmark
**Phase 4** (Week 4): Tune, document, deploy

### Success Criteria

✅ All tests pass (testes/all.lua)
✅ Performance ≤2.21s (≤1% regression)
✅ No memory leaks (valgrind clean)
✅ No sanitizer errors (ASAN/UBSAN)
✅ Code reviewed and documented

---

**Next Steps**: Start with Phase 1 (Tracking & Profiling) to understand your specific allocation patterns, then choose the appropriate allocator strategy.
