# GC Removal Plan: Ownership-Based Lifetime Management

**Date**: 2025-11-15
**Strategy**: RAII with std::unique_ptr/std::shared_ptr + Scope-based Lifetime
**Status**: ⚠️ **EXPERIMENTAL** - Fundamental Architecture Change
**Estimated Effort**: 250-350 hours

---

## Core Principle

**Every object has a clear owner. Lifetime is determined by ownership scope, not manual reference counting.**

- **Single ownership**: `std::unique_ptr<T, CustomDeleter>`
- **Shared ownership**: `std::shared_ptr<T>` (automatic refcounting, not manual)
- **Weak references**: `std::weak_ptr<T>` for cycles
- **Root owner**: `lua_State` owns the object pool/registry
- **No manual refcounting**: Compiler-managed only

---

## 1. Ownership Model Design

### 1.1 lua_State as Root Owner

```cpp
class lua_State : public GCBase<lua_State> {
private:
  // Object pool - lua_State owns all heap objects
  std::unordered_map<
    GCObject*,
    std::unique_ptr<GCObject, LuaDeleter>,
    std::hash<GCObject*>,
    std::equal_to<GCObject*>,
    LuaAllocator<std::pair<const GCObject*, std::unique_ptr<GCObject, LuaDeleter>>>
  > object_pool;

  // Registry table (root of reachability)
  std::unique_ptr<Table> registry;

  // String interning table
  std::unordered_map<
    std::string_view,
    std::unique_ptr<TString>,
    std::hash<std::string_view>,
    std::equal_to<>,
    LuaAllocator<std::pair<const std::string_view, std::unique_ptr<TString>>>
  > string_pool;

public:
  // Allocate new object (returns raw pointer, but stores in pool)
  template<typename T, typename... Args>
  T* allocate(Args&&... args) {
    auto ptr = std::unique_ptr<T, LuaDeleter>(
      new (luaM_malloc_(this, sizeof(T), 0)) T(std::forward<Args>(args)...),
      LuaDeleter{this}
    );
    T* raw = ptr.get();
    object_pool.emplace(raw, std::move(ptr));
    return raw;
  }

  // Remove object from pool (triggers destruction)
  void deallocate(GCObject* obj) {
    object_pool.erase(obj);  // unique_ptr destroyed → object destroyed
  }

  ~lua_State() {
    // Destroy all objects
    object_pool.clear();
    string_pool.clear();
    // All unique_ptrs destroyed → all objects destroyed (RAII)
  }
};
```

**Key insight**: `lua_State::object_pool` owns all objects. When an object becomes unreachable, it's erased from the pool, triggering destruction.

### 1.2 Custom Deleter for Lua Allocator

```cpp
struct LuaDeleter {
  lua_State* L;

  void operator()(GCObject* obj) const {
    if (obj) {
      // Call destructor
      switch (obj->getType()) {
        case LUA_VTABLE: static_cast<Table*>(obj)->~Table(); break;
        case LUA_VSHRSTR:
        case LUA_VLNGSTR: static_cast<TString*>(obj)->~TString(); break;
        // ... other types
      }
      // Free memory via custom allocator
      luaM_free_(L, obj, /* size */);
    }
  }
};
```

### 1.3 TValue: Raw Pointers to Pool Objects

```cpp
class TValue {
private:
  union {
    GCObject* gc;      // Raw pointer into lua_State::object_pool
    lua_Number n;
    lua_Integer i;
    void* p;
    // ...
  } value_;
  lu_byte tt_;

public:
  // Getters return raw pointers (non-owning)
  Table* tableValue() const { return static_cast<Table*>(value_.gc); }
  TString* stringValue() const { return static_cast<TString*>(value_.gc); }

  // Setters just store raw pointer (lua_State owns the object)
  void setTable(Table* t) {
    value_.gc = t;
    tt_ = LUA_VTABLE;
  }
};
```

**Insight**: `TValue` doesn't own objects - it just references them. Ownership is in `lua_State::object_pool`.

---

## 2. Reachability Tracking & Collection

**Challenge**: How do we know when to erase from `object_pool`?

**Solution**: Periodic mark-and-sweep (simplified GC!)

### 2.1 Mark Phase

