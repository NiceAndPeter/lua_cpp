# GC Minimization Plan: Performance Optimization & Overhead Reduction

**Date**: 2025-11-21
**Author**: AI Analysis
**Purpose**: Reduce GC overhead and recover from 9.3% performance regression
**Status**: PLANNING - Ready for phased implementation

---

## Executive Summary

### Current Situation

**Performance Regression**:
- **Baseline**: 4.20s avg (established Nov 16, 2025)
- **Current**: 4.62s avg (10% over target)
- **Target**: ≤4.33s (≤3% regression tolerance)
- **Gap**: -0.29s (-6.9% regression) - **UNACCEPTABLE**

**GC Architecture Status**:
- ✅ **Modularization complete**: 6 modules extracted (Phase 101+)
- ✅ **Code organization**: lgc.cpp reduced from 1,950 to 936 lines
- ⚠️ **Performance**: Regression likely caused by modularization overhead
- ❌ **Hot path optimization**: Not yet optimized after module extraction

### Problem Analysis

**Root Causes of Regression**:
1. **Function call overhead** - Module boundaries introduced function calls where there were none
2. **Non-inlined critical paths** - Marking, sweeping, barriers may not be inlined
3. **Lost compiler optimizations** - Cross-file optimizations may be lost
4. **Increased instruction cache pressure** - More functions = worse I-cache locality

### Strategic Approach

This plan focuses on **6 optimization strategies**:

1. **🔥 Hot Path Optimization** (HIGH PRIORITY) - Inline critical functions, optimize barriers
2. **📊 GC Tuning** (MEDIUM PRIORITY) - Adjust parameters to reduce GC frequency
3. **⚡ Allocation Fast Path** (HIGH PRIORITY) - Zero-overhead allocation when GC not needed
4. **🎯 Work Reduction** (MEDIUM PRIORITY) - Reduce marking/sweeping work per cycle
5. **🔬 Profiling & Measurement** (HIGH PRIORITY) - Identify actual bottlenecks
6. **🏗️ Compiler Optimizations** (LOW PRIORITY) - LTO, PGO, attribute annotations

**Expected Outcome**: Recover full performance (≤4.33s), ideally reach or beat 4.20s baseline

---

## Table of Contents

