# Lua Memory Allocation Architecture - Comprehensive Documentation

## Overview

Lua's memory allocation system is built on a customizable allocator interface that allows applications to completely control how memory is allocated, deallocated, and tracked. The system is tightly integrated with garbage collection (GC) to manage memory automatically while respecting the custom allocator.

**Key Characteristics:**
- Single global allocator function pointer per Lua state
- Unified interface for allocation, reallocation, and deallocation
- Integrated GC debt tracking for memory accounting
- Emergency collection mechanism for allocation failures
- Zero required alignment constraints (allocator handles it)
- Support for custom user data pointer (`ud`)

---

## 1. Allocator Interface

### 1.1 Type Definition

```c
/* Type for memory-allocation functions */
typedef void * (*lua_Alloc) (void *ud, void *ptr, size_t osize, size_t nsize);
```

**Location:** `/home/user/lua_cpp/include/lua.h` (line 161)

**Signature Semantics:**
- **`ud`** (user data): Arbitrary pointer stored in global_State, passed to every allocator call
- **`ptr`** (pointer to block): The memory block being allocated/reallocated/freed
  - `NULL` when allocating new memory
  - Valid pointer when reallocating or freeing
- **`osize`** (old size): Previous size of the block
  - `0` when allocating new memory
  - Size in bytes when reallocating or freeing
  - Must satisfy: `(osize == 0) == (ptr == NULL)` (invariant checked)
- **`nsize`** (new size): Desired new size
  - `> 0` for allocation/reallocation (allocator must return valid pointer or NULL)
  - `0` for deallocation (must always succeed; freeing NULL is allowed)
  - Relationship: `nsize > 0` means allocate/reallocate, `nsize == 0` means free

**Return Value:**
- New memory block pointer if successful (must be non-NULL for nsize > 0)
- NULL on failure (only valid if nsize > 0)
- May return NULL for nsize == 0 (freeing never fails in Lua)

### 1.2 Storage in global_State

```cpp
/* 1. Memory Allocator - Memory allocation management */
class MemoryAllocator {
private:
  lua_Alloc frealloc;  /* function to reallocate memory */
  void *ud;            /* auxiliary data to 'frealloc' */

public:
  inline lua_Alloc getFrealloc() const noexcept { return frealloc; }
  inline void setFrealloc(lua_Alloc f) noexcept { frealloc = f; }
  inline void* getUd() const noexcept { return ud; }
  inline void setUd(void* u) noexcept { ud = u; }
};
```

**Location:** `/home/user/lua_cpp/src/core/lstate.h` (lines 647-657)

---

## 2. Public API Functions

### 2.1 State Creation with Custom Allocator

```c
LUA_API lua_State *(lua_newstate) (lua_Alloc f, void *ud, unsigned seed);
```

**Location:** `/home/user/lua_cpp/include/lua.h` (line 199)
**Implementation:** `/home/user/lua_cpp/src/core/lstate.cpp` (lines 346-399)

**Process:**
1. Allocates `global_State` structure using provided allocator `f` with tag `LUA_TTHREAD`
2. Initializes main thread embedded in global_State
3. Stores allocator function and user data
4. Initializes GC parameters and accounting
5. Calls `f_luaopen` protected to complete initialization
6. Returns NULL if initialization fails

**Example:**
```c
// Default allocator from C standard library
void *my_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
  if (nsize == 0) {
    free(ptr);
    return NULL;
  }
  return realloc(ptr, nsize);
}

lua_State *L = lua_newstate(my_alloc, NULL, 0);
```

### 2.2 Get Current Allocator

```c
LUA_API lua_Alloc (lua_getallocf) (lua_State *L, void **ud);
```

**Location:** `/home/user/lua_cpp/include/lua.h` (line 414)
**Implementation:** `/home/user/lua_cpp/src/core/lapi.cpp` (lines 1319-1326)

**Behavior:**
- Returns currently installed allocator function
- Optionally returns user data pointer via `**ud` parameter
- Thread-safe (uses lock)

### 2.3 Change Allocator (Advanced Usage)

