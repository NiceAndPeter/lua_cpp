# Lua Garbage Collector: Pitfall Analysis & C++ Modernization Risks

**Date**: 2025-11-15
**Purpose**: Comprehensive analysis of GC implementation pitfalls, edge cases, and risks when integrating C++ standard library containers
**Critical Finding**: The GC's linked-list architecture and pointer-to-pointer manipulation are fundamentally incompatible with std:: containers

---

## Executive Summary

The Lua garbage collector uses a **tri-color incremental mark-and-sweep algorithm** with **generational optimization**. Analysis reveals 10 critical pitfall categories and multiple edge cases that must be preserved during C++ modernization.

**Key Constraints for Modernization**:
- ❌ **std::vector/std::list for GC object lists** - Breaks sweep algorithm
- ❌ **std::shared_ptr for GC objects** - Conflicts with tri-color marking
- ❌ **std::unordered_set for object tracking** - Loses traversal order
- ✅ **Custom allocator wrapper** - Safe if preserves exact semantics
- ✅ **std::sort for table sorting** - Safe (non-GC algorithm)
- ✅ **std::span for array views** - Safe (view-only, no ownership)

---

## 1. TRI-COLOR MARKING SYSTEM

### 1.1 Color Encoding (lgc.h:107-113)

The GC uses 8 bits in the `marked` field:

```cpp
Bit 7: TESTBIT (for tests only)
Bit 6: FINALIZEDBIT (has __gc metamethod)
Bit 5: BLACKBIT (fully marked object)
Bit 4: WHITE1BIT (white shade 1)
Bit 3: WHITE0BIT (white shade 0)
Bits 0-2: Age (7 generational age values)
```

**Color States**:
- **WHITE** (0b00011000 or 0b00010000): Not visited - candidate for collection
- **GRAY** (0b00000000): Visited but children not processed - in work queue
- **BLACK** (0b00100000): Fully processed - safe until next cycle

**Critical Property** (lgc.h:138-140):
```cpp
inline bool GCObject::isGray() const noexcept {
  return !testbits(marked, bitmask(BLACKBIT) | WHITEBITS);
}
```
Gray is **absence of color**, not a separate bit! This means:
- Setting any color bit makes object non-gray
- Clearing all color bits makes object gray
- Race conditions in bit manipulation can cause immediate misclassification

### 1.2 Two-White Scheme (lgc.cpp:43-52, 78-83)

**Purpose**: Distinguish objects allocated DURING marking from old dead objects.

**Mechanism** (lgc.cpp:206-208):
```cpp
inline lu_byte otherwhite(const global_State* g) noexcept {
  return g->getCurrentWhite() ^ WHITEBITS;  // Flip both white bits
}
```

**Lifecycle**:
1. **GC cycle N begins**: currentWhite = WHITE0 (0b00001000)
2. **New allocation during marking**: Gets WHITE1 (0b00010000) - the "other" white
3. **Sweep phase**: Only objects with WHITE0 (old white) are dead
4. **Cycle N ends**: Flip white → currentWhite = WHITE1
5. **Cycle N+1 begins**: New objects get WHITE0 again

**Pitfall 1.2.1: Double-Free Risk if White Flips Early**

```cpp
// Thread 1: Sweep ongoing
lu_byte ow = otherwhite(g);  // ow = WHITE1 (0b00010000)

// Thread 2: Atomic phase completes, flips white
g->setCurrentWhite(otherwhite(g));  // now current = WHITE1

// Thread 1: Continues sweep
if (isdeadm(ow, obj->getMarked())) {  // TRUE for both whites now!
  freeobj(L, obj);  // DOUBLE FREE RISK!
}
```

**Mitigation**: Atomic phase runs without interruption; sweep uses cached `ow` value (lgc.cpp:992-1007). But custom threading or callbacks during GC could violate this.

---

## 2. INVARIANT: BLACK → WHITE FORBIDDEN

### 2.1 The Core Invariant (lgc.h:18-30)

> "A black object can never point to a white one."

**Why**: If black→white pointers allowed:
```
1. Object A marked BLACK (fully processed, won't revisit)
2. A writes pointer to WHITE object B
3. Sweep phase runs
4. B is WHITE → collected (B is reachable but collected!)
```

**Enforcement**: Write barriers (lgc.cpp:309-342)

