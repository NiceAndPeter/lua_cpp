# Garbage Collector Removal Strategy - Incremental Plan

**Date**: 2025-11-15
**Author**: AI Analysis
**Status**: ⚠️ **EXPERIMENTAL** - High Risk Architectural Change
**Estimated Effort**: 300-400 hours
**Performance Risk**: CRITICAL - Must maintain ≤2.21s (≤1% regression)

---

## Executive Summary

This document outlines an incremental strategy to remove Lua's garbage collector and replace it with RAII-based memory management using C++ smart pointers and strict ownership rules.

**WARNING**: This is a fundamental architectural change that affects Lua's core value semantics. The circular reference problem makes this extremely challenging.

**Critical Constraints**:
- ✅ Must use custom allocator passed to `lua_newstate`
- ✅ Must have proper RAII constructors/destructors
- ✅ Must guarantee no memory leaks
- ✅ Must maintain performance ≤2.21s
- ✅ Must preserve C API compatibility
- ⚠️ May need to restrict Lua semantics (no circular table references?)

---

## Table of Contents

1. [Current GC Architecture Analysis](#1-current-gc-architecture-analysis)
2. [Fundamental Challenges](#2-fundamental-challenges)
3. [Proposed Solutions](#3-proposed-solutions)
4. [Custom Allocator Integration](#4-custom-allocator-integration)
5. [Ownership Model Design](#5-ownership-model-design)
6. [Incremental Implementation Phases](#6-incremental-implementation-phases)
7. [Testing Strategy](#7-testing-strategy)
8. [Rollback Plan](#8-rollback-plan)
9. [Performance Benchmarking](#9-performance-benchmarking)

---

## 1. Current GC Architecture Analysis

### 1.1 GC-Managed Object Types

All 9 types inherit from `GCBase<Derived>`:

| Type | Purpose | Reference Pattern | Complexity |
|------|---------|-------------------|------------|
| **TString** | Strings | Interned in hash table | HIGH - shared identity |
| **Table** | Hash tables | Can ref any object | CRITICAL - cycles |
| **Proto** | Function prototypes | Shared by closures | MEDIUM - shared |
| **UpVal** | Upvalues | Shared by closures | HIGH - stack refs |
| **LClosure** | Lua closures | Refs proto + upvals | HIGH - sharing |
| **CClosure** | C closures | Refs upvalues | MEDIUM |
| **Udata** | Userdata | Can have metatable | MEDIUM - finalizers |
| **lua_State** | Threads/coroutines | Owns stack | HIGH - root owner |
| *(no 9th type - only 8 GC types)* | | | |

### 1.2 Current Allocation Flow

```
luaC_newobj(L, tt, size)
  ↓
1. Allocate via luaM_malloc_(L, size, tt)
   ↓
2. Initialize GCObject header (next, tt, marked)
   ↓
3. Link into g->allgc list
   ↓
4. Placement new to construct object
   ↓
5. Return GCObject* (cast to actual type)
```

**Files**: `lgc.cpp:354-378`, placement new operators in `lobject.h`

### 1.3 Current GC Lists (16 concurrent lists)

```cpp
// From GCObjectLists subsystem:
GCObject *allgc;       // All collectable objects
GCObject *finobj;      // Objects with finalizers
GCObject *gray;        // Gray objects (mark phase)
GCObject *grayagain;   // Objects to revisit
GCObject *weak;        // Weak-value tables
GCObject *ephemeron;   // Weak-key tables
GCObject *allweak;     // All-weak tables
GCObject *tobefnz;     // Ready for finalization
GCObject *fixedgc;     // Never collected (interned strings)
// + 7 more for generational GC
```

**Key Insight**: These intrusive linked lists enable O(1) insertion/removal with pointer-to-pointer sweep algorithm. Cannot be replaced with `std::vector` or `std::list`.

### 1.4 GC Phases

```
Pause → Propagate → Atomic → SweepAllGC → SweepFinObj →
        SweepToBeFnz → SweepEnd → CallFin → Pause
```

Each phase:
- **Propagate**: Incremental marking of gray objects
- **Atomic**: Final marking without interruption
- **Sweep**: Free white (unmarked) objects
- **CallFin**: Execute `__gc` finalizers

---

## 2. Fundamental Challenges

### 2.1 Circular References (CRITICAL)

**Problem**: Lua allows arbitrary object graphs with cycles.

```lua
local t1 = {}
local t2 = {}
t1.ref = t2
t2.ref = t1
-- Both t1 and t2 are now unreachable except through each other
```

**With GC**: Tri-color marking finds both unreachable → sweep collects both.

**With std::shared_ptr**: Reference count never reaches zero → **MEMORY LEAK**.

**Possible Solutions**:
1. ✅ **Reference counting + cycle detection** (expensive, complex)
2. ✅ **Ownership hierarchy** (breaks Lua semantics)
3. ✅ **Weak references** (requires programmer discipline)
4. ❌ **Manual memory management** (defeats RAII goal)

### 2.2 String Interning (HIGH)

**Problem**: Strings are interned in a hash table for identity semantics.

```cpp
// Current: stringtable with intrusive linked lists
TString **hash;  // Array of bucket heads
int nuse;        // Number of strings
int size;        // Number of buckets

// Each bucket: TString → TString → TString → NULL
```

**String identity**:
```lua
local s1 = "hello"
local s2 = "hello"
assert(s1 == s2)  -- TRUE (same pointer!)
```

**With std::string**: Each string is a separate object → identity broken.

**Solution**: Custom `InternedString` class with reference counting.

### 2.3 Shared Ownership (HIGH)

**Upvalues**: Multiple closures can share the same upvalue.

```lua
local count = 0
local f1 = function() count = count + 1 end
local f2 = function() return count end
-- Both f1 and f2 share the same upvalue for 'count'
```

**Prototypes**: Multiple closures share the same Proto.

```lua
function make_counter()
  local count = 0
  return function() count = count + 1; return count end
end
local c1 = make_counter()  -- New upvalue, shared proto
local c2 = make_counter()  -- New upvalue, shared proto
```

**Solution**: `std::shared_ptr<Proto>` and `std::shared_ptr<UpVal>`.

### 2.4 Upvalue Stack References (CRITICAL)

**Problem**: Open upvalues point to stack slots, which can be reallocated.

```cpp
class UpVal {
  union {
    TValue *p;         // Points to stack or to u.value
    ptrdiff_t offset;  // Used during stack reallocation
  } v;
  union {
    struct { UpVal *next, **previous; } open;
    TValue value;  // Closed upvalue storage
  } u;
};
```

**Lifecycle**:
1. **Open**: `v.p` points to stack slot (e.g., `L->stack[10]`)
2. **Stack reallocated**: Update `v.p` to new stack location
3. **Function returns**: Close upvalue → copy stack value to `u.value`
4. **Closed**: `v.p` points to `&u.value`

**Challenge**: How to manage lifetime when upvalue outlives stack?

**Solution**: Upvalue must own the value after closure, use `std::unique_ptr` for closed values.

### 2.5 Weak Tables (HIGH)

**Lua weak tables**:
```lua
local weak_vals = setmetatable({}, {__mode="v"})
weak_vals[1] = some_object
-- If some_object has no other references, it's cleared from table
```

**With GC**: Weak references cleared during sweep phase.

**With std::weak_ptr**: Works but requires cycle detection for ephemerons.

**Ephemeron challenge**:
```lua
local eph = setmetatable({}, {__mode="k"})
local k1, k2 = {}, {}
eph[k1] = k2  -- k2 kept alive only if k1 is alive
```

Requires convergence loop (see `GC_PITFALLS_ANALYSIS.md` section 5.2).

### 2.6 Finalizers and Resurrection (MEDIUM)

**Problem**: `__gc` metamethods can resurrect objects.

```lua
local storage = {}
local obj = setmetatable({}, {
  __gc = function(self)
    storage[#storage+1] = self  -- Resurrect!
  end
})
obj = nil  -- Make unreachable
```

**With GC**:
1. Detect unreachable
2. Call `__gc` → obj resurrected
3. Remark phase catches resurrection
4. obj survives

**With RAII**: Destructor cannot resurrect! Once destructor called, object is being destroyed.

**Solution**:
- Disable resurrection (breaking change)
- OR: Delay destruction, run finalizers first, then check references

### 2.7 Performance Requirements (CRITICAL)

**Baseline**: 2.17s for `all.lua` test suite
**Target**: ≤2.21s (≤1% regression)

**GC performance characteristics**:
- **Incremental**: Spreads cost over time
- **Generational**: Most objects die young (fast collection)
- **O(live) marking**: Only traverse reachable objects
- **O(1) allocation**: Simple bump allocator

**Smart pointer overhead**:
- `std::shared_ptr`: 2 words per object (control block pointer)
- Reference count operations: Atomic increment/decrement
- Cache misses: Indirection through control block

**Estimated overhead**: +10-30% performance regression

**Mitigation**:
- Intrusive reference counting (1 word overhead)
- Custom allocator pools (reduce fragmentation)
- Reference count batching

---

## 3. Proposed Solutions

### 3.1 Hybrid Approach: Reference Counting + Cycle Detection

**Strategy**: Use `std::shared_ptr` with custom allocator + periodic cycle detection.

**Advantages**:
✅ RAII semantics
✅ Automatic memory management
✅ Supports shared ownership
✅ Can handle most Lua patterns

**Disadvantages**:
❌ Cycle detection is expensive
❌ Performance overhead (atomic refcounts)
❌ Complex implementation
❌ May not meet performance target

**Cycle Detection Algorithm**: Mark-and-sweep on reference graph (essentially a GC!).

### 3.2 Ownership Hierarchy with Arenas

**Strategy**: lua_State owns all objects via arena allocator.

```cpp
class lua_State {
  ArenaAllocator arena;

  // All objects allocated in arena
  ~lua_State() {
    arena.deallocate_all();  // Destroys all objects
  }
};
```

**Advantages**:
✅ Fast allocation (bump pointer)
✅ Fast deallocation (destroy arena)
✅ Simple lifetime management
✅ Low overhead

**Disadvantages**:
❌ Cannot return values across states
❌ Breaks C API (can't return strings, tables)
❌ No shared ownership
❌ Closures can't outlive creating state

**Verdict**: Too restrictive for Lua semantics.

### 3.3 Intrusive Reference Counting (RECOMMENDED)

**Strategy**: Add reference count to `GCObject`, use custom smart pointers.

```cpp
class GCObject {
protected:
  std::atomic<int> refcount{0};  // Add refcount field
  GCObject* next;                 // Keep for free list
  lu_byte tt;
  lu_byte marked;  // Repurpose for cycle detection
};

template<typename T>
class lua_ptr {
  T* ptr;
public:
  lua_ptr(T* p) : ptr(p) { if (ptr) ptr->addRef(); }
  ~lua_ptr() { if (ptr) ptr->release(); }
  // ... copy, move, operators
};
```

**Advantages**:
✅ Low overhead (1 atomic word per object)
✅ Compatible with custom allocator
✅ RAII semantics
✅ Can coexist with existing code

**Disadvantages**:
❌ Still need cycle detection
❌ Atomic operations overhead
❌ Complex weak reference implementation

**Cycle Detection**: Periodic mark-and-sweep on objects with refcount > 0.

---

## 4. Custom Allocator Integration

### 4.1 Requirements

Must integrate with `lua_Alloc` interface:

```c
typedef void * (*lua_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);
```

### 4.2 std::allocator Adapter

```cpp
template<typename T>
class LuaAllocator {
  lua_State* L;

public:
  using value_type = T;

  LuaAllocator(lua_State* state) : L(state) {}

  T* allocate(std::size_t n) {
    void* p = luaM_malloc_(L, n * sizeof(T), 0);
    if (!p) throw std::bad_alloc();
    return static_cast<T*>(p);
  }

  void deallocate(T* p, std::size_t n) noexcept {
    luaM_free_(L, p, n * sizeof(T));
  }

  template<typename U>
  struct rebind { using other = LuaAllocator<U>; };
};
```

**Usage**:
```cpp
std::vector<TValue, LuaAllocator<TValue>> vec(LuaAllocator<TValue>(L));
std::shared_ptr<Table> t = std::allocate_shared<Table>(LuaAllocator<Table>(L));
```

### 4.3 Intrusive Pointer Adapter

```cpp
template<typename T>
class intrusive_ptr {
  T* ptr = nullptr;

  void add_ref() {
    if (ptr) ptr->refcount.fetch_add(1, std::memory_order_relaxed);
  }

  void release() {
    if (ptr && ptr->refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      // Last reference - destroy object
      ptr->~T();
      luaM_free_(ptr->getState(), ptr, sizeof(T));
    }
  }

public:
  intrusive_ptr() = default;
  intrusive_ptr(T* p) : ptr(p) { add_ref(); }
  ~intrusive_ptr() { release(); }

  // Copy constructor
  intrusive_ptr(const intrusive_ptr& other) : ptr(other.ptr) { add_ref(); }

  // Move constructor
  intrusive_ptr(intrusive_ptr&& other) noexcept : ptr(other.ptr) {
    other.ptr = nullptr;
  }

  // Assignment operators, swap, etc.
  // ...
};
```

---

## 5. Ownership Model Design

### 5.1 String Ownership: Interned with Reference Counting

```cpp
class TString : public GCBase<TString> {
private:
  std::atomic<int> refcount{0};  // NEW
  // ... existing fields

public:
  void addRef() { refcount.fetch_add(1, std::memory_order_relaxed); }

  void release() {
    if (refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      // Remove from string table
      this->remove(L);
      // Destroy
      this->~TString();
      luaM_free_(L, this, this->totalSize());
    }
  }
};

// String table still uses hash table, but with refcounts
using StringPtr = intrusive_ptr<TString>;
```

**Invariant**: String in string table ↔ refcount > 0.

### 5.2 Table Ownership: Shared with Cycle Detection

```cpp
using TablePtr = intrusive_ptr<Table>;

class Table : public GCBase<Table> {
private:
  std::atomic<int> refcount{0};  // NEW
  Value *array;
  Node *node;
  TablePtr metatable;  // Use smart pointer

public:
  void set(lua_State* L, const TValue* key, TValue* value);
  // ...
};
```

**Circular reference handling**:
- Periodic cycle detection (mark-and-sweep on refcounted objects)
- OR: Track potentially cyclic tables in special list
- OR: Use weak references for certain patterns (user must annotate)

### 5.3 Closure Ownership: Shared Proto, Shared Upvalues

```cpp
using ProtoPtr = intrusive_ptr<Proto>;
using UpValPtr = intrusive_ptr<UpVal>;

class LClosure : public GCBase<LClosure> {
private:
  std::atomic<int> refcount{0};
  ProtoPtr p;           // Shared prototype
  UpValPtr upvals[1];   // Shared upvalues

public:
  // ...
};
```

**Prototype sharing**: Many closures can share one Proto.
**Upvalue sharing**: Multiple closures can share upvalues for same outer local.

### 5.4 UpValue Ownership: Complex Lifecycle

```cpp
class UpVal : public GCBase<UpVal> {
private:
  std::atomic<int> refcount{0};
  union {
    TValue *p;  // Points to stack (open) or &u.value (closed)
    ptrdiff_t offset;
  } v;
  union {
    struct { UpVal *next, **previous; } open;  // Open upvalue list
    TValue value;  // Closed value storage (OWNED)
  } u;

  bool isOpen() const { return v.p != &u.value; }

public:
  void close() {
    if (isOpen()) {
      // Copy stack value to owned storage
      u.value = *v.p;
      v.p = &u.value;
      // Remove from open upvalue list
      unlink();
    }
  }
};
```

**Lifecycle**:
1. Created open → points to stack slot (NOT owned)
2. Stack reallocated → update pointer
3. Function returns → close() copies value (NOW owned)
4. Last closure releases → upvalue destroyed

### 5.5 Lua State Ownership: Root Owner

```cpp
class lua_State : public GCBase<lua_State> {
private:
  // Stack is NOT reference counted (owned by state)
  StkIdRel top, stack, stack_last;

  // Open upvalues list (raw pointers - state doesn't own)
  UpVal* openupval;

  // Call info list (owned by state)
  std::vector<CallInfo, LuaAllocator<CallInfo>> callstack;

public:
  ~lua_State() {
    // 1. Close all open upvalues
    for (UpVal* uv = openupval; uv; uv = uv->getNext()) {
      uv->close();
    }

    // 2. Free stack
    luaM_freearray(this, stack.p, stacksize());

    // 3. Callstack destroyed automatically (RAII)
  }
};
```

**Key insight**: State owns stack and call chain, but NOT heap objects.

### 5.6 Weak Reference Handling

```cpp
template<typename T>
class weak_ptr {
  T* ptr = nullptr;  // Raw pointer (doesn't affect refcount)

public:
  weak_ptr(const intrusive_ptr<T>& strong) : ptr(strong.get()) {}

  intrusive_ptr<T> lock() const {
    // Check if object still alive (refcount > 0)
    if (ptr && ptr->refcount.load(std::memory_order_relaxed) > 0) {
      return intrusive_ptr<T>(ptr);  // Increment refcount
    }
    return intrusive_ptr<T>();  // nullptr
  }
};
```

**Problem**: Object can be destroyed between check and increment!

**Solution**: Two-phase reclamation:
1. Mark object for deletion (set refcount to negative)
2. Actual deletion deferred until safe point

---

## 6. Incremental Implementation Phases

### Phase 1: Infrastructure (10-15 hours)

**Goals**:
- Implement custom allocator adapter
- Implement `intrusive_ptr<T>` template
- Add `refcount` field to `GCObject`
- Create RAII test harness

**Files to modify**:
- `src/memory/lmem.h` - Add allocator adapter
- `src/objects/lobject.h` - Add `intrusive_ptr` template and refcount to GCObject
- New: `src/memory/intrusive_ptr.h` - Intrusive pointer implementation

**Testing**:
```cpp
// Test basic refcounting
void test_refcount() {
  lua_State* L = luaL_newstate();
  {
    intrusive_ptr<Table> t1(new (L, LUA_VTABLE) Table());
    assert(t1->refcount == 1);

    intrusive_ptr<Table> t2 = t1;  // Copy
    assert(t1->refcount == 2);

    t2.reset();  // Release
    assert(t1->refcount == 1);
  }
  // t1 destroyed → Table freed
  lua_close(L);
}
```

**Deliverable**: Working `intrusive_ptr` with tests.

---

### Phase 2: String Interning Refactoring (20-30 hours)

**Goals**:
- Convert `TString` to reference counted
- Modify string table to use `intrusive_ptr`
- Update all string creation/destruction sites

**Files to modify**:
- `src/objects/lstring.h` - Add refcount methods to TString
- `src/objects/lstring.cpp` - Update `luaS_new*` functions
- `src/objects/lobject.h` - Update TValue string setters

**Before**:
```cpp
TString* luaS_new(lua_State* L, const char* str) {
  // Allocate via GC
  TString* ts = cast(TString*, luaC_newobj(L, LUA_VSHRSTR, size));
  // Link to string table
  // ...
}
```

**After**:
```cpp
intrusive_ptr<TString> luaS_new(lua_State* L, const char* str) {
  // Check string table first
  intrusive_ptr<TString> existing = find_in_table(L, str);
  if (existing) return existing;  // Increment refcount

  // Allocate new
  TString* ts = static_cast<TString*>(
    luaM_malloc_(L, size, LUA_VSHRSTR)
  );
  new (ts) TString(str, len);  // Placement new
  ts->refcount = 1;  // Initial refcount

  // Add to string table
  add_to_table(L, ts);

  return intrusive_ptr<TString>(ts);
}
```

**Migration strategy**:
1. Keep GC running in parallel
2. Convert one string creation site at a time
3. Test after each conversion
4. When all sites converted, remove GC for strings

**Testing**:
```bash
cd testes
../build/lua strings.lua  # Must pass
```

**Benchmarking**:
```bash
# Must remain ≤2.21s
for i in 1 2 3 4 5; do ../build/lua all.lua 2>&1 | grep "total time:"; done
```

---

### Phase 3: Table Refactoring (30-40 hours)

**Goals**:
- Convert `Table` to reference counted
- Handle table-to-table references
- Implement basic cycle detection

**Files to modify**:
- `src/objects/ltable.h` - Add refcount to Table
- `src/objects/ltable.cpp` - Update table operations
- New: `src/memory/cycle_detector.cpp` - Cycle detection

**TValue changes**:
```cpp
class TValue {
  union {
    intrusive_ptr<Table> h;       // Table reference (auto-managed)
    intrusive_ptr<TString> ts;    // String reference
    lua_Number n;
    lua_Integer i;
    // ...
  } value_;
};
```

**Problem**: Union can't contain non-trivial types like `intrusive_ptr`!

**Solution**: Use `std::variant` or manual placement new/destroy.

```cpp
class TValue {
  Value value_;  // Raw pointers (as before)
  lu_byte tt_;

  // NEW: Helper to manage refcounts
  void incrementRef() {
    if (iscollectable(this)) {
      gcvalue(this)->addRef();
    }
  }

  void decrementRef() {
    if (iscollectable(this)) {
      gcvalue(this)->release();
    }
  }

public:
  void setTable(lua_State* L, Table* t) {
    decrementRef();  // Release old value
    value_.gc = reinterpret_cast<GCObject*>(t);
    tt_ = ctb(LUA_VTABLE);
    incrementRef();  // Add ref to new value
  }

  ~TValue() {
    decrementRef();  // Release on destruction
  }
};
```

**Cycle detection**:

```cpp
// Periodic cycle detection (every N allocations)
void detect_cycles(lua_State* L) {
  // 1. Mark phase: Mark all objects reachable from roots
  mark_from_roots(L);

  // 2. Sweep phase: Objects with refcount > 0 but not marked = cycle
  for (GCObject* o : all_objects) {
    if (o->refcount > 0 && !ismarked(o)) {
      // Found cycle member - add to cycle list
      add_to_cycle_list(o);
    }
  }

  // 3. Collect cycles
  collect_cycle_list(L);
}
```

**Testing**:
```lua
-- Test circular references
local t1 = {}
local t2 = {}
t1.ref = t2
t2.ref = t1
t1, t2 = nil, nil
collectgarbage()  -- Should free both tables
```

---

### Phase 4: Closure & Upvalue Refactoring (40-50 hours)

**Goals**:
- Convert `Proto`, `UpVal`, `LClosure`, `CClosure` to reference counted
- Handle upvalue lifetime correctly
- Support closure sharing

**Key challenge**: Upvalues can point to stack (not owned) or heap (owned).

**UpValue lifecycle**:

```cpp
class UpVal : public GCBase<UpVal> {
private:
  std::atomic<int> refcount{0};
  TValue* stack_ptr = nullptr;  // Points to stack (NOT owned)
  std::unique_ptr<TValue> heap_value;  // Owned value (closed)

public:
  bool isOpen() const { return heap_value == nullptr; }

  TValue* getValue() {
    return isOpen() ? stack_ptr : heap_value.get();
  }

  void close() {
    if (isOpen()) {
      heap_value = std::make_unique<TValue>(*stack_ptr);
      stack_ptr = nullptr;
    }
  }

  void updateStackPtr(TValue* new_ptr) {
    if (isOpen()) stack_ptr = new_ptr;
  }
};
```

**Stack reallocation**:

```cpp
void lua_State::reallocStack(int newsize) {
  TValue* old_stack = stack.p;
  TValue* new_stack = luaM_newvector(this, newsize, TValue);

  // Copy values
  memcpy(new_stack, old_stack, stacksize() * sizeof(TValue));

  // Update all open upvalues
  for (UpVal* uv = openupval; uv; uv = uv->getNext()) {
    ptrdiff_t offset = uv->getValue() - old_stack;
    uv->updateStackPtr(new_stack + offset);
  }

  // Free old stack
  luaM_freearray(this, old_stack, stacksize());
  stack.p = new_stack;
}
```

**Testing**:
```lua
-- Test upvalue sharing
function make_counter()
  local count = 0
  return function() count = count + 1; return count end
end

local c1 = make_counter()
local c2 = make_counter()
assert(c1() == 1)
assert(c1() == 2)
assert(c2() == 1)  -- Different upvalue
```

---

### Phase 5: Weak Table Implementation (30-40 hours)

**Goals**:
- Implement weak value tables
- Implement weak key tables (ephemerons)
- Handle convergence correctly

**Weak value table**:

```cpp
class WeakValueTable : public Table {
  std::vector<weak_ptr<GCObject>> weak_values;

  void compact() {
    // Remove dead weak references
    for (auto it = weak_values.begin(); it != weak_values.end(); ) {
      if (it->expired()) {
        it = weak_values.erase(it);
      } else {
        ++it;
      }
    }
  }
};
```

**Ephemeron table**: More complex - requires convergence loop.

---

### Phase 6: Finalizer Support (20-30 hours)

**Goals**:
- Call `__gc` metamethods on destruction
- Handle finalizer errors gracefully
- Disable resurrection (breaking change!)

**Approach**:

```cpp
class Udata : public GCBase<Udata> {
private:
  std::atomic<int> refcount{0};
  intrusive_ptr<Table> metatable;
  bool finalized = false;

public:
  ~Udata() {
    if (!finalized && metatable) {
      finalized = true;
      call_gc_metamethod(this);
    }
  }
};

void call_gc_metamethod(Udata* ud) {
  // Call __gc in protected mode
  lua_State* L = /* get state */;

  const TValue* tm = fasttm(L, ud->getMetatable(), TM_GC);
  if (tm) {
    // Push finalizer and userdata
    setobj2s(L, L->getTop().p++, tm);
    setuvalue(L, L->getTop().p++, ud);

    // Call in protected mode
    int status = L->pCall(/* ... */);
    if (status != LUA_OK) {
      luaE_warnerror(L, "__gc error");
    }
  }
}
```

**Breaking change**: Resurrection disabled (destructor can't revive object).

---

### Phase 7: GC Removal (10-20 hours)

**Goals**:
- Remove all GC list management code
- Remove mark/sweep algorithms
- Remove GC phases
- Remove GC-related API functions

**Files to delete**:
- Most of `src/memory/lgc.cpp` (keep cycle detection)
- GC-related fields in `global_State`

**Files to modify**:
- `include/lua.h` - Mark `lua_gc()` as deprecated
- `src/core/lapi.cpp` - Stub out GC functions

**Stub implementations**:

```cpp
LUA_API int lua_gc(lua_State *L, int what, ...) {
  switch (what) {
    case LUA_GCCOLLECT:
      // Run cycle detection instead
      detect_cycles(L);
      return 0;

    case LUA_GCCOUNT:
      // Return allocated memory
      return cast_int(G(L)->getTotalBytes() >> 10);

    default:
      return 0;  // Ignore other GC operations
  }
}
```

---

### Phase 8: Testing & Optimization (40-60 hours)

**Goals**:
- Comprehensive testing of all phases
- Memory leak detection (Valgrind)
- Performance optimization
- Address any regressions

**Testing strategy**:

```bash
# 1. Functional tests
cd testes
../build/lua all.lua  # Must print "final OK !!!"

# 2. Memory leak check
valgrind --leak-check=full ../build/lua all.lua
# Must show: All heap blocks were freed -- no leaks are possible

# 3. Performance test (5 runs)
for i in 1 2 3 4 5; do \
  ../build/lua all.lua 2>&1 | grep "total time:"; \
done
# Average must be ≤2.21s

# 4. Stress tests
../build/lua stress/circular_refs.lua
../build/lua stress/deep_nesting.lua
../build/lua stress/many_strings.lua
```

**Performance optimization**:
1. Profile with `perf` to find hotspots
2. Optimize refcount operations (batching, lock-free)
3. Optimize cycle detection (incremental, lazy)
4. Consider lockless data structures for multi-threading

---

## 7. Testing Strategy

### 7.1 Unit Tests

Create new test suite: `testes/refcount.lua`

```lua
-- Test 1: Basic refcounting
do
  local t = {}
  collectgarbage()  -- Should not collect t (in scope)
  assert(t ~= nil)
end
collectgarbage()  -- Should collect t (out of scope)

-- Test 2: Circular references
do
  local t1 = {}
  local t2 = {}
  t1.ref = t2
  t2.ref = t1
end
collectgarbage()  -- Should collect both (cycle detection)

-- Test 3: String interning
do
  local s1 = "hello"
  local s2 = "hello"
  assert(s1 == s2)  -- Same object
end

-- Test 4: Upvalue sharing
do
  local count = 0
  local f1 = function() count = count + 1 end
  local f2 = function() return count end
  f1()
  assert(f2() == 1)
end

-- Test 5: Weak tables
do
  local t = setmetatable({}, {__mode="v"})
  t[1] = {}
  collectgarbage()
  assert(t[1] == nil)  -- Weak value collected
end
```

### 7.2 Integration Tests

Run full test suite:

```bash
cd testes
for test in *.lua; do
  echo "Running $test..."
  ../build/lua "$test" || echo "FAILED: $test"
done
```

Must pass:
- `gc.lua` - Garbage collection tests (adapt for refcount)
- `gengc.lua` - Generational GC tests (adapt for cycle detection)
- `all.lua` - Complete test suite

### 7.3 Memory Leak Detection

```bash
valgrind --leak-check=full --show-leak-kinds=all \
  --track-origins=yes \
  ../build/lua all.lua
```

Must show: **0 bytes in 0 blocks definitely lost**

### 7.4 Performance Regression Testing

```bash
# Baseline (current GC)
git checkout main
cmake --build build
cd testes
for i in 1 2 3 4 5; do \
  ../build/lua all.lua 2>&1 | grep "total time:"; \
done
# Record average

# After refcount
git checkout refcount-branch
cmake --build build
cd testes
for i in 1 2 3 4 5; do \
  ../build/lua all.lua 2>&1 | grep "total time:"; \
done
# Compare average (must be ≤1% regression)
```

---

## 8. Rollback Plan

If any phase fails or exceeds performance budget:

1. **Revert to last working phase**
   ```bash
   git revert HEAD~N  # Revert last N commits
   git push -f origin branch-name
   ```

2. **Analyze failure**
   - Performance profiling
   - Memory leak analysis
   - Functional test failures

3. **Redesign problematic phase**
   - Identify bottleneck
   - Alternative approach
   - Incremental fix

4. **Retry phase with improvements**

**Critical revert points**:
- After Phase 2: If string refcounting fails
- After Phase 3: If cycle detection is too slow
- After Phase 4: If upvalue management fails
- After Phase 7: If overall performance > 2.21s

---

## 9. Performance Benchmarking

### 9.1 Baseline Measurements

**Current GC performance** (from CLAUDE.md):
- Baseline: 2.17s average
- Target: ≤2.21s (≤1% regression)
- Current: 2.08s average (4% faster after SRP refactoring)

**New target**: ≤2.16s (maintain current performance)

### 9.2 Expected Overheads

| Component | Overhead | Mitigation |
|-----------|----------|------------|
| Refcount increment/decrement | +5-10% | Intrusive refcount (no indirection) |
| Atomic operations | +3-5% | Batching, relaxed ordering |
| Cycle detection | +5-15% | Incremental, lazy |
| Smart pointer indirection | +2-5% | Inline, compiler optimization |
| **Total estimated** | **+15-35%** | **Optimizations bring to +10%?** |

**Risk**: May not meet performance target!

### 9.3 Optimization Strategies

1. **Lockless refcounting**
   ```cpp
   void addRef() noexcept {
     refcount.fetch_add(1, std::memory_order_relaxed);  // Relaxed ordering
   }

   void release() noexcept {
     if (refcount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
       destroy();
     }
   }
   ```

2. **Refcount batching**
   ```cpp
   // Batch refcount operations in loop
   for (int i = 0; i < 1000; i++) {
     temp_refs[i] = table[i];  // Don't increment yet
   }
   // Batch increment
   for (auto& ref : temp_refs) {
     ref->addRef();
   }
   ```

3. **Lazy cycle detection**
   ```cpp
   // Only detect cycles when memory pressure high
   if (allocated_bytes > threshold) {
     detect_cycles();
   }
   ```

4. **Compiler optimizations**
   ```cpp
   // Inline everything
   __attribute__((always_inline)) void addRef() { /* ... */ }

   // Link-time optimization
   cmake -DLUA_ENABLE_LTO=ON
   ```

---

## 10. Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| **Circular ref leaks** | HIGH | CRITICAL | Cycle detection algorithm |
| **Performance regression** | HIGH | CRITICAL | Extensive profiling, optimization |
| **Upvalue lifetime bugs** | MEDIUM | HIGH | Careful lifecycle management |
| **String identity broken** | LOW | HIGH | Keep string interning |
| **API compatibility** | MEDIUM | MEDIUM | Keep API, change internals |
| **Finalizer resurrection** | HIGH | MEDIUM | Disable resurrection (breaking) |
| **Weak table convergence** | MEDIUM | MEDIUM | Implement ephemeron algorithm |
| **Memory leaks** | HIGH | CRITICAL | Valgrind, ASAN, extensive testing |
| **Crashes** | MEDIUM | CRITICAL | Extensive testing, ASAN |
| **Schedule overrun** | HIGH | HIGH | Incremental approach, rollback |

**Overall risk level**: **VERY HIGH** ⚠️

**Recommendation**: Proceed with extreme caution, frequent testing, and be prepared to abandon if performance target cannot be met.

---

## 11. Alternative Approaches (If Main Plan Fails)

### 11.1 Hybrid GC + Refcount

Keep GC for tables (allow cycles), use refcount for strings/closures.

**Advantages**:
- Lower risk
- Better performance
- Handles cycles correctly

**Disadvantages**:
- Not full RAII
- Still have GC complexity

### 11.2 Arena Allocator with Explicit Ownership

Use arena allocator for each script execution, free entire arena after.

**Advantages**:
- Very fast allocation
- Simple lifecycle
- No refcounting overhead

**Disadvantages**:
- Can't return values across scripts
- Breaks Lua semantics

### 11.3 Conservative GC

Replace tri-color marking with conservative mark-and-sweep.

**Advantages**:
- Simpler implementation
- No write barriers
- Still automatic

**Disadvantages**:
- Not RAII
- Still have GC

---

## 12. Conclusion

Removing Lua's garbage collector and replacing it with RAII-based memory management is an extremely challenging task that requires:

1. ✅ **Intrusive reference counting** for most objects
2. ✅ **Cycle detection** algorithm (essentially a GC!)
3. ✅ **Custom allocator integration** with `std::shared_ptr`/`intrusive_ptr`
4. ✅ **Careful lifetime management** for upvalues, strings, closures
5. ⚠️ **Breaking changes** (disable resurrection, possibly restrict circular tables)
6. ⚠️ **Significant performance overhead** (+10-35% estimated)

**Estimated total effort**: 300-400 hours

**Success criteria**:
- Zero memory leaks (Valgrind clean)
- All tests pass (testes/all.lua)
- Performance ≤2.21s (ideally ≤2.16s)
- C API compatibility maintained

**Critical decision points**:
- After Phase 2: Is string refcount working?
- After Phase 3: Is cycle detection fast enough?
- After Phase 4: Are upvalues managed correctly?
- After Phase 8: Does overall performance meet target?

**Recommendation**: Proceed incrementally with this plan, but be prepared to pivot to Alternative Approach 11.1 (Hybrid GC + Refcount) if full GC removal proves too costly performance-wise.

---

**Document Version**: 1.0
**Last Updated**: 2025-11-15
**Status**: Draft - Awaiting approval to proceed with Phase 1

**Related Documents**:
- `GC_PITFALLS_ANALYSIS.md` - Detailed GC architecture analysis
- `MEMORY_ALLOCATION_ARCHITECTURE.md` - Custom allocator documentation
- `CLAUDE.md` - Project status and guidelines