```c
LUA_API void (lua_setallocf) (lua_State *L, lua_Alloc f, void *ud);
```

**Location:** `/home/user/lua_cpp/include/lua.h` (line 415)
**Implementation:** `/home/user/lua_cpp/src/core/lapi.cpp` (lines 1329-1334)

**Behavior:**
- Changes allocator and user data for a running state
- Thread-safe (uses lock)
- Allows switching allocators during runtime (advanced use case)

**Warning:** Changing allocators while memory is outstanding can cause problems if the new allocator uses different memory pools than the old one.

---

## 3. Internal Allocation Functions

### 3.1 Core Allocation Macros and Functions

All internal Lua code uses macros that ultimately call these functions:

```c
/* Macros (defined in lmem.h) */
#define luaM_malloc_(L, size, tag)              /* allocate size bytes */
#define luaM_realloc_(L, block, osize, nsize)   /* reallocate block */
#define luaM_saferealloc_(L, block, osize, nsize) /* realloc or throw error */
#define luaM_free_(L, block, osize)             /* free block of size osize */

/* Higher-level convenience macros */
#define luaM_new(L, t)              /* allocate single object of type t */
#define luaM_newvector(L, n, t)     /* allocate array of n objects */
#define luaM_newobject(L, tag, s)   /* allocate with GC tag */
#define luaM_free(L, b)             /* free single object */
#define luaM_freearray(L, b, n)     /* free array of n objects */
```

**Location:** `/home/user/lua_cpp/src/memory/lmem.h` (lines 52-93)

### 3.2 Internal Function Implementations

```cpp
/* Generic reallocation - core function */
void *luaM_realloc_ (lua_State *L, void *block, size_t oldsize, size_t newsize);

/* Safe reallocation - throws error on failure */
void *luaM_saferealloc_ (lua_State *L, void *block, size_t oldsize, size_t newsize);

/* Memory allocation (new blocks only) */
void *luaM_malloc_ (lua_State *L, size_t size, int tag);

/* Memory deallocation */
void luaM_free_ (lua_State *L, void *block, size_t osize);

/* Array growth helper - doubles size exponentially */
void *luaM_growaux_ (lua_State *L, void *block, int nelems, int *size, 
                     unsigned size_elem, int limit, const char *what);

/* Array shrinking helper - used in parser cleanup */
void *luaM_shrinkvector_ (lua_State *L, void *block, int *size, 
                          int final_n, unsigned size_elem);
```

**Location:** `/home/user/lua_cpp/src/memory/lmem.h` (lines 82-93)
**Implementation:** `/home/user/lua_cpp/src/memory/lmem.cpp` (lines 97-215)

---

## 4. Allocation Flow and GC Integration

### 4.1 Fundamental Principle: GC Debt Tracking

Lua uses a **GC debt** mechanism to track memory that's been allocated but not yet accounted for by the GC. This allows the GC to:
- Trigger automatically when debt exceeds thresholds
- Run emergency collections when allocations fail
- Maintain a predictable pause schedule

**Key Data Structure:**
```cpp
class GCAccounting {
private:
  l_mem totalbytes;    /* Total allocated bytes + debt */
  l_mem debt;          /* Bytes counted but not yet allocated */
  l_mem marked;        /* Objects marked in current GC cycle */
  l_mem majorminor;    /* Counter to control major-minor shifts */
  // ...
};
```

**Location:** `/home/user/lua_cpp/src/core/lstate.h` (lines 661-686)

### 4.2 Allocation Flow: `luaM_realloc_`

```
luaM_realloc_(L, block, osize, nsize)
  │
  ├─ Get global_State from L
  │
  ├─ Try initial allocation via allocator
  │  Call: callfrealloc(g, block, osize, nsize)
  │  │     = (*g->getFrealloc())(g->getUd(), block, osize, nsize)
  │
  ├─ If allocation failed AND nsize > 0:
  │  │
  │  └─ Try emergency collection + retry:
  │     ├─ Check: canTryAgain(g)  
  │     │  = g->isComplete() && !g->getGCStopEm()
  │     │
  │     ├─ If yes: luaC_fullgc(L, 1)  /* run full GC */
  │     ├─ Then retry: callfrealloc(g, block, osize, nsize)
  │     │
  │     └─ If still fails: return NULL (caller must handle)
  │
  ├─ Update GC debt:
  │  debt -= (nsize - osize)  /* negative debt = credit */
  │
  └─ Return new block pointer
```