### 2.2 Forward Barrier (lgc.cpp:309-324)

**When**: Black object writes to white object
**Action**: Mark white object gray (or black if no children)

```cpp
void luaC_barrier_(lua_State *L, GCObject *o, GCObject *v) {
  global_State *g = G(L);
  lua_assert(isblack(o) && iswhite(v) && !isdead(g, v) && !isdead(g, o));

  if (g->keepInvariant()) {  /* Propagate or Atomic phase */
    reallymarkobject(g, v);  /* Mark v (white → gray/black) */

    if (isold(o)) {
      lua_assert(!isold(v));  /* white cannot be old */
      setage(v, GCAge::Old0);  /* Preserve generational invariant */
    }
  }
  else {  /* Sweep phase - invariant temporarily broken */
    lua_assert(g->isSweepPhase());
    if (g->getGCKind() != GCKind::GenerationalMinor)
      makewhite(g, o);  /* Mark o white to avoid repeated barriers */
  }
}
```

**Pitfall 2.2.1: Barrier Not Called**

If code writes pointer without barrier:
```cpp
// WRONG!
table->array[10] = white_string;  // No barrier → invariant broken!

// CORRECT:
TValue val;
setsvalue(L, &val, white_string);  // Sets value AND calls barrier
table->set(L, &key, &val);  // Internal barrier check
```

**Critical Locations**: All pointer writes in lobject.h, ltable.cpp, lvm.cpp must use barrier macros.

### 2.3 Backward Barrier (lgc.cpp:331-342)

**When**: Old object modified (in generational mode)
**Action**: Mark object as "touched", add to grayagain list

```cpp
void luaC_barrierback_(lua_State *L, GCObject *o) {
  global_State *g = G(L);
  lua_assert(isblack(o) && !isdead(g, o));

  if (getage(o) == GCAge::Touched2) {
    set2gray(o);  /* Was touched last cycle, now gray */
  } else {
    linkobjgclist(o, *g->getGrayAgainPtr());  /* Add to grayagain */
  }

  if (isold(o))
    setage(o, GCAge::Touched1);  /* Mark as recently modified */
}
```

**Purpose**: In generational mode, old objects are skipped. But if modified, they must be revisited.

**Pitfall 2.3.1: Missed Backward Barrier in Tables**

Tables use backward barrier (lgc.h:373-375):
```cpp
#define luaC_barrierback(L,p,v) (  \
  (isblack(p) && iswhite(v)) ? \
  luaC_barrierback_(L,obj2gco(p)) : cast_void(0))
```

If table modified without barrier:
```cpp
// During generational minor GC:
old_table->node[5].value = young_object;  // No barrier!
// Young object collected prematurely (old_table not revisited)
```

---

## 3. OBJECT LISTS: POINTER-TO-POINTER ARCHITECTURE

### 3.1 List Structure (lstate.h:728-821)

The GC manages **16 concurrent linked lists**:

**Incremental Lists**:
1. `allgc` - All collectable objects
2. `sweepgc` - Current sweep position (**pointer-to-pointer**)
3. `finobj` - Objects with finalizers
4. `gray` - Gray work queue
5. `grayagain` - Objects to revisit in atomic
6. `weak` - Weak-value tables
7. `ephemeron` - Weak-key tables
8. `allweak` - All-weak tables
9. `tobefnz` - Ready for finalization
10. `fixedgc` - Never collected

**Generational Pointers** (additional 6):
11. `survival` - Survived one cycle
12. `old1` - Survived two cycles
13. `reallyold` - Survived 3+ cycles
14-16. Sweep positions for each generation

### 3.2 Pointer-to-Pointer Sweep (lgc.cpp:990-1007)

**Why not `GCObject* sweepgc`?**

Because in-place removal requires updating the *previous node's next pointer*:

```cpp
static GCObject **sweeplist(lua_State *L, GCObject **p, l_mem countin) {
  lu_byte ow = otherwhite(g);
  while (*p != NULL && countin-- > 0) {
    GCObject *curr = *p;  // Current object

    if (isdeadm(ow, curr->getMarked())) {
      *p = curr->getNext();  // Remove: point previous to next
      freeobj(L, curr);
      // p unchanged - now points to next object
    } else {
      curr->setMarked(...);  // Alive - update marking
      p = curr->getNextPtr();  // Advance to next node's 'next' pointer
    }
  }
  return (*p == NULL) ? NULL : p;  // Return resume position
}
```