```cpp
void lua_State::mark_reachable() {
  // Clear all marks
  for (auto& [ptr, obj] : object_pool) {
    obj->setMarked(0);  // White
  }

  // Mark from roots
  mark_object(registry.get());
  mark_stack();
  mark_upvalues();
  // ... mark other roots

  // Propagate marks
  while (has_gray_objects()) {
    propagate_marks();
  }
}

void mark_object(GCObject* obj) {
  if (!obj || obj->isBlack()) return;

  obj->setMarked(BLACKBIT);  // Mark black

  // Mark children
  switch (obj->getType()) {
    case LUA_VTABLE: {
      Table* t = static_cast<Table*>(obj);
      // Mark array part
      for (unsigned i = 0; i < t->arraySize(); i++) {
        mark_value(&t->getArray()[i]);
      }
      // Mark hash part
      for (unsigned i = 0; i < t->nodeSize(); i++) {
        Node* n = t->getNode(i);
        if (!n->isKeyNil()) {
          mark_value(n->getValue());
        }
      }
      // Mark metatable
      mark_object(t->getMetatable());
      break;
    }
    // ... other types
  }
}
```

### 2.2 Sweep Phase

```cpp
void lua_State::sweep() {
  // Erase unmarked objects from pool
  for (auto it = object_pool.begin(); it != object_pool.end(); ) {
    if (it->second->getMarked() == 0) {  // White (unmarked)
      it = object_pool.erase(it);  // Destroy object (RAII)
    } else {
      ++it;
    }
  }
}
```

### 2.3 Periodic Collection

```cpp
void lua_State::collect_garbage() {
  mark_reachable();
  sweep();
}

// Call periodically (e.g., every N allocations)
void lua_State::check_collection() {
  static int alloc_counter = 0;
  if (++alloc_counter > COLLECTION_THRESHOLD) {
    alloc_counter = 0;
    collect_garbage();
  }
}
```

**Wait... this is just GC with different ownership!**

Yes, but with key differences:
- ✅ Objects owned by `std::unique_ptr` (RAII)
- ✅ Destruction automatic (destructors called)
- ✅ No manual list manipulation (std::unordered_map)
- ✅ Simpler mark-and-sweep (no phases, no incremental)
- ✅ No write barriers needed (mark-and-sweep is always complete)

---

## 3. Shared Ownership Cases

### 3.1 String Interning

Strings are **owned by lua_State::string_pool**, but referenced by TValues.

```cpp
class lua_State {
private:
  std::unordered_map<
    std::string_view,
    std::unique_ptr<TString, LuaDeleter>,
    /* ... */
  > string_pool;

public:
  TString* intern_string(const char* str, size_t len) {
    std::string_view key(str, len);
    auto it = string_pool.find(key);

    if (it != string_pool.end()) {
      return it->second.get();  // Existing string
    }

    // Create new string
    auto ptr = std::unique_ptr<TString, LuaDeleter>(
      new (luaM_malloc_(this, sizeof(TString) + len + 1, LUA_VSHRSTR)) TString(str, len),
      LuaDeleter{this}
    );
    TString* raw = ptr.get();
    string_pool.emplace(key, std::move(ptr));
    return raw;
  }
};
```

**Lifetime**: String lives as long as it's in `string_pool`. Removed during sweep if unreachable.

### 3.2 Prototype Sharing

Multiple closures share the same `Proto`. Use **raw pointers** (proto owned by pool).

```cpp
class LClosure {
private:
  Proto* p;  // Raw pointer (lua_State owns the Proto)
  UpVal* upvals[1];

public:
  LClosure(Proto* proto) : p(proto) {}
};
```

**Alternative** (if Proto needs shared_ptr):

```cpp
class lua_State {
private:
  std::vector<std::shared_ptr<Proto>> proto_pool;  // Shared ownership

public:
  std::shared_ptr<Proto> allocate_proto() {
    auto proto = std::allocate_shared<Proto>(LuaAllocator<Proto>{this});
    proto_pool.push_back(proto);
    return proto;
  }
};

class LClosure {
private:
  std::shared_ptr<Proto> p;  // Shared ownership
};
```

**Tradeoff**: `shared_ptr` uses automatic refcounting (acceptable since not manual).

### 3.3 Upvalue Sharing

Multiple closures can share upvalues. Use `std::shared_ptr<UpVal>`.