**File:** `/home/user/lua_cpp/src/memory/lmem.cpp` (lines 176-189)

### 4.3 Safe Allocation Flow: `luaM_saferealloc_`

```
luaM_saferealloc_(L, block, osize, nsize)
  │
  ├─ Call luaM_realloc_(...)
  │
  ├─ If newblock == NULL and nsize > 0:
  │  └─ Throw memory error: luaM_error(L)
  │     └─ L->doThrow(LUA_ERRMEM)
  │
  └─ Return newblock or throw
```

**File:** `/home/user/lua_cpp/src/memory/lmem.cpp` (lines 192-198)

### 4.4 Allocation Flow: `luaM_malloc_`

```
luaM_malloc_(L, size, tag)
  │
  ├─ If size == 0:
  │  └─ Return NULL (no allocation needed)
  │
  ├─ Try initial allocation via allocator
  │  Call: firsttry(g, NULL, tag, size)
  │  │     (tag is passed as osize for tracking purposes)
  │
  ├─ If allocation failed:
  │  │
  │  └─ Try emergency collection + retry
  │
  ├─ If still failed: throw error (luaM_error)
  │
  ├─ Update GC debt:
  │  debt -= size
  │
  └─ Return new block
```

**File:** `/home/user/lua_cpp/src/memory/lmem.cpp` (lines 201-215)

### 4.5 Deallocation Flow: `luaM_free_`

```
luaM_free_(L, block, osize)
  │
  ├─ Check invariant: (osize == 0) == (block == NULL)
  │
  ├─ Call allocator to free: 
  │  callfrealloc(g, block, osize, 0)
  │
  ├─ Update GC debt:
  │  debt += osize  /* increase debt (now has credit) */
  │
  └─ Return (deallocation always succeeds)
```

**File:** `/home/user/lua_cpp/src/memory/lmem.cpp` (lines 150-155)

---

## 5. GC Debt and Automatic Collection

### 5.1 Debt Semantics

- **Positive debt**: Memory allocated but not yet counted by GC (most common)
- **Negative debt**: Credits available for allocation without triggering GC

**Equation:**
```
actual_allocated = totalbytes - debt
```

When `debt` becomes large (positive), GC runs and reduces it.

### 5.2 Emergency Collection Conditions

Collections are triggered only if:
```cpp
cantryagain(g) = g->isComplete() && !g->getGCStopEm()
```

Where:
- **`g->isComplete()`**: Global state fully initialized (not in initialization phase)
- **`!g->getGCStopEm()`**: Not already in a GC collection step (prevents recursive GC)

**File:** `/home/user/lua_cpp/src/memory/lmem.cpp` (line 58)

### 5.3 GC Parameters

Stored in `GCParameters` subsystem:

```cpp
class GCParameters {
private:
  lu_byte params[LUA_GCPN];  /* GC tuning parameters */
  lu_byte currentwhite;      /* Current white color for GC */
  lu_byte state;             /* State of garbage collector */
  lu_byte kind;              /* Kind of GC running (incremental/generational) */
  lu_byte stopem;            /* Stops emergency collections */
  lu_byte stp;               /* Control whether GC is running */
  lu_byte emergency;         /* True if this is emergency collection */
};
```

**Location:** `/home/user/lua_cpp/src/core/lstate.h` (lines 689-724)

### 5.4 GC Configuration Constants

**Location:** `/home/user/lua_cpp/src/memory/lgc.h`