**Critical Property**: `sweepgc` points to **address of a next pointer**, not an object!

```
allgc ──→ [A] ──→ [B] ──→ [C] ──→ NULL
          ↑
     sweepgc points to &A (location in global_State)

After sweep step 1:
allgc ──→ [A] ──→ [B] ──→ [C] ──→ NULL
                  ↑
             sweepgc points to &A->next

If B is dead:
allgc ──→ [A] ──→ [C] ──→ NULL
                  ↑
             sweepgc points to &A->next (unchanged!)
             *sweepgc now equals C
```

**Pitfall 3.2.1: std::vector/std::list Incompatibility**

If replaced with std::list:
```cpp
// WRONG APPROACH:
std::list<GCObject*> allgc;
auto sweepgc_it = allgc.begin();

// Problem 1: Iterator invalidation after erase
if (isdead(*sweepgc_it)) {
  allgc.erase(sweepgc_it);  // sweepgc_it is now INVALID!
  // Undefined behavior on next access
}

// Problem 2: No pointer-to-pointer
// Can't update previous node without O(n) search or prev() calls
```

**Why std::list::erase() differs**:
- C linked list: Direct pointer manipulation, O(1) removal
- std::list: Iterator-based, invalidates on erase, needs explicit iterator management

**Conclusion**: GC lists **MUST remain intrusive linked lists**.

---

## 4. GENERATIONAL GC: AGE PROGRESSION

### 4.1 Age State Machine (lgc.h:116-124, lgc.cpp:1279-1287)

```cpp
enum class GCAge : lu_byte {
  New       = 0,  // Created this cycle
  Survival  = 1,  // Survived 1 cycle
  Old0      = 2,  // Barrier-aged this cycle (not truly old)
  Old1      = 3,  // Survived 2 cycles
  Old       = 4,  // Truly old (skip in minor GC)
  Touched1  = 5,  // Old object modified this cycle
  Touched2  = 6   // Old object modified last cycle
};

static const GCAge nextage[] = {
  GCAge::Survival,  // New → Survival
  GCAge::Old1,      // Survival → Old1
  GCAge::Old1,      // Old0 → Old1 (both progress to Old1)
  GCAge::Old,       // Old1 → Old
  GCAge::Old,       // Old (stays)
  GCAge::Touched1,  // Touched1 (stays)
  GCAge::Touched2   // Touched2 (stays)
};
```

**Age Transitions**:
```
New ──cycle──→ Survival ──cycle──→ Old1 ──cycle──→ Old (permanent)
                   ↑                 ↑
                   └──── Old0 ───────┘
                   (barrier-promoted)

Old ──modified──→ Touched1 ──cycle──→ Touched2 ──cycle──→ gray
```

### 4.2 Minor vs Major Collection (lgc.cpp:1439-1484)

**Minor (Young) Collection**:
- Scans: `allgc` (new objects) + `survival` generation
- Skips: `old1`, `reallyold` (marked old without traversal)
- Triggers: When `gch.marked >= majorminor * LUAI_MINORMAJOR%`
- Optimization: ~90% of objects die young

**Major Collection**:
- Full scan of all generations
- Uses incremental mark-and-sweep
- Triggered when minor finds too many survivors

**Pitfall 4.2.1: Old→New Pointers in Generational Mode**

```cpp
// Scenario:
Old object O (age=Old, skipped in minor GC)
New object N (age=New, will be scanned)

// Program writes:
O->field = N;  // Forward barrier triggers!

// Barrier action (lgc.cpp:314-317):
reallymarkobject(g, N);  // Mark N
setage(N, GCAge::Old0);  // Promote N to Old0 (prevents collection)
```

**Why Old0 exists**: Prevents premature collection of objects referenced by old objects.

**Edge Case**: If barrier fails to promote:
```
1. Minor GC starts
2. O is old → skipped
3. N is new → marked for collection (no references found)
4. N collected while O→N reference exists!
```

**Safeguard**: Barrier **must** run on every old→young write (lgc.h:359-361).

---

## 5. WEAK TABLES AND EPHEMERONS

### 5.1 Weak Table Categories (lgc.cpp:541-682)