1. [Performance Regression Analysis](#1-performance-regression-analysis)
2. [Hot Path Optimization (P0)](#2-hot-path-optimization-p0)
3. [GC Parameter Tuning (P1)](#3-gc-parameter-tuning-p1)
4. [Allocation Fast Path (P0)](#4-allocation-fast-path-p0)
5. [Work Reduction Strategies (P1)](#5-work-reduction-strategies-p1)
6. [Profiling & Measurement (P0)](#6-profiling--measurement-p0)
7. [Compiler Optimizations (P2)](#7-compiler-optimizations-p2)
8. [Implementation Roadmap](#8-implementation-roadmap)
9. [Risk Assessment](#9-risk-assessment)
10. [Success Metrics](#10-success-metrics)

---

## 1. Performance Regression Analysis

### 1.1 Modularization Impact

**Before Modularization** (Single file: lgc.cpp, 1,950 lines):
- ✅ All functions in one translation unit
- ✅ Aggressive compiler inlining within file
- ✅ Excellent instruction cache locality
- ✅ Link-time optimization effective
- ⚠️ Poor code organization

**After Modularization** (7 files: lgc.cpp + 6 modules, 2,677 lines total):
- ✅ Better code organization (40% improvement)
- ✅ Clearer separation of concerns
- ❌ Function calls across module boundaries
- ❌ Reduced inlining opportunities
- ❌ Worse instruction cache locality
- ❌ **9.3% performance regression** (4.62s vs 4.20s)

### 1.2 Critical Hot Paths

**VM Execution Path** (Most frequent):
```
lvm.cpp: VM instruction loop
  → luaC_step()           [Every N allocations]
    → singlestep()        [gc_collector.cpp]
      → propagatemark()   [gc_marking.cpp]
        → reallymarkobject() [gc_marking.cpp]
```

**Object Allocation Path** (Very frequent):
```
lvm.cpp / ltable.cpp / etc.: Object creation
  → luaC_newobj()         [lgc.cpp]
    → luaC_checkGC()      [Threshold check]
      → luaC_step()       [Incremental GC work]
```

**Write Barrier Path** (Extremely frequent):
```
lvm.cpp / ltable.cpp: Object pointer write
  → luaC_barrier()        [lgc.h - MACRO]
    → luaC_barrier_()     [lgc.cpp]
      → reallymarkobject() [gc_marking.cpp]
```

**Frequency Estimates** (per all.lua test suite):
- VM instructions: ~100M+ executions
- Object allocations: ~10M+ calls
- Write barriers: ~50M+ calls
- GC steps: ~10K+ calls
- Full GC cycles: ~10-100 cycles

**Performance Impact**:
- 1% overhead on barriers = ~0.5% total regression
- 1% overhead on allocation = ~0.2% total regression
- 10% overhead on GC steps = ~1% total regression

### 1.3 Suspected Bottlenecks

**Top 5 Likely Causes** (ordered by probability):

1. **❌ Non-inlined `reallymarkobject()`** (gc_marking.cpp)
   - Called from every barrier
   - Called from every propagatemark iteration
   - ~1M+ calls per test suite
   - **Impact**: 2-3% if not inlined

2. **❌ Non-inlined `propagatemark()`** (gc_marking.cpp)
   - Called from singlestep() loop
   - ~10K+ calls per test suite
   - **Impact**: 1-2% if not inlined

3. **❌ Function call overhead in `singlestep()`** (gc_collector.cpp)
   - Calls into gc_marking, gc_sweeping, gc_weak modules
   - Previously all inline within single file
   - **Impact**: 1-2% cumulative

4. **❌ Lost cross-module optimizations**
   - Compiler can't see through module boundaries
   - Missed constant propagation opportunities
   - **Impact**: 1-2% cumulative

5. **❌ Increased instruction cache pressure**
   - 7 files vs 1 file = more code pages
   - More function boundaries = worse locality
   - **Impact**: 0.5-1%

**Total Estimated Impact**: 5.5-10% (matches observed 9.3% regression!)

---

## 2. Hot Path Optimization (P0)

### 2.1 Critical Function Inlining

**Goal**: Force inlining of top 10 hot functions

**Phase 2.1: Inline Annotations**

Add `__attribute__((always_inline))` or `inline` + header placement:

```cpp
// gc_marking.h
inline __attribute__((always_inline))
void reallymarkobject(global_State* g, GCObject* o) {
    // Move implementation to header for guaranteed inlining
    lua_assert(iswhite(o) && !isdead(g, o));
    set2gray(o);

    switch (o->getType()) {
        case LUA_VSTRING:
            set2black(o);  // Strings have no children
            break;
        case LUA_VUSERDATA:
            markUserdata(g, gco2u(o));
            break;
        case LUA_VTABLE:
            linkgclist(gco2t(o), g->getGray());
            break;
        // ... etc
    }
}
```

**Top 10 Functions to Inline**:
1. ✅ `reallymarkobject()` - **CRITICAL** (called from every barrier)
2. ✅ `propagatemark()` - **CRITICAL** (called in GC loop)
3. ✅ `luaC_barrier_()` - **CRITICAL** (write barrier hot path)
4. ✅ `luaC_barrierback_()` - **CRITICAL** (backward barrier)
5. ✅ `luaC_step()` - **HIGH** (incremental step)
6. ✅ `sweeplist()` - **HIGH** (sweep hot path)
7. ✅ `traversetable()` - **MEDIUM** (table marking)
8. ✅ `propagateall()` - **MEDIUM** (gray list processing)
9. ✅ `set2gray()` / `set2black()` - **HIGH** (already inline, verify)
10. ✅ `iswhite()` / `isblack()` - **HIGH** (already inline, verify)

**Implementation Strategy**:
- Move implementations to headers (gc_marking.h, gc_sweeping.h, etc.)
- Mark with `inline __attribute__((always_inline))`
- Keep .cpp files for non-hot functions only
- Measure after each function moved

**Expected Impact**: **3-5% performance recovery**

**Effort**: 15-20 hours
**Risk**: LOW (can revert if no improvement)

---

### 2.2 Barrier Macro Optimization

**Current Implementation** (lgc.h):
```cpp
#define luaC_barrier(L,p,v) (  \
    (iswhite(gcvalue(v)) && isblack(p)) ? \
    luaC_barrier_(L,obj2gco(p),gcvalue(v)) : cast_void(0))
```

**Problem**: Function call overhead if condition true (~10% of barrier calls)

**Optimization 1: Inline luaC_barrier_() body into macro**

```cpp
#define luaC_barrier(L,p,v) do { \
    if (iswhite(gcvalue(v)) && isblack(p)) { \
        global_State* g = G(L); \
        GCObject* o = obj2gco(p); \
        GCObject* v_obj = gcvalue(v); \
        \
        if (g->keepInvariant()) { \
            /* Inline reallymarkobject */ \
            lua_assert(iswhite(v_obj) && !isdead(g, v_obj)); \
            set2gray(v_obj); \
            /* Link to gray list based on type */ \
            if (v_obj->getType() == LUA_VSTRING) { \
                set2black(v_obj); /* No children */ \
            } else { \
                linkgclist(v_obj, g->getGray()); \
            } \
            \
            if (isold(o)) { \
                lua_assert(!isold(v_obj)); \
                setage(v_obj, GCAge::Old0); \
            } \
        } else { \
            lua_assert(g->isSweepPhase()); \
            if (g->getGCKind() != GCKind::GenerationalMinor) \
                makewhite(g, o); \
        } \
    } \
} while(0)
```

**Problem with Macro Approach**: Code bloat, complex logic in macro (maintainability issues)

**Better Optimization 2: Always-inline function**

```cpp
// lgc.h
inline __attribute__((always_inline))
void luaC_barrier_(lua_State* L, GCObject* o, GCObject* v) {
    global_State* g = G(L);

    if (g->keepInvariant()) {
        reallymarkobject(g, v);  // Also inlined

        if (isold(o)) {
            lua_assert(!isold(v));
            setage(v, GCAge::Old0);
        }
    } else {
        lua_assert(g->isSweepPhase());
        if (g->getGCKind() != GCKind::GenerationalMinor)
            makewhite(g, o);
    }
}
```

**Expected Impact**: **1-2% performance recovery**

**Effort**: 5-8 hours
**Risk**: LOW

---

### 2.3 Allocation Fast Path

**Goal**: Zero-overhead allocation when GC doesn't need to run

**Current Implementation** (lgc.cpp):
```cpp
GCObject* luaC_newobj(lua_State* L, int tt, size_t sz) {
    global_State* g = G(L);
    GCObject* o = obj2gco(cast(GCObject *, luaM_newobject(L, novariant(tt), sz)));
    o->marked = luaC_white(g);
    o->tt = tt;
    o->next = g->allgc;
    g->allgc = o;

    return o;
}
```

**Problem**: Always adds to allgc list, always sets marked field

**Optimization: Inline with GC check**

```cpp
// lgc.h
inline __attribute__((always_inline))
GCObject* luaC_newobj(lua_State* L, int tt, size_t sz) {
    global_State* g = G(L);

    // Allocate object
    GCObject* o = obj2gco(cast(GCObject*, luaM_newobject(L, novariant(tt), sz)));

    // Fast path: Just initialize and link
    o->marked = luaC_white(g);
    o->tt = tt;
    o->next = g->allgc;
    g->allgc = o;

    return o;
}
```

**Further Optimization: Check GC threshold BEFORE allocation**

```cpp
// Before heavy allocations, check if GC needed
inline bool luaC_shouldRunGC(global_State* g) noexcept {
    return g->getGCDebt() > 0;
}

// In allocation-heavy code:
if (luaC_shouldRunGC(g)) {
    luaC_step(L);  // Do GC work before allocating
}
// Now allocate without GC check
```

**Expected Impact**: **0.5-1% performance recovery**

**Effort**: 8-10 hours
**Risk**: LOW

---

## 3. GC Parameter Tuning (P1)

### 3.1 Current Default Parameters

**GC Thresholds** (lstate.cpp initialization):
```cpp
g->GCdebt = 0;
g->GCpause = LUAI_GCPAUSE;      // 200 (2.0x)
g->GCstepmul = LUAI_GCMUL;      // 100 (1.0x)
g->GCstepsize = LUAI_GCSTEPSIZE; // 13 KB
```

**What These Mean**:
- `GCpause = 200`: Wait until memory usage reaches 2.0x before starting GC
- `GCstepmul = 100`: For each KB allocated, do 1 KB of GC work
- `GCstepsize = 13 KB`: Minimum work per step

### 3.2 Problem: Too Frequent GC

**Hypothesis**: GC running too often, wasting cycles

**Diagnostic**:
```cpp
// Add counters in global_State
size_t gc_step_count;       // Total GC steps executed
size_t gc_full_cycles;      // Full GC cycles completed
size_t gc_objects_marked;   // Objects marked
size_t gc_objects_swept;    // Objects freed

// Log at end of test
printf("GC Stats:\n");
printf("  Steps: %zu\n", g->gc_step_count);
printf("  Full cycles: %zu\n", g->gc_full_cycles);
printf("  Objects marked: %zu\n", g->gc_objects_marked);
printf("  Objects swept: %zu\n", g->gc_objects_swept);
```

**Tuning Strategy 1: Increase GCpause** (delay GC start)
```cpp
g->GCpause = 300;  // 3.0x instead of 2.0x
// Benefit: 33% fewer GC cycles
// Cost: 50% more peak memory
```

**Tuning Strategy 2: Increase GCstepsize** (larger steps, fewer interruptions)
```cpp
g->GCstepsize = 26;  // 26 KB instead of 13 KB
// Benefit: Fewer GC interruptions (better cache locality)
// Cost: Slightly larger pauses
```

**Tuning Strategy 3: Decrease GCstepmul** (less aggressive incremental GC)
```cpp
g->GCstepmul = 80;  // 0.8x instead of 1.0x
// Benefit: Less GC work per allocation
// Cost: Longer GC cycles (more total steps)
```

**Expected Impact**: **1-3% performance improvement** (depends on workload)

**Effort**: 10-15 hours (including measurement)
**Risk**: MEDIUM (affects memory usage)

---

### 3.3 Adaptive GC Tuning

**Idea**: Adjust parameters based on runtime behavior

**Strategy: Detect allocation patterns**
```cpp
class AdaptiveGC {
private:
    size_t fast_alloc_phase = 0;  // Count rapid allocations
    size_t slow_alloc_phase = 0;  // Count slow periods

public:
    void onAllocation(global_State* g) {
        fast_alloc_phase++;
        slow_alloc_phase = 0;

        // Many rapid allocations? Delay GC
        if (fast_alloc_phase > 1000) {
            g->GCpause = 300;  // Be more lazy
        }
    }

    void onGCStep(global_State* g) {
        slow_alloc_phase++;
        fast_alloc_phase = 0;

        // Low allocation rate? Be more aggressive
        if (slow_alloc_phase > 100) {
            g->GCpause = 150;  // Collect sooner
        }
    }
};
```

**Expected Impact**: **1-2% improvement** (workload-dependent)

**Effort**: 20-25 hours
**Risk**: HIGH (complex, may introduce instability)

**Recommendation**: **DEFER** until simpler optimizations exhausted

---

## 4. Allocation Fast Path (P0)

### 4.1 Current Allocation Overhead

**Every allocation calls** (lmem.cpp):
```cpp
void* luaM_newobject(lua_State* L, int tag, size_t size) {
    global_State* g = G(L);

    // GC accounting
    g->totalbytes += size;
    g->GCdebt += size;

    // Check if GC needed
    luaC_checkGC(L);  // May trigger GC step!

    // Actual allocation
    void* block = g->alloc_f(g->alloc_ud, nullptr, 0, size);
    if (block == nullptr && size > 0)
        luaM_toobig(L);

    return block;
}
```

**Problem**: `luaC_checkGC()` called on EVERY allocation

### 4.2 Optimization: Batch GC Checks

**Idea**: Only check GC every N allocations

```cpp
// global_State.h
size_t gc_alloc_counter;     // Allocations since last check
static constexpr size_t GC_CHECK_INTERVAL = 16;  // Check every 16 allocs

// lmem.cpp
void* luaM_newobject(lua_State* L, int tag, size_t size) {
    global_State* g = G(L);

    g->totalbytes += size;
    g->GCdebt += size;

    // Only check GC every N allocations
    if (++g->gc_alloc_counter >= GC_CHECK_INTERVAL) {
        g->gc_alloc_counter = 0;
        luaC_checkGC(L);  // May trigger GC
    }

    void* block = g->alloc_f(g->alloc_ud, nullptr, 0, size);
    if (block == nullptr && size > 0)
        luaM_toobig(L);

    return block;
}
```

**Benefits**:
- 94% reduction in `luaC_checkGC()` calls (1/16 vs 1/1)
- Better instruction cache locality
- Less branch misprediction overhead

**Risks**:
- May accumulate more debt before GC runs
- GC steps may be slightly larger

**Expected Impact**: **1-2% performance improvement**

**Effort**: 5-8 hours
**Risk**: LOW

---

### 4.3 Optimization: Allocation Size Threshold

**Idea**: Only check GC for "large" allocations

```cpp
static constexpr size_t GC_CHECK_SIZE_THRESHOLD = 256;  // 256 bytes

void* luaM_newobject(lua_State* L, int tag, size_t size) {
    global_State* g = G(L);

    g->totalbytes += size;
    g->GCdebt += size;

    // Only check GC for large allocations
    if (size >= GC_CHECK_SIZE_THRESHOLD || g->GCdebt > g->GCstepsize * 4) {
        luaC_checkGC(L);
    }

    void* block = g->alloc_f(g->alloc_ud, nullptr, 0, size);
    if (block == nullptr && size > 0)
        luaM_toobig(L);

    return block;
}
```

**Rationale**: Small allocations (TString, small tables) very frequent but low overhead

**Expected Impact**: **0.5-1% improvement**

**Effort**: 5-8 hours
**Risk**: LOW

---

## 5. Work Reduction Strategies (P1)

### 5.1 Reduce Marking Work

**Strategy 1: Mark less aggressively**

**Current**: Every barrier call marks object immediately

**Optimization**: Batch marking
```cpp
// Accumulate objects to mark
GCObject* pending_mark_list = nullptr;

void luaC_barrier_batched(lua_State* L, GCObject* o, GCObject* v) {
    // Add to pending list instead of marking immediately
    v->next_pending = pending_mark_list;
    pending_mark_list = v;
}

// In luaC_step(), process batched marks
void processBatchedMarks(global_State* g) {
    while (pending_mark_list) {
        GCObject* o = pending_mark_list;
        pending_mark_list = o->next_pending;
        reallymarkobject(g, o);
    }
}
```

**Expected Impact**: **1-2% improvement** (fewer incremental interruptions)

**Effort**: 15-20 hours
**Risk**: MEDIUM (affects GC correctness)

---

**Strategy 2: Skip marking of "known safe" objects**

**Observation**: Interned strings, certain constants never die

**Optimization**: Mark interned strings as black on creation
```cpp
TString* luaS_new(lua_State* L, const char* str, size_t len) {
    // ... create string ...

    if (is_interned) {
        set2black(ts);  // Never needs marking again
        // Add to fixedgc list
        linkgclist(ts, g->fixedgc);
    }

    return ts;
}
```

**Expected Impact**: **0.5-1% improvement** (fewer objects to mark)

**Effort**: 10-12 hours
**Risk**: LOW

---

### 5.2 Reduce Sweeping Work

**Strategy: Lazy sweep with larger chunks**

**Current**: Sweep GCSWEEPMAX (20) objects per step

**Optimization**: Adaptive sweep chunk size
```cpp
// Sweep more objects at once if allocation rate is low
int getSweepChunkSize(global_State* g) {
    if (g->fast_alloc_phase > 0) {
        return GCSWEEPMAX;  // 20 - small increments
    } else {
        return GCSWEEPMAX * 4;  // 80 - larger chunks
    }
}
```

**Expected Impact**: **0.5-1% improvement** (fewer sweep interruptions)

**Effort**: 8-10 hours
**Risk**: LOW

---

### 5.3 Generational Mode Optimization

**Current**: 7 age states with complex transitions

**Optimization**: Simplify to 4 ages (from GC_SIMPLIFICATION_ANALYSIS.md)

```cpp
enum class GCAge : lu_byte {
    Young     = 0,  // New + Survival (merged)
    Old       = 1,  // Old0 + Old1 + Old (merged)
    Touched1  = 2,  // Modified once
    Touched2  = 3   // Modified twice
};
```

**Benefits**:
- Simpler age advancement logic
- Fewer branches in age checking
- Clearer semantics

**Expected Impact**: **0.5-1% improvement** (if generational mode used)

**Effort**: 10-15 hours
**Risk**: MEDIUM (requires careful testing)

---

## 6. Profiling & Measurement (P0)

### 6.1 Comprehensive Profiling

**Goal**: Identify actual bottlenecks, not suspected ones

**Phase 6.1: Add Instrumentation**

```cpp
// gc_profiler.h
class GCProfiler {
public:
    size_t barrier_calls = 0;
    size_t barrier_forward = 0;
    size_t barrier_backward = 0;

    size_t allocation_count = 0;
    size_t allocation_bytes = 0;

    size_t gc_steps = 0;
    size_t gc_full_cycles = 0;

    size_t mark_calls = 0;
    size_t mark_objects = 0;

    size_t sweep_calls = 0;
    size_t sweep_freed = 0;

    uint64_t time_in_gc_ns = 0;
    uint64_t time_in_marking_ns = 0;
    uint64_t time_in_sweeping_ns = 0;

    void report() {
        printf("=== GC Profiler Report ===\n");
        printf("Barriers: %zu total (%zu forward, %zu backward)\n",
               barrier_calls, barrier_forward, barrier_backward);
        printf("Allocations: %zu objects, %zu bytes\n",
               allocation_count, allocation_bytes);
        printf("GC: %zu steps, %zu full cycles\n",
               gc_steps, gc_full_cycles);
        printf("Marking: %zu calls, %zu objects marked\n",
               mark_calls, mark_objects);
        printf("Sweeping: %zu calls, %zu objects freed\n",
               sweep_calls, sweep_freed);
        printf("Time in GC: %.2f ms (%.2f%% of total)\n",
               time_in_gc_ns / 1e6, time_in_gc_ns / total_time_ns * 100);
        printf("  Marking: %.2f ms\n", time_in_marking_ns / 1e6);
        printf("  Sweeping: %.2f ms\n", time_in_sweeping_ns / 1e6);
    }
};
```

**Usage**:
```cpp
// In each hot function:
void luaC_barrier_(lua_State* L, GCObject* o, GCObject* v) {
    PROFILE_INCREMENT(barrier_calls);

    auto start = std::chrono::high_resolution_clock::now();

    // ... actual work ...

    auto end = std::chrono::high_resolution_clock::now();
    PROFILE_ADD_TIME(time_in_gc_ns, end - start);
}
```

**Expected Insights**:
- Which functions consume most time
- How often GC actually runs
- Ratio of marking vs sweeping time
- Barrier call frequency

**Effort**: 15-20 hours
**Risk**: NONE (profiling only, no functional changes)

---

### 6.2 Linux `perf` Profiling

**Command**:
```bash
# Build with debug symbols
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build

# Profile with perf
cd testes
perf record -g --call-graph dwarf ../build/lua all.lua

# Analyze
perf report --stdio | head -100
perf report --hierarchy --stdio | grep -E "lgc|gc_" | head -50
```

**What to Look For**:
- Top functions by CPU time
- Call graph hotspots
- Instruction cache misses (perf stat -e cache-misses)
- Branch mispredictions (perf stat -e branch-misses)

**Effort**: 5-8 hours
**Risk**: NONE

---

### 6.3 Cachegrind / Valgrind Analysis

**Command**:
```bash
valgrind --tool=cachegrind --branch-sim=yes ../build/lua all.lua

# Analyze
cg_annotate cachegrind.out.* | less
```

**Metrics to Check**:
- Instruction cache miss rate (should be <1%)
- Data cache miss rate (should be <5%)
- Branch misprediction rate (should be <2%)

**Effort**: 5-8 hours
**Risk**: NONE

---

## 7. Compiler Optimizations (P2)

### 7.1 Link-Time Optimization (LTO)

**Current**: LTO available but not always enabled

**Optimization**: Force LTO for Release builds

```cmake
# CMakeLists.txt
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)  # Enable LTO

    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-flto=auto)
        add_link_options(-flto=auto -fuse-linker-plugin)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        add_compile_options(-flto=thin)
        add_link_options(-flto=thin)
    endif()
endif()
```

**Expected Impact**: **1-3% improvement** (cross-module inlining)

**Effort**: 2-3 hours
**Risk**: LOW (standard compiler feature)

---

### 7.2 Profile-Guided Optimization (PGO)

**Strategy**: Train compiler on actual workload

**Phase 1: Collect profile**
```bash
# Build with instrumentation
cmake -B build_pgo -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-fprofile-generate"
cmake --build build_pgo

# Run benchmark to collect profile
cd testes
../build_pgo/lua all.lua
```

**Phase 2: Build with profile**
```bash
# Build optimized binary
cmake -B build_opt -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-fprofile-use -fprofile-correction"
cmake --build build_opt

# Benchmark
cd testes
../build_opt/lua all.lua
```

**Expected Impact**: **2-5% improvement** (better branch prediction, inlining decisions)

**Effort**: 10-12 hours
**Risk**: LOW (easy to revert)

---

### 7.3 CPU-Specific Optimizations

**Current**: Generic x86-64 code

**Optimization**: Target specific CPU architecture

```cmake
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # Auto-detect CPU features
    add_compile_options(-march=native -mtune=native)

    # Or target specific architecture
    # add_compile_options(-march=haswell)  # AVX2
    # add_compile_options(-march=skylake)  # AVX2 + better
endif()
```

**Expected Impact**: **0.5-2% improvement** (better instruction selection)

**Effort**: 1-2 hours
**Risk**: LOW (portability affected, but okay for benchmarks)

---

### 7.4 Attribute Annotations

**Goal**: Give compiler more optimization hints

**hot attribute**: Optimize aggressively
```cpp
__attribute__((hot))
void reallymarkobject(global_State* g, GCObject* o) {
    // ...
}
```

**cold attribute**: Don't optimize (save I-cache)
```cpp
__attribute__((cold))
void luaG_runerror(lua_State* L, const char* fmt, ...) {
    // Error handling - rarely executed
}
```

**pure/const attributes**: No side effects
```cpp
__attribute__((pure))
bool iswhite(const GCObject* o) noexcept {
    return testbits(o->marked, WHITEBITS);
}
```

**Expected Impact**: **0.5-1% improvement** (better optimization decisions)

**Effort**: 8-10 hours
**Risk**: LOW

---

## 8. Implementation Roadmap

### Phase 0: Baseline & Profiling (CRITICAL - DO FIRST)

**Goals**:
- Establish accurate baseline performance
- Identify actual bottlenecks through profiling
- Validate hypotheses

**Tasks**:
1. ✅ Add GC profiling instrumentation (Section 6.1)
2. ✅ Run comprehensive profiling (perf, cachegrind)
3. ✅ Analyze results, identify top 5 hotspots
4. ✅ Document baseline metrics

**Deliverables**:
- Profiling report with top hotspots
- Validated optimization priorities
- Baseline metrics document

**Time**: 25-30 hours
**Expected Outcome**: Data-driven optimization roadmap

---

### Phase 1: Hot Path Inlining (HIGHEST PRIORITY)

**Goals**:
- Inline top 10 critical functions
- Eliminate module boundary overhead
- Recover 3-5% performance

**Tasks**:
1. ✅ Move `reallymarkobject()` to gc_marking.h (inline)
2. ✅ Move `propagatemark()` to gc_marking.h (inline)
3. ✅ Move `luaC_barrier_()` to lgc.h (inline)
4. ✅ Move `luaC_barrierback_()` to lgc.h (inline)
5. ✅ Move `sweeplist()` to gc_sweeping.h (inline)
6. ✅ Add `__attribute__((always_inline))` annotations
7. ✅ Benchmark after each function
8. ✅ Document results

**Acceptance Criteria**:
- Performance ≤4.33s (recover to target)
- All tests passing
- No regressions in secondary metrics

**Time**: 20-25 hours
**Expected Outcome**: **3-5% improvement** → 4.48s → 4.39s

---

### Phase 2: Compiler Optimizations (QUICK WINS)

**Goals**:
- Enable LTO for cross-module optimization
- Apply attribute annotations
- Low-effort, high-return optimizations

**Tasks**:
1. ✅ Enable LTO in CMake (Section 7.1)
2. ✅ Add `__attribute__((hot))` to top 10 functions
3. ✅ Add `__attribute__((cold))` to error handlers
4. ✅ Add `-march=native` for benchmark builds
5. ✅ Benchmark

**Acceptance Criteria**:
- Performance ≤4.30s (beat target!)
- All tests passing

**Time**: 5-8 hours
**Expected Outcome**: **1-2% improvement** → 4.39s → 4.26s

---

### Phase 3: Allocation Fast Path (MEDIUM PRIORITY)

**Goals**:
- Reduce allocation overhead
- Batch GC checks
- Optimize hot allocation paths

**Tasks**:
1. ✅ Implement GC check batching (Section 4.2)
2. ✅ Implement size threshold optimization (Section 4.3)
3. ✅ Inline `luaC_newobj()` (Section 2.3)
4. ✅ Benchmark

**Acceptance Criteria**:
- Performance ≤4.22s (beat baseline!)
- All tests passing
- No increase in peak memory usage >10%

**Time**: 15-20 hours
**Expected Outcome**: **1-2% improvement** → 4.26s → 4.18s

---

### Phase 4: GC Parameter Tuning (OPTIONAL)

**Goals**:
- Reduce GC frequency through parameter tuning
- Balance performance vs memory usage

**Tasks**:
1. ✅ Measure current GC frequency (Section 3.2)
2. ✅ Experiment with GCpause = 250, 300
3. ✅ Experiment with GCstepsize = 20, 26
4. ✅ Find optimal parameters
5. ✅ Document tradeoffs

**Acceptance Criteria**:
- Performance ≤4.15s (beat original baseline 4.20s!)
- Memory usage increase ≤25%
- All tests passing

**Time**: 15-20 hours
**Expected Outcome**: **1-2% improvement** → 4.18s → 4.10s

---

### Phase 5: Advanced Optimizations (RESEARCH)

**Goals**:
- Explore advanced techniques
- Push performance limits
- Experimental optimizations

**Tasks**:
1. 🔬 Profile-Guided Optimization (Section 7.2)
2. 🔬 Batched barrier marking (Section 5.1)
3. 🔬 Adaptive GC tuning (Section 3.3)
4. 🔬 Age state simplification (Section 5.3)

**Acceptance Criteria**:
- Performance <4.10s (significant improvement over baseline)
- All tests passing
- Stable under stress testing

**Time**: 40-50 hours
**Expected Outcome**: **1-3% improvement** (uncertain)

---

### Total Timeline

**Phase 0 (Profiling)**: 25-30 hours [CRITICAL]
**Phase 1 (Inlining)**: 20-25 hours [HIGH PRIORITY]
**Phase 2 (Compiler)**: 5-8 hours [QUICK WIN]
**Phase 3 (Allocation)**: 15-20 hours [MEDIUM]
**Phase 4 (Tuning)**: 15-20 hours [OPTIONAL]
**Phase 5 (Advanced)**: 40-50 hours [RESEARCH]

**Total**: 120-153 hours (15-19 weeks at 8 hours/week)

**Recommended Minimum**: Phases 0-3 = 65-83 hours (8-10 weeks)
**Expected Result**: 4.10-4.20s (recover full performance, possibly beat baseline)

---

## 9. Risk Assessment

### 9.1 Technical Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| **Inlining doesn't help** | LOW | HIGH | Profile first (Phase 0), validate each inline |
| **LTO breaks build** | LOW | MEDIUM | Test on multiple compilers, keep fallback |
| **GC tuning hurts workload** | MEDIUM | MEDIUM | Extensive testing, document tradeoffs |
| **Memory usage explodes** | MEDIUM | HIGH | Monitor peak memory, set hard limits |
| **Regressions introduced** | LOW | HIGH | Benchmark after EVERY change, revert if >3% |
| **Optimizations not portable** | LOW | LOW | Keep portable defaults, optimize for benchmark |

### 9.2 Performance Risks

**Worst Case Scenarios**:

1. **Inlining causes code bloat** → Worse I-cache performance
   - **Mitigation**: Measure I-cache misses with cachegrind
   - **Fallback**: Revert specific inlines if bloat detected

2. **LTO slows down build** → Development velocity decrease
   - **Mitigation**: Only enable for Release builds
   - **Fallback**: Keep LTO optional

3. **GC tuning helps benchmark, hurts real workloads**
   - **Mitigation**: Test on multiple workloads, not just all.lua
   - **Fallback**: Keep tuning parameters runtime-configurable

### 9.3 Mitigation Strategy

**Golden Rule**: **Measure, don't guess**

1. ✅ **Always profile before optimizing**
2. ✅ **Benchmark after every change**
3. ✅ **Revert if regression >3%**
4. ✅ **Test on multiple workloads**
5. ✅ **Monitor secondary metrics** (memory, I-cache, branches)

---

## 10. Success Metrics

### 10.1 Primary Metrics

**Performance** (all.lua test suite):
- ✅ **Target**: ≤4.33s (3% tolerance from 4.20s baseline)
- 🎯 **Stretch Goal**: ≤4.20s (match or beat original baseline)
- 🏆 **Ambitious Goal**: ≤4.10s (beat baseline by 2.4%)

**Test Coverage**:
- ✅ All 30+ tests passing ("final OK !!!")
- ✅ Zero crashes or memory errors
- ✅ Zero valgrind warnings

### 10.2 Secondary Metrics

**Memory Usage** (measured at GC pause):
- ✅ Peak memory ≤125% of baseline (tolerable increase)
- ⚠️ Alert if >150% of baseline (unacceptable)

**Instruction Cache**:
- ✅ I-cache miss rate ≤1% (cachegrind)
- ⚠️ Alert if >2% (code bloat)

**Branch Prediction**:
- ✅ Branch misprediction rate ≤2% (perf stat)
- ⚠️ Alert if >5% (branchy code)

### 10.3 Code Quality Metrics

**Maintainability**:
- ✅ No increase in code complexity
- ✅ Inlined functions remain readable (≤30 lines)
- ✅ Comprehensive comments on optimizations

**Documentation**:
- ✅ Every optimization documented with rationale
- ✅ Performance impact measured and recorded
- ✅ Reversion instructions provided

---

## 11. Conclusion & Recommendations

### 11.1 Summary of Analysis

**Problem**: 9.3% performance regression (4.62s vs 4.20s) after GC modularization

**Root Cause**: Module boundaries introduced function call overhead, lost compiler optimizations

**Solution Strategy**: 6-phase approach focusing on:
1. **Hot path inlining** (3-5% expected recovery)
2. **Compiler optimizations** (1-2% expected recovery)
3. **Allocation fast path** (1-2% expected recovery)
4. **GC parameter tuning** (1-2% expected improvement)
5. **Advanced techniques** (1-3% potential improvement)

**Total Expected Recovery**: **7-14% improvement** → 4.00-4.30s final performance

### 11.2 Recommended Immediate Actions

**DO THIS FIRST** (Critical Path):
1. ✅ **Phase 0: Profiling** (25-30 hours)
   - Add instrumentation, run profiling, analyze results
   - Validate optimization priorities with data

2. ✅ **Phase 1: Hot Path Inlining** (20-25 hours)
   - Inline top 10 functions identified by profiling
   - Expected: 3-5% recovery → 4.48s → 4.39s

3. ✅ **Phase 2: Compiler Optimizations** (5-8 hours)
   - Enable LTO, add attributes
   - Expected: 1-2% recovery → 4.39s → 4.26s

**THEN EVALUATE** (Based on Results):
- If ≤4.33s achieved → **SUCCESS**, document and close
- If 4.26-4.33s achieved → Consider Phase 3 (Allocation)
- If >4.33s still → Proceed with Phases 3-4 (Allocation + Tuning)

### 11.3 Key Success Factors

1. **Profile-Driven**: Always measure before optimizing
2. **Incremental**: Small changes, frequent benchmarks
3. **Reversible**: Easy to revert if regression occurs
4. **Data-Driven**: Decisions based on measurements, not intuition
5. **Disciplined**: 3% tolerance strictly enforced

### 11.4 Expected Outcome

**Conservative Estimate** (Phases 0-2 only):
- **Time**: 50-63 hours (6-8 weeks)
- **Result**: 4.26s (1.4% better than target)
- **Confidence**: HIGH (inlining + LTO well-understood)

**Optimistic Estimate** (Phases 0-3):
- **Time**: 65-83 hours (8-10 weeks)
- **Result**: 4.18s (0.5% better than baseline!)
- **Confidence**: MEDIUM (allocation optimizations less certain)

**Ambitious Estimate** (Phases 0-4):
- **Time**: 80-103 hours (10-13 weeks)
- **Result**: 4.10s (2.4% better than baseline)
- **Confidence**: MEDIUM-LOW (GC tuning workload-dependent)

### 11.5 Final Recommendation

**START with Phase 0 (Profiling) immediately**

Only profiling will tell us:
- Which functions are actually hot
- Where the regression truly comes from
- What optimizations will have most impact

Without profiling, we're optimizing blind.

**Target for Initial Implementation**: Phases 0-2 (50-63 hours)
**Expected Result**: ≤4.30s (beat target, close to baseline)
**Risk**: LOW (standard optimizations)

**Then decide**: Continue to Phases 3-4 if needed, or declare success.

---

**Document Version**: 1.0
**Date**: 2025-11-21
**Status**: READY FOR APPROVAL

**Next Steps**:
1. ✅ Review and approve this plan
2. ✅ Begin Phase 0 (Profiling) immediately
3. ✅ Track progress with TodoWrite tool
4. ✅ Update plan based on profiling results

**Related Documents**:
- `CLAUDE.md` - Project guidelines and status
- `GC_SIMPLIFICATION_ANALYSIS.md` - Code organization (already done)
- `GC_PITFALLS_ANALYSIS.md` - GC architecture deep-dive
- `CMAKE_BUILD.md` - Build system configuration