```cpp
/* Incremental Collector */

/* Number of bytes must be LUAI_GCPAUSE% before starting new cycle */
inline constexpr int LUAI_GCPAUSE = 250;
/* Meaning: GC runs when allocated bytes = (previous_collected * 250 / 100) */

/* Step multiplier: The collector handles LUAI_GCMUL% work units for
   each new allocated word. (Each "work unit" ≈ sweeping 1 object) */
inline constexpr int LUAI_GCMUL = 200;
/* Meaning: For each allocated unit, GC does 200% units of work */

/* How many bytes to allocate before next GC step */
inline constexpr size_t LUAI_GCSTEPSIZE = (200 * sizeof(Table));
/* Approximately 3200-6400 bytes depending on architecture */

/* Generational Collector */

/* Minor collections will shift to major ones after LUAI_MINORMAJOR%
   bytes become old. */
inline constexpr int LUAI_MINORMAJOR = 70;

/* Major collections will shift to minor ones after collecting at least
   LUAI_MAJORMINOR% of the new bytes. */
inline constexpr int LUAI_MAJORMINOR = 50;

/* A young (minor) collection will run after creating LUAI_GENMINORMUL%
   new bytes. */
inline constexpr int LUAI_GENMINORMUL = 20;
```

---

## 6. Allocation Sites and Object Types

### 6.1 Core Objects Allocated

| Object Type | Macros Used | Subsystem | Count |
|------------|------------|-----------|-------|
| **TString** | `luaM_new`, `luaM_newobject` | lstring.cpp | Dynamic |
| **Table** | `luaM_new`, `luaM_newvector` | ltable.cpp | Dynamic |
| **Proto** | `luaM_new`, `luaM_newvector` | lfunc.cpp | Dynamic |
| **UpVal** | `luaM_new` | lfunc.cpp | Dynamic |
| **Closure** | `luaM_new`, `luaM_newvector` | lfunc.cpp | Dynamic |
| **Udata** | `luaM_newobject` | lstring.cpp | Dynamic |
| **lua_State** | `luaM_new` | lstate.cpp | One per thread |
| **global_State** | Custom via allocator | lstate.cpp | One per VM |

### 6.2 Collection Tracking

All GC-managed objects inherit from `GCObject` and are tracked in:

```cpp
class GCObjectLists {
private:
  GCObject *allgc;      /* All collectable objects */
  GCObject *finobj;     /* Objects with finalizers */
  GCObject *gray;       /* Gray objects (mark phase) */
  GCObject *grayagain;  /* Objects to revisit */
  GCObject *weak;       /* Weak-value tables */
  GCObject *ephemeron;  /* Ephemeron tables */
  GCObject *allweak;    /* All-weak tables */
  GCObject *tobefnz;    /* To be finalized */
  GCObject *fixedgc;    /* Never collected (strings, etc.) */
  // ... more lists for generational GC
};
```

**Location:** `/home/user/lua_cpp/src/core/lstate.h` (lines 727-800)

### 6.3 Usage Examples

**String Allocation:**
```cpp
/* In lstring.cpp */
tb->setHash(luaM_newvector(L, MINSTRTABSIZE, TString*));
```

**Table Allocation:**
```cpp
/* In ltable.cpp */
t->setNodeArray(luaM_newvector(L, size, Node));
char *node = luaM_newblock(L, bsize);
```

**Function Allocation:**
```cpp
/* In lfunc.cpp */
Proto *p = luaM_new(L, Proto);
```

---

## 7. Integration Points for Custom Allocators

### 7.1 What a Custom Allocator Must Do

When providing a custom `lua_Alloc` function, it must:

1. **Handle all three operations** (allocation, reallocation, deallocation):
   ```c
   void *my_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
     if (nsize == 0) {
       // Deallocation: free(ptr), return NULL
     } else if (ptr == NULL) {
       // Allocation: malloc(nsize), return pointer or NULL
     } else {
       // Reallocation: realloc(ptr, nsize), return pointer or NULL
     }
   }
   ```

2. **Maintain size tracking**: Lua passes both old and new sizes; allocator can use `osize` to verify consistency

3. **Return NULL on failure**: Only valid for `nsize > 0` (allocation/reallocation)

4. **Support NULL pointers**: Must handle `free(NULL)` correctly (returning NULL is fine)