```cpp
switch (getmode(g, h)) {
  case 0: traversestrongtable(g, h); break;  // Normal table
  case 1: traverseweakvalue(g, h); break;    // __mode="v"
  case 2: traverseephemeron(g, h, 0); break; // __mode="k"
  case 3: traverseallweak(g, h); break;      // __mode="kv"
}
```

**Weak-Value Table** (`__mode="v"`):
- Keys are marked normally
- Values are **NOT** marked (weak references)
- After marking, white values are cleared (lgc.cpp:1668-1669)

**Ephemeron Table** (`__mode="k"`):
- Keys are weak
- Values marked **ONLY IF** corresponding key is marked elsewhere
- Requires convergence loop (lgc.cpp:809-828)

### 5.2 Ephemeron Convergence (lgc.cpp:809-828)

```cpp
static void convergeephemerons(global_State *g) {
  int changed;
  int dir = 0;
  do {
    GCObject *w;
    GCObject *next = g->getEphemeron();
    g->setEphemeron(NULL);
    changed = 0;

    while ((w = next) != NULL) {
      Table *h = gco2t(w);
      next = h->getGclist();
      nw2black(h);

      if (traverseephemeron(g, h, dir)) {
        propagateall(g);  // Propagate newly marked values
        changed = 1;  // Must revisit all ephemerons
      }
    }
    dir = !dir;  // Alternate direction (forward/backward)
  } while (changed);
}
```

**Why Convergence Needed**:
```lua
local eph1 = setmetatable({}, {__mode="k"})
local eph2 = setmetatable({}, {__mode="k"})
local k1, k2 = {}, {}

eph1[k1] = k2  -- k2 reachable if k1 marked
eph2[k2] = k1  -- k1 reachable if k2 marked

-- Neither k1 nor k2 marked initially
-- Iteration 1: Neither value marked (no keys marked)
-- If external reference to k1 appears:
--   Iteration 2: k1 marked → eph1[k1] marked → k2 marked
--   Iteration 3: k2 marked → eph2[k2] marked → k1 already marked ✓
-- Converged!
```

**Pitfall 5.2.1: Infinite Loop Risk**

If ephemeron table references itself:
```lua
local eph = setmetatable({}, {__mode="k"})
local k = {}
eph[k] = eph  -- Table is both key and value

-- Convergence:
-- Iteration 1: k marked → eph marked (as value)
-- Iteration 2: eph marked → revisit ephemeron
-- Iteration 3: k already marked → no change
-- Converged ✓ (direction alternation doesn't help, object identity does)
```

**Current Protection**: Object identity - once marked, won't re-mark (lgc.cpp:230, nw2black).

**Theoretical Risk**: If multiple ephemerons create marking cycles AND marking logic changes, could loop.

---

## 6. FINALIZATION SYSTEM

### 6.1 Finalizer Lifecycle (lgc.cpp:1123-1167)

**Phase 1: Detection** (lgc.cpp:1123-1167)
```cpp
void GCObject::checkFinalizer(lua_State* L, Table* mt) {
  if (tofinalize(this) ||                   // Already marked
      gfasttm(g, mt, TM_GC) == NULL ||      // No __gc metamethod
      (g->getGCStp() & GCSTPCLS)) {         // State closing
    return;
  }

  // Move from allgc to finobj
  removeFromList(g->getAllGCPtr());  // Remove from allgc
  linkToList(g->getFinObjPtr());     // Add to finobj
  setMarkedBit(FINALIZEDBIT);        // Mark as finalizable
}
```

**Phase 2: Separation** (lgc.cpp:1594-1622)
```cpp
static void separatetobefnz(global_State *g, int all) {
  for (p = g->getFinObjPtr(); *p != NULL; /* empty */) {
    GCObject *curr = *p;

    if (!(iswhite(curr) || all)) {
      p = curr->getNextPtr();  // Alive - keep in finobj
    } else {
      *p = curr->getNext();  // Remove from finobj
      curr->setNext(g->getToBeFnz());
      g->setToBeFnz(curr);  // Add to tobefnz
    }
  }
}
```