```cpp
class UpVal {
private:
  TValue* stack_ptr = nullptr;  // Points to stack (NOT owned)
  std::optional<TValue> closed_value;  // Owned value after closure

public:
  bool isOpen() const { return !closed_value.has_value(); }

  TValue* getValue() {
    return isOpen() ? stack_ptr : &closed_value.value();
  }

  void close() {
    if (isOpen()) {
      closed_value = *stack_ptr;
      stack_ptr = nullptr;
    }
  }
};

class LClosure {
private:
  std::shared_ptr<UpVal> upvals[1];  // Shared ownership

public:
  void setUpval(int idx, std::shared_ptr<UpVal> uv) {
    upvals[idx] = uv;
  }
};
```

**Lifetime**: UpVal destroyed when last closure is destroyed (shared_ptr refcount reaches zero).

---

## 4. Handling Circular References

**Problem**: Tables can reference each other circularly.

```lua
local t1 = {}
local t2 = {}
t1.ref = t2
t2.ref = t1
t1, t2 = nil, nil
-- Both unreachable, but stored in object_pool
```

**Solution 1**: Periodic mark-and-sweep (described in Section 2)

**Solution 2**: Use `std::weak_ptr` to break cycles (manual annotation)

```lua
-- User explicitly marks weak reference
t1 = {}
t2 = {}
setmetatable(t1, {__mode="v"})  -- Weak values
t1.ref = t2
t2.ref = t1
```

Implementation:

```cpp
class Table {
private:
  struct Value {
    std::variant<
      std::monostate,       // nil
      std::shared_ptr<GCObject>,  // Strong reference
      std::weak_ptr<GCObject>     // Weak reference (user-annotated)
    > data;
  };

  std::vector<Value> array;

public:
  void set_weak(const TValue* key, GCObject* value) {
    // Store as weak_ptr
    array[idx].data = std::weak_ptr<GCObject>(value->shared_from_this());
  }
};
```

**Requires**: `GCObject` inherits from `std::enable_shared_from_this<GCObject>`.

**Problem**: `std::enable_shared_from_this` requires objects to be managed by `shared_ptr`, but we're using `unique_ptr` in `object_pool`!

**Solution**: Change `object_pool` to use `shared_ptr`:

```cpp
class lua_State {
private:
  std::unordered_map<
    GCObject*,
    std::shared_ptr<GCObject>,  // Changed from unique_ptr
    /* ... */
  > object_pool;
};
```

**Implication**: All objects use `shared_ptr` (automatic refcounting), but cycles still need manual annotation or periodic collection.

---

## 5. Incremental Implementation Plan

### Phase 1: Infrastructure (10-15 hours)

**Goals**:
- Implement `LuaAllocator<T>` adapter
- Implement `LuaDeleter` for unique_ptr
- Create `lua_State::object_pool`

### Phase 2: String Pool Refactoring (15-20 hours)

**Goals**:
- Replace GC string allocation with `lua_State::string_pool`
- Use `std::unique_ptr<TString>` for ownership
- Keep string interning semantics

### Phase 3: Table Ownership (25-35 hours)

**Goals**:
- Tables stored in `object_pool`
- Table values are raw pointers (pool owns objects)
- Implement mark-and-sweep for collection

### Phase 4: Closure & Upvalue (30-40 hours)

**Goals**:
- Prototypes use `shared_ptr` (shared by closures)
- Upvalues use `shared_ptr` (shared by closures)
- Handle upvalue closure correctly

### Phase 5: Mark-and-Sweep Implementation (20-30 hours)

**Goals**:
- Implement `mark_reachable()` and `sweep()`
- Periodic collection trigger
- Test with circular references

### Phase 6: Weak Tables (15-25 hours)

**Goals**:
- Implement weak value tables with `weak_ptr`
- Handle ephemeron tables
- Convergence algorithm

### Phase 7: GC Code Removal (10-15 hours)

**Goals**:
- Remove old GC phases (Propagate, Atomic, etc.)
- Remove write barriers
- Remove GC lists (allgc, gray, etc.)
- Keep mark-and-sweep for reachability

### Phase 8: Testing & Optimization (40-60 hours)

**Goals**:
- Full test suite passes
- No memory leaks (Valgrind)
- Performance ≤2.21s
- Optimize mark-and-sweep

---

## 6. Key Differences from Manual Refcounting Plan