5. **Preserve invariants**:
   - `(osize == 0) == (ptr == NULL)` must hold
   - Must handle freeing (nsize == 0) successfully
   - Must return non-NULL for successful allocations

### 7.2 Customization Points

**Via `ud` (user data parameter):**
- Store pointer to allocator state/pool
- Pass context to memory management system
- Track statistics per allocation

**Example:**
```cpp
struct MyAllocState {
  void* memory_pool;
  size_t total_allocated;
  std::map<void*, size_t> allocations;
};

void *my_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
  MyAllocState *state = (MyAllocState *)ud;
  
  if (nsize == 0) {
    if (ptr) {
      state->allocations.erase(ptr);
      free(ptr);
    }
    return NULL;
  }
  
  void *newptr = realloc(ptr, nsize);
  if (newptr && ptr != newptr) {
    if (ptr) state->allocations.erase(ptr);
    state->allocations[newptr] = nsize;
  }
  return newptr;
}
```

### 7.3 Common Use Cases

1. **Memory limits**: Reject allocations if `state->total_allocated + nsize > limit`
2. **Statistics**: Track peak allocation, fragmentation
3. **Custom pools**: Use arena allocators, memory pools
4. **Debugging**: Log all allocations/deallocations
5. **Cleanup**: Defer actual freeing to batch later

---

## 8. Error Handling

### 8.1 Memory Errors

When allocation fails (allocator returns NULL):

1. **Attempt emergency GC** (if possible)
2. **Retry allocation** with freed memory
3. **Throw exception** if still fails

```cpp
/* Define in llimits.h */
#define luaM_error(L)  (L)->doThrow(LUA_ERRMEM)
```

**File:** `/home/user/lua_cpp/src/memory/lmem.h` (line 17)

### 8.2 Recovery Mechanism

```c
/* Try allocate - fails silently */
void *block = luaM_realloc_(L, NULL, 0, size);
if (block == NULL) {
  // Caller handles NULL
}

/* Try allocate - throws on failure */
void *block = luaM_saferealloc_(L, NULL, 0, size);
// Never returns NULL for size > 0
```

### 8.3 Overflow Protection

Lua checks for multiplication overflow before allocating arrays:

```cpp
/* Test whether n*e might overflow */
#define luaM_testsize(n,e)  \
  (sizeof(n) >= sizeof(size_t) && cast_sizet((n)) + 1 > MAX_SIZET/(e))

/* Check and error if might overflow */
#define luaM_checksize(L,n,e)  \
  (luaM_testsize(n,e) ? luaM_toobig(L) : cast_void(0))
```

**File:** `/home/user/lua_cpp/src/memory/lmem.h` (lines 31-35)

---

## 9. Memory Accounting and GC Interaction

### 9.1 How GC "Knows" About Allocations

Lua never directly asks the allocator "how much do I have allocated?" Instead:

1. **Track allocations**: Each `luaM_malloc_`, `luaM_realloc_`, `luaM_free_` updates `debt`
2. **Periodically run GC**: When debt exceeds threshold, run a GC cycle
3. **GC marks objects**: Marks all reachable objects
4. **GC sweeps**: Frees unmarked objects and updates `totalbytes`

### 9.2 Example: Allocation → GC Trigger → Collection

```
1. Application allocates 5MB
   └─ luaM_malloc_ called
   └─ debt increases by 5MB
   └─ gcdebt = 5MB

2. GC checks threshold
   └─ if (gcdebt > threshold) → run GC

3. GC cycle runs
   └─ Mark all reachable objects
   └─ Sweep unmarked objects
   └─ Update totalbytes (actual allocated)
   └─ Update debt (new pending)

4. If memory freed
   └─ debt decreases (or becomes negative)
   └─ Application can proceed
```

### 9.3 GC State Machine

```
Pause → Propagate → Atomic → SweepAllGC → ... → SweepEnd → Pause
```

Debt tracking happens at each state transition.

---

## 10. Special Allocator Features

### 10.1 External String Allocators

Strings can use separate allocators:

```c
const char *(lua_pushexternalstring)(lua_State *L, 
    const char *s, size_t len, lua_Alloc falloc, void *ud);
```