**Phase 3: Execution** (lgc.cpp:1095-1130)
```cpp
static void GCTM(lua_State *L) {
  global_State *g = G(L);
  GCObject *o = udata2finalize(g);  // Get from tobefnz

  g->setGCStp(oldgcstp | GCSTPGC);  // DISABLE GC during __gc

  setobj2s(L, L->getTop().p++, tm);    // Push finalizer
  setobj2s(L, L->getTop().p++, &v);    // Push object

  status = L->pCall(dothecall, NULL, L->saveStack(L->getTop().p - 2), 0);

  if (l_unlikely(status != LUA_OK)) {
    luaE_warnerror(L, "__gc");  // Error → warning (non-fatal)
  }

  g->setGCStp(oldgcstp);  // RE-ENABLE GC
}
```

### 6.2 Resurrection (lgc.cpp:1670-1683)

**Critical Code**:
```cpp
/* Atomic phase - after clearing weak tables */
separatetobefnz(g, 0);  // Move white finobj → tobefnz
markbeingfnz(g);        // Mark tobefnz objects (run finalizers)
propagateall(g);        // CRITICAL: Remark to catch resurrections!
convergeephemerons(g);  // Re-converge ephemerons
```

**Resurrection Scenario**:
```lua
local storage = {}

local obj = setmetatable({data = "important"}, {
  __gc = function(self)
    storage[#storage+1] = self  -- Resurrect!
  end
})

obj = nil  -- Make unreachable

-- GC cycle:
-- 1. obj marked white (unreachable)
-- 2. separatetobefnz: obj → tobefnz
-- 3. markbeingfnz: mark obj (for finalization)
-- 4. GCTM: Run __gc → obj stored in storage
-- 5. propagateall: Mark obj again (now reachable via storage)
-- 6. Sweep: obj is black → SURVIVES!
```

**Pitfall 6.2.1: Missing Remark Phase**

If `propagateall()` at line 1675 omitted:
```
1. obj resurrected by __gc (stored in storage)
2. storage not remarked
3. Sweep runs
4. obj is white → FREED despite being in storage!
5. Next access to storage[1] → use-after-free!
```

**Safeguard**: Explicit remark phase after finalization (lgc.cpp:1675).

**Pitfall 6.2.2: Re-finalization**

Once finalized, `FINALIZEDBIT` is cleared (lgc.cpp:1055):
```cpp
o->clearMarkedBit(FINALIZEDBIT);  // Mark as normal
```

If object becomes unreachable again:
```
Cycle 1: obj unreachable → finalized → resurrected
Cycle 2: obj unreachable again → finalized AGAIN!
```

This is **intentional** but can cause:
- Infinite finalization loops
- Resource exhaustion if __gc has side effects

---

## 7. GC PHASES AND ATOMIC OPERATIONS

### 7.1 State Machine (lstate.h:154-164)

```cpp
enum class GCState : lu_byte {
  Propagate    = 0,  // Incremental marking
  EnterAtomic  = 1,  // Prepare for atomic
  Atomic       = 2,  // Final marking (no interruption)
  SweepAllGC   = 3,  // Sweep main list
  SweepFinObj  = 4,  // Sweep finobj
  SweepToBeFnz = 5,  // Sweep tobefnz
  SweepEnd     = 6,  // Finalize sweep
  CallFin      = 7,  // Call finalizers
  Pause        = 8   // Idle
};
```

**State Transitions** (lgc.cpp:1723-1791):
```
Pause → Propagate → EnterAtomic → Atomic → SweepAllGC → SweepFinObj
    ↑                                                          ↓
    └────────── CallFin ← SweepEnd ← SweepToBeFnz ←──────────┘
```

### 7.2 Atomic Phase (lgc.cpp:1649-1687)

**Purpose**: Final marking without interruption (prevent concurrent mutations)

**Critical Operations**:
```cpp
static void atomic(lua_State *L) {
  global_State *g = G(L);

  // 1. Mark roots
  markobject(g, L);  // Main thread
  markvalue(g, g->getRegistry());  // Registry
  markmt(g);  // Metatables
  propagateall(g);  // Mark all reachable

  // 2. Remark upvalues (may have changed)
  remarkupvals(g);
  propagateall(g);

  // 3. Process weak tables
  convergeephemerons(g);
  clearbyvalues(g, g->getWeak(), NULL);
  clearbyvalues(g, g->getAllWeak(), NULL);

  // 4. Finalization (critical for resurrections!)
  separatetobefnz(g, 0);
  markbeingfnz(g);
  propagateall(g);  // ← Catches resurrected objects
  convergeephemerons(g);  // ← Re-converge after resurrection

  // 5. Final cleanup
  clearbykeys(g, g->getEphemeron());
  clearbykeys(g, g->getAllWeak());
  clearbyvalues(g, g->getWeak(), origweak);
  clearbyvalues(g, g->getAllWeak(), origall);

  // 6. Flip white color
  g->setCurrentWhite(cast_byte(otherwhite(g)));
}
```