| Aspect | Manual Refcount Plan | Ownership-Based Plan |
|--------|---------------------|---------------------|
| **Refcounting** | Intrusive manual refcount | std::shared_ptr automatic |
| **Ownership** | Ambiguous (refcount) | Clear (unique_ptr/shared_ptr) |
| **Object storage** | Intrusive linked lists | std::unordered_map pool |
| **Circular refs** | Periodic cycle detection | Mark-and-sweep |
| **Performance** | +15-35% overhead | +10-25% overhead (less atomic ops) |
| **Complexity** | High (manual increment/decrement) | Medium (mark-and-sweep) |
| **RAII** | Partial (manual tracking) | Full (RAII everywhere) |

---

## 7. Performance Implications

### 7.1 Expected Overhead

| Component | Overhead | Mitigation |
|-----------|----------|------------|
| std::shared_ptr control block | +2 words/object | Use for shared objects only |
| std::unordered_map overhead | +1-2 words/object | Custom allocator |
| Mark-and-sweep pause | +5-15% | Incremental marking |
| Destructor calls | +2-5% | Inline destructors |
| **Total** | **+15-30%** | **Optimizations** |

### 7.2 Optimization Strategies

1. **Use unique_ptr where possible** (strings, single-owner objects)
2. **Minimize shared_ptr** (only for truly shared objects)
3. **Inline destructors** (reduce call overhead)
4. **Incremental mark-and-sweep** (reduce pause time)
5. **Custom allocator pools** (reduce fragmentation)

---

## 8. Critical Challenges

### 8.1 Challenge: Still Need Collection

**Problem**: Even with RAII, we still need mark-and-sweep to detect unreachable cycles.

**Impact**: We haven't actually removed the GC - we've just simplified it and added RAII.

**Mitigation**: At least objects are properly destructed (RAII), and ownership is clear.

### 8.2 Challenge: shared_ptr is Refcounting

**Problem**: `std::shared_ptr` IS reference counting (just automatic).

**Impact**: User said "no manual refcounting", but automatic refcounting is still refcounting.

**Mitigation**: It's automatic (compiler-managed), not manual. Acceptable?

### 8.3 Challenge: Performance Overhead

**Problem**: `shared_ptr` + `unordered_map` adds overhead.

**Impact**: May not meet ≤2.21s target.

**Mitigation**: Extensive profiling and optimization required.

---

## 9. Alternative: Arena Allocator (No Individual Deletion)

If mark-and-sweep is unacceptable, consider **arena allocator**:

```cpp
class lua_State {
private:
  ArenaAllocator arena;

public:
  template<typename T, typename... Args>
  T* allocate(Args&&... args) {
    void* mem = arena.allocate(sizeof(T));
    return new (mem) T(std::forward<Args>(args)...);
  }

  ~lua_State() {
    // Destroy all objects in arena
    for (Object* obj : arena.all_objects()) {
      obj->~Object();  // Call destructor
    }
    arena.deallocate_all();  // Free entire arena
  }
};
```

**Advantages**:
- ✅ Fast allocation (bump pointer)
- ✅ Fast destruction (batch)
- ✅ True RAII (destructors called)
- ✅ No GC needed

**Disadvantages**:
- ❌ No individual object deletion
- ❌ Memory accumulates until lua_close()
- ❌ Long-running scripts leak memory

**Verdict**: Only viable for short-lived scripts.

---

## 10. Recommendation

**Proceed with Ownership-Based Plan** using:

1. ✅ **std::unique_ptr** for single-owned objects (strings in pool)
2. ✅ **std::shared_ptr** for shared objects (prototypes, upvalues)
3. ✅ **Raw pointers** for non-owned references (TValue)
4. ✅ **Mark-and-sweep** for reachability (simplified GC)
5. ✅ **RAII destructors** for all objects
6. ✅ **Custom allocator** integration

**Accept**:
- We still need collection mechanism (mark-and-sweep)
- std::shared_ptr uses automatic refcounting
- Some performance overhead (+15-30%)

**Gain**:
- ✅ Clear ownership semantics
- ✅ RAII destructors called automatically
- ✅ No manual refcount management
- ✅ Modern C++ idioms
- ✅ Better type safety

---

**Status**: Awaiting approval to proceed with Phase 1

**Estimated Total Effort**: 250-350 hours

**Success Criteria**:
- All objects have clear owners
- No manual refcount operations
- Destructors called via RAII
- No memory leaks
- Performance ≤2.21s