**Location:** `/home/user/lua_cpp/include/lua.h` (line 284)

This allows strings to be allocated from different memory pools than the rest of Lua.

### 10.2 GC Tags for Allocation Context

Some allocation macros include a "tag" parameter:

```cpp
#define luaM_newobject(L, tag, s)  luaM_malloc_(L, (s), tag)
```

The tag is passed to the allocator's `osize` parameter to indicate object type.

---

## 11. Key Constraints and Requirements

### 11.1 Allocator Contract Requirements

| Requirement | Constraint | Reason |
|------------|-----------|--------|
| **Freeing size tracking** | `osize` must match allocation size | GC debt accounting |
| **NULL handling** | `free(NULL)` must be safe | Standard C semantics |
| **Failure semantics** | Only fail for `nsize > 0` | GC debt is never lost |
| **Consistency** | Allocator must be reentrant | GC can trigger during allocation |
| **Invariants** | `(osize == 0) == (ptr == NULL)` | Lua asserts this |

### 11.2 No Alignment Requirements

Lua doesn't require specific alignment. The allocator can return any properly allocated pointer.

### 11.3 No Size Queries

Lua never calls allocator to query allocated size. All size tracking is internal.

---

## 12. Memory Limits and Configuration

### 12.1 Size Limits

```cpp
#define MAX_SIZE  /* Minimum of MAX_SIZET and LUA_MAXINTEGER */
#define MAX_LMEM  /* Maximum l_mem value */
```

**Location:** `/home/user/lua_cpp/src/memory/llimits.h` (lines 38-61)

### 12.2 Buffer Size

```cpp
#define LUAL_BUFFERSIZE \
  ((int)(16 * sizeof(void*) * sizeof(lua_Number)))
```

**Location:** `/home/user/lua_cpp/include/luaconf.h` (line 720)

Default auxiliary library buffer size, configurable.

### 12.3 Maximum Alignment

```cpp
#define LUAI_MAXALIGN \
  lua_Number n; double u; void *s; lua_Integer i; long l
```

**Location:** `/home/user/lua_cpp/include/luaconf.h` (line 727)

Used in `TValue` union to ensure proper alignment.

---

## 13. Allocation Sites Distribution

**Files using allocation macros (58 calls across):**

- **Memory/Core**: lmem.cpp
- **Objects**: ltable.cpp, lstring.cpp, lfunc.cpp, lobject.cpp
- **Core VM**: lstate.cpp, ldo.cpp, ldebug.cpp
- **Compiler**: lparser.cpp, lcode.cpp
- **Serialization**: lundump.cpp

**Most allocations by type:**
1. Tables and hash nodes
2. Strings (table, interning)
3. Functions and closures
4. Arrays in parser

---

## 14. Summary and Integration Checklist

### For Implementing Custom Allocator:

- [ ] Implement `lua_Alloc` function with proper signature
- [ ] Handle all three operations: allocation, reallocation, deallocation
- [ ] Support `ud` parameter for custom context
- [ ] Never fail on deallocation (nsize == 0)
- [ ] Can return NULL only for allocations (nsize > 0)
- [ ] Pass to `lua_newstate()` or `lua_setallocf()`
- [ ] Track statistics if needed via `ud`
- [ ] Support emergency GC scenario (multiple failed attempts)

### For Understanding Memory Flow:

- [ ] GC debt is the key accounting mechanism
- [ ] Allocations add to debt
- [ ] GC runs when debt exceeds threshold
- [ ] Emergency collection happens on allocation failure
- [ ] No separate allocator queries needed
- [ ] Lua handles all size tracking internally

### Critical Files:

| File | Purpose |
|------|---------|
| `lmem.h` | Public allocation interface |
| `lmem.cpp` | Allocation implementation with GC debt |
| `lstate.h` | MemoryAllocator and GCAccounting subsystems |
| `lstate.cpp` | Allocator initialization in `lua_newstate` |
| `lapi.cpp` | Public API for changing allocators |
| `lgc.h` | GC parameters and debt triggering |