**Pitfall 7.2.1: Interrupting Atomic Phase**

If atomic phase interrupted (e.g., by emergency GC):
```
1. Atomic starts, marks roots
2. Emergency GC triggered (memory allocation)
3. Atomic interrupted, state inconsistent
4. Sweep runs with incomplete marking
5. Reachable objects collected!
```

**Safeguard**: Atomic phase runs to completion (lgc.cpp:1726-1731). Emergency GC only during Pause state.

---

## 8. OPEN UPVALUES AND THREAD SAFETY

### 8.1 Open Upvalue Special Case (lgc.cpp:403-405)

```cpp
case LUA_VUPVAL: {
  UpVal *uv = gco2upv(o);
  if (uv->isOpen())
    set2gray(uv);  // Open upvalues stay GRAY
  else
    set2black(uv);  // Closed upvalues marked BLACK
  markvalue(g, uv->getVP());
}
```

**Why Gray**: Open upvalues point to stack slots, not heap objects. Stack is traversed separately.

**Pitfall 8.1.1: Thread Collection Before Upvalue Closure**

```cpp
// Thread T has open upvalue pointing to T->stack[10]
UpVal *uv = T->openupval;

// GC marks T as unreachable
// T collected → T->stack freed
// uv still holds pointer to T->stack[10] → DANGLING POINTER!
```

**Safeguard**: Threads with upvalues kept in `twups` list (lgc.cpp:731-745):
```cpp
static void remarkupvals(global_State *g) {
  lua_State *thread;
  for (thread = g->getTwUps(); thread != NULL; thread = thread->getTwUps()) {
    markobject(g, thread);  // Keep thread alive if has upvalues
  }
}
```

---

## 9. C++ STANDARD LIBRARY INTEGRATION RISKS

### 9.1 Why std::vector for GC Lists FAILS

**Reason 1: Pointer-to-Pointer Sweep**
```cpp
// Current: GCObject **sweepgc
*sweepgc = (*sweepgc)->getNext();  // In-place removal

// With std::vector:
std::vector<GCObject*> allgc;
size_t sweepgc_idx;
allgc.erase(allgc.begin() + sweepgc_idx);  // O(n) shift!
```

**Reason 2: Iterator Invalidation**
```cpp
auto it = allgc.begin() + sweepgc_idx;
allgc.erase(it);  // it now INVALID
++it;  // UNDEFINED BEHAVIOR
```

**Reason 3: Reallocation**
```cpp
allgc.push_back(new_obj);  // May reallocate
// All pointers into allgc now INVALID!
// sweepgc pointer now dangling!
```

### 9.2 Why std::shared_ptr for GC Objects FAILS

**Reason 1: Circular References**
```cpp
std::shared_ptr<Table> t1 = std::make_shared<Table>();
std::shared_ptr<Table> t2 = std::make_shared<Table>();

t1->set(key, t2);  // t2.use_count = 2
t2->set(key, t1);  // t1.use_count = 2

t1.reset();  // use_count = 1 (still referenced by t2)
t2.reset();  // use_count = 1 (still referenced by t1)

// MEMORY LEAK: Neither collected (refcount never reaches 0)
```

**Reason 2: Weak References Don't Match Weak Tables**
```cpp
// Lua weak table:
local t = setmetatable({}, {__mode="v"})
t[1] = some_object  // Weak reference - cleared if no other refs

// std::weak_ptr:
std::shared_ptr<Object> obj = std::make_shared<Object>();
std::weak_ptr<Object> weak = obj;  // Weak reference

obj.reset();  // Strong count = 0 → object DESTROYED immediately
// weak.lock() returns nullptr

// But Lua weak tables keep object alive until next GC!
```

**Reason 3: Generational GC Conflicts**
```cpp
// Lua:
Old object O (age=Old, skipped in minor GC)
O->field = New object N
// Barrier promotes N to Old0

// std::shared_ptr:
std::shared_ptr<Object> old_obj(age=Old);
old_obj->field = std::make_shared<Object>();  // No barrier concept!
// Shared_ptr doesn't understand generations
```

### 9.3 Safe C++ Standard Library Usage

**✅ SAFE: std::allocator Wrapper**
```cpp
template<typename T>
class lua_allocator {
  lua_State* L;
public:
  T* allocate(std::size_t n) {
    return static_cast<T*>(luaM_malloc_(L, n * sizeof(T), 0));
  }
  void deallocate(T* p, std::size_t n) {
    luaM_free_(L, p, n * sizeof(T));
  }
};

// Usage: std::vector<int, lua_allocator<int>> v(lua_alloc);
// Integrates with GC memory accounting
```

**✅ SAFE: std::sort for Table Sorting**
```cpp
// ltablib.cpp - table.sort() implementation
std::sort(arr.begin(), arr.end(),
  [L](const TValue& a, const TValue& b) -> bool {
    return sort_comp(L, a, b);
  });

// Why safe:
// - No GC object ownership
// - No list manipulation
// - Operates on array part only (not GC-tracked)
// - Temporary stack usage (freed after sort)
```

**✅ SAFE: std::span for Array Views**
```cpp
void process_array(std::span<TValue> values) {
  for (auto& v : values) {
    // Process value
  }
}

// Why safe:
// - Non-owning view (no ownership transfer)
// - No GC interaction
// - No allocation
// - Compile-time bounds checking
```

**✅ SAFE: std::variant for Expression Descriptors**
```cpp
// lparser.h - expdesc
class expdesc {
  expkind k;
  std::variant<lua_Integer, lua_Number, TString*> u;
  int t, f;
};

// Why safe:
// - Compile-time only (parser)
// - No runtime GC interaction
// - Type safety improvement
// - No performance impact
```

**❌ UNSAFE: std::unordered_map for String Interning**
```cpp
// WRONG!
std::unordered_map<std::string, TString*> string_cache;

// Problems:
// 1. String identity lost (pointer equality broken)
// 2. GC can't traverse std::unordered_map
// 3. Rehashing invalidates pointers
// 4. No integration with GC lists
```

**❌ UNSAFE: std::deque for CallInfo**
```cpp
// WRONG!
std::deque<CallInfo> call_stack;

// Problems:
// 1. CallInfo has previous/next pointers (intrusive list)
// 2. GC traverses via pointers, not deque iterators
// 3. Performance critical (VM hot path)
// 4. Deque reallocation breaks pointer stability
```

---

## 10. CRITICAL PITFALLS SUMMARY

| # | Pitfall | Location | Risk | Detection | Mitigation |
|---|---------|----------|------|-----------|------------|
| 1 | Double-free if white flips early | lgc.cpp:992-1007 | HIGH | Assertion failure, crashes | Cache `otherwhite()` before sweep |
| 2 | Missing write barrier | All pointer writes | CRITICAL | Use-after-free | Always use barrier macros |
| 3 | std::vector for GC lists | N/A (hypothetical) | CRITICAL | Crashes, corruption | Keep intrusive lists |
| 4 | Ephemeron convergence loop | lgc.cpp:809-828 | MEDIUM | Infinite loop, timeout | Object identity check |
| 5 | Missing resurrection remark | lgc.cpp:1675 | HIGH | Use-after-free | Explicit `propagateall()` |
| 6 | Interrupted atomic phase | lgc.cpp:1649-1687 | CRITICAL | Corruption | Run to completion |
| 7 | Old→New without barrier | Generational writes | HIGH | Premature collection | Forward barrier + Old0 |
| 8 | Open upval dangling ptr | lgc.cpp:403-405 | MEDIUM | Crash on closure | `twups` list |
| 9 | std::shared_ptr cycles | N/A (hypothetical) | HIGH | Memory leaks | Use tri-color GC |
| 10 | Iterator invalidation | std::list erase | HIGH | Undefined behavior | Pointer-to-pointer |

---

## 11. RECOMMENDATIONS FOR C++ MODERNIZATION

### 11.1 DO Modernize (Low Risk)

1. **Custom Allocator** (3.1a from stdlib analysis)
   - Wraps `luaM_malloc_`/`luaM_free_`
   - Enables std:: containers for non-GC data
   - Risk: LOW (doesn't affect GC lists)

2. **std::sort for table.sort()** (4.1a)
   - Replaces custom quicksort
   - Risk: LOW (no GC interaction)
   - Benefit: 150 lines reduction, safer

3. **std::variant for expdesc** (8.1a)
   - Compile-time only
   - Risk: LOW (parser internal)
   - Benefit: Type safety

4. **std::span for array parameters**
   - View-only, no ownership
   - Risk: LOW (no allocation)
   - Benefit: Bounds checking

### 11.2 DO NOT Modernize (High Risk)

1. **GC Object Lists** → std::vector/std::list
   - Risk: CRITICAL (breaks sweep algorithm)
   - Reason: Pointer-to-pointer architecture required

2. **GC Objects** → std::shared_ptr
   - Risk: CRITICAL (conflicts with tri-color)
   - Reason: Circular refs, no generations

3. **String Interning** → std::unordered_map
   - Risk: HIGH (loses identity semantics)
   - Reason: Pointer equality required

4. **CallInfo Stack** → std::deque
   - Risk: HIGH (performance critical)
   - Reason: Intrusive list, hot path

### 11.3 Research Required

1. **Boost.Intrusive for GC Lists**
   - Potential: Replace manual list manipulation
   - Risk: MEDIUM (must preserve pointer-to-pointer)
   - Action: Prototype and benchmark

2. **std::pmr Allocators**
   - Potential: Better memory pool integration
   - Risk: LOW (orthogonal to GC)
   - Action: Evaluate for non-GC containers

---

## 12. TESTING CHECKLIST FOR GC CHANGES

After ANY modification to GC code:

- [ ] **Functional Tests**
  - [ ] `cd testes && ../build/lua all.lua` passes
  - [ ] `gc.lua` passes (basic GC)
  - [ ] `gengc.lua` passes (generational GC)

- [ ] **Performance Tests**
  - [ ] 5-run benchmark ≤ 2.21s
  - [ ] No regression from 2.17s baseline

- [ ] **Stress Tests**
  - [ ] Large allocation pressure (10M+ objects)
  - [ ] Deep call stacks (1000+ levels)
  - [ ] Many weak tables (100+)
  - [ ] Circular references
  - [ ] Finalizer resurrection patterns

- [ ] **Memory Tests**
  - [ ] Valgrind memcheck (no leaks)
  - [ ] AddressSanitizer (no use-after-free)
  - [ ] UndefinedBehaviorSanitizer

- [ ] **Edge Cases**
  - [ ] Emergency GC during allocation
  - [ ] Errors during __gc execution
  - [ ] Upvalue closure during GC
  - [ ] Thread death with open upvalues

---

## 13. CONCLUSION

The Lua garbage collector is a **highly optimized, intricate system** with critical invariants:

1. **Tri-color marking** prevents collecting reachable objects during incremental GC
2. **Pointer-to-pointer architecture** enables efficient O(1) sweep
3. **Generational optimization** skips old objects in minor collections
4. **Write barriers** maintain invariants despite concurrent mutations
5. **Resurrection support** allows finalizers to preserve objects

**Key Finding**: The GC's fundamental architecture is **incompatible with std:: containers for object lists**. Attempts to replace intrusive linked lists with std::vector/std::list will:
- Break sweep algorithm (no pointer-to-pointer)
- Cause iterator invalidation bugs
- Destroy performance (O(n) operations)

**Safe Modernization Path**:
- ✅ Use std:: for **non-GC data** (with custom allocator)
- ✅ Use std:: for **algorithms** (sort, search)
- ✅ Use std:: for **compile-time safety** (variant, span)
- ❌ **DO NOT** replace GC lists with std:: containers
- ❌ **DO NOT** use std::shared_ptr for GC objects

**Recommendation**: Focus C++ modernization on **type safety** and **algorithm safety**, not GC architecture replacement. The current GC design is proven, efficient, and deeply integrated with Lua's semantics.

---

**Document Version**: 1.0
**Last Updated**: 2025-11-15
**Related Documents**:
- `CLAUDE.md` - Project status and guidelines
- `SRP_ANALYSIS.md` - Single Responsibility Principle refactoring
- `stdlib_opportunities.md` - C++ standard library replacement opportunities
