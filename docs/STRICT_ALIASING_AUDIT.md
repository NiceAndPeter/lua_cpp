# Lua C++ Strict Aliasing and Type Punning Audit Report

**Date**: 2025-11-21  
**Codebase**: /home/user/lua_cpp  
**Scope**: Very thorough examination of type punning and strict aliasing violations  
**Files Analyzed**: 84 source files (headers + implementations)

---

## EXECUTIVE SUMMARY

This codebase has **multiple categories of potential strict aliasing violations** stemming from C's original design. While the Lua C implementation had these issues by design, the C++ conversion requires careful analysis because:

1. **Union type punning with different active members** - The Value union
2. **Pointer arithmetic through char* followed by reinterpret_cast**
3. **Memory layout assumptions** for variable-size objects
4. **Overlay patterns** (TString contents, Table array hint)
5. **Template-based type conversions** in GCObject conversions

Most violations are **technically allowed by C++17 standard** due to careful allocation and initialization patterns, but they rely on implementation details and are fragile to compiler optimizations.

---

## SEVERITY CLASSIFICATION

### Critical (MUST FIX)
- Issues that definitely violate C++ standard or cause undefined behavior

### High (SHOULD FIX)
- Likely to cause compiler optimization issues or undefined behavior under different conditions

### Medium (SHOULD REVIEW)
- Questionable patterns that work but rely on implementation details

### Low (INFORMATIONAL)
- Patterns that are generally safe but deserve documentation

---

## FINDINGS

### 1. TValue Union Type Punning (MEDIUM SEVERITY)

**Location**: `src/objects/ltvalue.h:41-49`, `src/objects/lobject.h:1378-1443`

**Pattern**:
```cpp
typedef union Value {
  GCObject *gc;      /* collectable objects */
  void *p;           /* light userdata */
  lua_CFunction f;   /* light C functions */
  lua_Integer i;     /* integer numbers */
  lua_Number n;      /* float numbers */
  lu_byte ub;        /* guard for uninitialized */
} Value;

// TValue stores one Value + one lu_byte type tag
class TValue {
  Value value_;
  lu_byte tt_;
};

// Field assignments like:
inline void TValue::setInt(lua_Integer i) noexcept {
  value_.i = i;
  tt_ = LUA_VNUMINT;
}

inline void TValue::setFloat(lua_Number n) noexcept {
  value_.n = n;
  tt_ = LUA_VNUMFLT;
}
```

**Strict Aliasing Issue**:
- Accessing `value_.i` and `value_.n` as different union members violates strict aliasing if the compiler assumes they don't alias
- **However**: This is safe per C++17 §8.3 [class.mem] because union members can be read/written as long as only one is active
- The `tt_` field indicates which union member is active, acting as a discriminator

**Safeguard**:
- Type tag (`tt_`) acts as discriminator
- Code always checks tag before accessing specific union member
- Macro/method guards like `ttisinteger(o)` check tag before reading

**Risk Assessment**: 
- **ACCEPTABLE** with current compiler flags (-O3)
- **RISKY** with aggressive whole-program optimization (LTO)
- Could break if compiler doesn't respect union semantics

**Recommendation**: 
- Add runtime assertions in debug builds to verify tag matches accessed field
- Consider using `std::variant` in C++17 (would provide better type safety)
- Document the invariant: "Never access union field unless tag matches"

---

### 2. reinterpret_cast GCObject Conversions (MEDIUM SEVERITY)

**Location**: Multiple files

**Pattern 1 - GCBase<Derived> conversions**:
```cpp
// src/objects/lobject.h:320-326
template<typename Derived>
GCObject* toGCObject() noexcept {
    return reinterpret_cast<GCObject*>(static_cast<Derived*>(this));
}
```

**Analysis**:
- `static_cast<Derived*>(this)` is safe (just cast within inheritance hierarchy)
- `reinterpret_cast<GCObject*>(...)` then converts to base class
- **Safe because**: Derived inherits from GCBase<Derived> which inherits from GCObject
- Memory layout is identical to GCObject at start (no offset needed)
- Static assert in each derived class verifies memory layout:
  ```cpp
  static_assert(sizeof(GCObject) == offsetof(Table, flags));
  ```

**Pattern 2 - Generic GC type conversions**:
```cpp
// src/memory/lgc.h:167-173
template<typename T>
inline bool iswhite(const T* x) noexcept {
  return reinterpret_cast<const GCObject*>(x)->isWhite();
}
```

**Analysis**:
- Assumes `T*` points to object with GCObject at start
- No type checking - relies on caller ensuring correct type
- **Risk**: If passed wrong type, could read garbage as GC fields

**Pattern 3 - Char pointer arithmetic + cast**:
```cpp
// src/memory/lgc.cpp:224-225
char *p = cast_charp(luaM_newobject(L, novariant(tt), sz));
GCObject *o = reinterpret_cast<GCObject*>(p + offset);
```

**Analysis**:
- Allocates raw bytes with `luaM_newobject`
- Adds offset to get to actual object start
- Casts to GCObject*
- **Safe because**: Used in controlled allocation path
- **Relies on**: Caller providing correct offset

**Risk Assessment**: **MEDIUM**
- Static layout guarantees make these safe in practice
- But no runtime type checking
- Compiler might reorder checks if not careful with const semantics

**Recommendation**:
- Keep as-is for performance
- Add comprehensive tests for GC type conversions
- Document which types have compatible memory layouts
- Consider static_assert for each GC type: `static_assert(alignof(Type) == alignof(GCObject))`

---

### 3. Stack Pointer Arithmetic and restore() (HIGH SEVERITY)

**Location**: `src/core/lstack.h:118-125`

**Pattern**:
```cpp
/* Convert stack pointer to offset from base */
inline ptrdiff_t save(StkId pt) const noexcept {
    return cast_charp(pt) - cast_charp(stack.p);
}

/* Convert offset to stack pointer */
inline StkId restore(ptrdiff_t n) const noexcept {
    return reinterpret_cast<StkId>(cast_charp(stack.p) + n);
}
```

**Where StkId is**:
```cpp
// src/objects/lobject.h:63
typedef StackValue *StkId;

// src/objects/lobject.h:52-59
typedef union StackValue {
  TValue val;
  struct {
    Value value_;
    lu_byte tt_;
    unsigned short delta;
  } tbclist;
} StackValue;
```

**Strict Aliasing Issue**:
1. **save()**: 
   - Casts `StackValue*` to `char*` 
   - This is allowed (any pointer can be cast to char*)
   - Subtraction of char pointers is well-defined

2. **restore()**:
   - Does: `cast_charp(stack.p) + n` → pointer arithmetic on char*
   - Then: `reinterpret_cast<StkId>(...)` → casts back to StackValue*
   - **This is a round-trip conversion**

**Analysis of Round-trip Conversion**:
```
StackValue*  →  char*  →  (char* + offset)  →  StackValue*
```

- **Technically allowed** by C++17 when the result pointer points to the same object
- **However**: Compiler might not realize this
- **Optimization risk**: If compiler doesn't track this conversion, it might assume the StackValue* from restore() has no alias relationship with original

**Problem Case**:
```cpp
StkId original = stack.p + 5;
ptrdiff_t offset = save(original);
// ... realloc happens ...
StkId restored = restore(offset);

// If compiler doesn't realize restored == new_stack.p + 5,
// it might use cached assumptions about *original
```

**Risk Assessment**: **HIGH**
- Works fine on current compiler (no aggressive whole-program optimization)
- Could break with LTO enabled
- Depends on compiler recognizing pointer round-trip conversion

**Recommendation** (IMPORTANT):
- **Replace with direct offset storage**: Don't convert to char* at all
  ```cpp
  // Better approach:
  inline ptrdiff_t save(StkId pt) const noexcept {
      return pt - stack.p;  // Direct pointer arithmetic
  }
  
  inline StkId restore(ptrdiff_t n) const noexcept {
      return stack.p + n;
  }
  ```
- **Why this is better**: 
  - No char* intermediate → compiler understands aliasing better
  - Same performance (pointer arithmetic is cheap)
  - Clearer intent
  - Safer with LTO

---

### 4. NodeArray Memory Layout Manipulation (HIGH SEVERITY)

**Location**: `src/objects/ltable.cpp:81-136`

**Pattern**:
```cpp
typedef union {
  Node *lastfree;
  char padding[offsetof(Limbox_aux, follows_pNode)];
} Limbox;

class NodeArray {
public:
  static Node* allocate(lua_State* L, unsigned int n, bool withLastfree) {
    if (withLastfree) {
      size_t total = sizeof(Limbox) + n * sizeof(Node);
      char* block = luaM_newblock(L, total);
      Limbox* limbox = reinterpret_cast<Limbox*>(block);
      Node* nodeStart = reinterpret_cast<Node*>(block + sizeof(Limbox));
      limbox->lastfree = nodeStart + n;
      return nodeStart;
    }
  }

  static Node*& getLastFree(Node* nodeStart) {
    Limbox* limbox = reinterpret_cast<Limbox*>(nodeStart) - 1;
    return limbox->lastfree;
  }
};
```

**Strict Aliasing Issues**:

1. **Allocation phase**:
   - Allocates `char* block` of size `sizeof(Limbox) + n * sizeof(Node)`
   - `reinterpret_cast<Limbox*>(block)` - cast char* to Limbox*
   - `reinterpret_cast<Node*>(block + sizeof(Limbox))` - cast char* to Node*
   - **Is this safe?** 
     - C++17 §8.2.10: "Reinterpret_cast from one pointer type to another is valid if the memory contains an object of that type"
     - **But here**: memory contains `Limbox` followed by `Node[]`
     - When we cast the start to `Limbox*`, we're OK
     - When we cast `block + sizeof(Limbox)` to `Node*`, we're accessing the Node[] part
     - **SAFE**: Provided pointer arithmetic and casting are done on the correct underlying objects

2. **Access phase (getLastFree)**:
   - Takes `Node* nodeStart` 
   - Casts to `Limbox*` and subtracts 1
   - `reinterpret_cast<Limbox*>(nodeStart) - 1`
   - **This is pointer arithmetic on Limbox array**: Treats memory as if it's a Limbox array
   - **Is this safe?**
     - The -1 points back to the Limbox header before the nodes
     - Conceptually valid: `[Limbox][Node...] ← nodeStart`
     - **However**: Pointer arithmetic assumes same type, but we're casting a `Node*` to `Limbox*` then doing arithmetic
     - This violates the assumption that a Node* points to nodes, not Limbox

**Problem**:
```cpp
// The nodeStart pointer semantically points to a Node
// But we're treating its predecessor as a Limbox
Limbox* limbox = reinterpret_cast<Limbox*>(nodeStart) - 1;
```

- Compiler might assume `nodeStart` never aliases a Limbox
- If compiler does bounds analysis, it might think going back 1 element is UB

**Risk Assessment**: **HIGH**
- Comments acknowledge this is "pointer arithmetic on Limbox array for arithmetic purposes"
- Implementation is clever but fragile
- Could break with stricter aliasing analysis

**Code Comments Acknowledge This**:
```cpp
// Safe per C++17 §8.7: pointer arithmetic within allocated block
// nodeStart points to element after Limbox, so (nodeStart - 1) conceptually
// points to the Limbox (treating the block as Limbox array for arithmetic purposes)
```

**Recommendation**:
- **Better approach**: Store Limbox pointer explicitly
  ```cpp
  struct NodeHeader {
    Limbox limbox;
    Node nodes[1];  // Flexible array member in C++
  };
  
  static Node* allocate(...) {
    if (withLastfree) {
      NodeHeader* header = new (luaM_newblock(...)) NodeHeader;
      header->limbox.lastfree = header->nodes + n;
      return header->nodes;
    }
  }
  
  static Node*& getLastFree(Node* nodeStart) {
    // nodeStart points to nodes[0], so -1 of the containing NodeHeader
    NodeHeader* header = containing_record(nodeStart, NodeHeader, nodes);
    return header->limbox.lastfree;
  }
  ```

- **Why this is better**:
  - Explicit pointer relationship
  - No pointer arithmetic tricks
  - Clear memory layout
  - Compiler understands the structure

---

### 5. TString Short String Contents Overlay (MEDIUM SEVERITY)

**Location**: `src/objects/lobject.h:445-456, 496-497`, `src/objects/lstring.cpp:229`

**Pattern**:
```cpp
class TString : public GCBase<TString> {
private:
  lu_byte extra;
  ls_byte shrlen;
  unsigned int hash;
  union {
    size_t lnglen;
    TString *hnext;
  } u;
  char *contents;           /* <- For LONG strings only */
  lua_Alloc falloc;         /* <- For EXTERNAL strings only */
  void *ud;                 /* <- For EXTERNAL strings only */

public:
  TString() noexcept {
    // NOTE: contents, falloc, ud are NOT initialized!
    // For short strings, they're overlay for string data
  }

  // For short strings, the string data starts AFTER the u union
  char* getContentsAddr() noexcept {
    return cast_charp(this) + contentsOffset();
  }
};
```

**Usage**:
```cpp
// src/lstring.cpp:229
ts->setContents(cast_charp(ts) + tstringFallocOffset());
```

**How It Works**:
- **Short strings**: Allocated with size = `contentsOffset() + strlen + 1`
  - The `contents`, `falloc`, `ud` fields don't exist in memory
  - Instead, string data is laid out after the TString fields
  
- **Long strings**: Full allocation
  - `contents` field points to external string data
  - `falloc`/`ud` for custom deallocation

**Memory Layout**:
```
Short String:
  [GCObject.next][GCObject.tt][GCObject.marked]
  [extra][shrlen][hash][u union]
  [string data starts here] ← getContentsAddr() points here

Long String:
  [GCObject.next][GCObject.tt][GCObject.marked]
  [extra][shrlen][hash][u union]
  [*contents][*falloc][*ud] ← actual pointers
  [external string data somewhere else]
```

**Strict Aliasing Issue**:
- For short strings, we're treating the `contents`/`falloc`/`ud` memory region as char array
- These fields are `char*`, `lua_Alloc`, `void*` - different types
- Reading them as `char*` is type punning
- **However**: Constructor explicitly says "NOTE: contents, falloc, ud are NOT initialized"
- For short strings, these bytes just don't semantically exist

**Risk Assessment**: **MEDIUM**
- **Safe in practice** because:
  - Allocation size is computed correctly
  - Type tag (`shrlen >= 0` for short) discriminates behavior
  - No code tries to read these pointers for short strings
- **Fragile because**:
  - Relies on sizeof() and field layout
  - Compiler could theoretically rearrange fields
  - If someone adds a vtable, layout breaks

**Code Comments Show Awareness**:
```cpp
// Phase 50: Constructor - initializes only fields common to both short and long strings
// For short strings: only fields up to 'u' exist (contents/falloc/ud are overlay for string data)
// For long strings: all fields exist

// Phase 50: Destructor - trivial (GC handles deallocation)
// MUST be empty (not = default) because for short strings, not all fields exist in memory!
~TString() noexcept {}
```

**Recommendation**:
- Keep as-is (performance-critical fast path)
- Add `static_assert` to verify field ordering
  ```cpp
  static_assert(offsetof(TString, contents) == TString::contentsOffset());
  ```
- Document the variable-size layout explicitly
- Consider adding debug validation in luaS_* functions

---

### 6. Table Array Hint Type Punning (LOW-MEDIUM SEVERITY)

**Location**: `src/objects/lobject.h:1685-1707`

**Pattern**:
```cpp
inline unsigned int* getLenHint() noexcept {
    return static_cast<unsigned int*>(static_cast<void*>(array));
}

inline const unsigned int* getLenHint() const noexcept {
    return static_cast<const unsigned int*>(static_cast<const void*>(array));
}

inline lu_byte* getArrayTag(lua_Unsigned k) noexcept {
    return static_cast<lu_byte*>(static_cast<void*>(array)) + sizeof(unsigned) + k;
}

inline const lu_byte* getArrayTag(lua_Unsigned k) const noexcept {
    return static_cast<const lu_byte*>(static_cast<const void*>(array)) + sizeof(unsigned) + k;
}

inline Value* getArrayVal(lua_Unsigned k) noexcept {
    return array - 1 - k;
}
```

**Context**:
- Table's array part stores values in a special layout
- For arrays with "count" semantics, length hint is stored at the beginning
- The array pointer points past the header

**Memory Layout**:
```
Array storage:
[count (unsigned)][tag byte][tag byte]...[Value][Value]...[Value]...
                    ↑ getArrayTag(0)
           ↑ getLenHint() points here
                                        ↑ array points here (to first Value)
```

**Type Punning**:
- `array` is `Value*` 
- We cast it to `unsigned int*` to read count
- We cast it to `lu_byte*` to read tags
- Type punning across Value, unsigned int, lu_byte

**Safety Analysis**:
- Pointer is created by `luaM_newvector` on raw void* allocation
- The memory is allocated as raw bytes
- Casting void* → any type is allowed (void* is generic)
- **However**: Code goes `Value* → void* → unsigned int*`
- This double-cast violates strict aliasing if Value* was the original type

**The Issue**:
```cpp
// If array comes from: Value* array = ... 
// Then doing:          unsigned int* = (unsigned int*)(void*)array
// Violates aliasing: compiler assumes Value* and unsigned int* don't alias
```

**Actually Safe Because**:
- The array is allocated as raw bytes via `luaM_newblock`
- Conversion is: `char* → Value*` (initial array setup)
- Then used as generic data store
- The double-cast through void* is just being defensive

**Risk Assessment**: **LOW-MEDIUM**
- **Likely safe** because allocation is untyped
- **Notation is defensive** (void* cast ensures it's not aliasing-based optimization)
- Could be optimized by storing offset instead of pointer

**Recommendation**:
- Keep as-is (well-documented pattern)
- Consider adding explicit documentation:
  ```cpp
  // Array storage layout (allocated as raw bytes, stored as untyped void*):
  // [count:unsigned int][tags:lu_byte...][values:Value...]
  // array points to first Value (after count and tags)
  ```

---

### 7. Pointer to Integer Conversions (INFORMATIONAL)

**Location**: `src/memory/llimits.h:88-102, 209-212`

**Pattern**:
```cpp
#define L_P2I	uintptr_t  /* convert pointer to unsigned integer */

template<typename T>
inline constexpr unsigned int point2uint(T* p) noexcept {
    return cast_uint((L_P2I)(p) & std::numeric_limits<unsigned int>::max());
}
```

**Usage**: Hash computation for pointers (objects, tables, etc.)

**Analysis**:
- Converting pointer → integer → truncated uint for hashing
- Uses `uintptr_t` (safe conversion per C++17)
- Truncation to 32-bit is intentional (hash collision acceptable)
- **Not a strict aliasing issue** (pointer value, not dereferencing)

**Risk Assessment**: **LOW - INFORMATIONAL**
- Safe pattern for hashing
- Well-defined behavior

---

### 8. UpVal::getLevel() Cast (LOW SEVERITY)

**Location**: `src/objects/lobject.h:1250-1253`

**Pattern**:
```cpp
StkId getLevel() const noexcept {
    lua_assert(isOpen());
    return reinterpret_cast<StkId>(v.p);
}
```

**Context**:
- `v.p` is `TValue*` (pointer to stack slot) for open upvalues
- Reinterprets as `StkId` (which is `StackValue*`)
- TValue and StackValue are different types with same size/layout

**Analysis**:
- `StkId = StackValue*`
- `v.p = TValue*` (stored in union)
- `StackValue` is a union containing `TValue` as first member
- **Actually safe**: StackValue.val IS a TValue at offset 0
- But relies on memory layout: `sizeof(TValue) == sizeof(StackValue.val)`

**Risk Assessment**: **LOW**
- Safe because StackValue.val is TValue
- But cast is unnecessary (could use `(StackValue*)(v.p)` conceptually)
- Works in practice

**Recommendation**:
- Safe to keep as-is
- Could add static_assert:
  ```cpp
  static_assert(offsetof(StackValue, val) == 0);
  static_assert(sizeof(TValue) == sizeof(StackValue.val));
  ```

---

## SUMMARY TABLE

| Issue | File | Severity | Type | Status |
|-------|------|----------|------|--------|
| TValue Union Type Punning | ltvalue.h, lobject.h | MEDIUM | Union discrimination | Safe with safeguards |
| GCBase Conversions | lobject.h, lgc.h | MEDIUM | reinterpret_cast | Safe with static layout |
| Stack restore() | lstack.h | HIGH | Pointer round-trip | Works but fragile |
| NodeArray Layout | ltable.cpp | HIGH | Pointer arithmetic trick | Works but complex |
| TString Overlay | lstring.cpp, lobject.h | MEDIUM | Variable-size object | Safe with careful allocation |
| Table Array Tags | lobject.h | LOW-MEDIUM | Type punning | Likely safe |
| Pointer Hashing | llimits.h | LOW | Pointer→int | Safe, intentional |
| UpVal getLevel() | lobject.h | LOW | Minor cast | Safe, unnecessary |

---

## COMPILER OPTIMIZATION RISKS

### Current Compiler (GCC/Clang without LTO)
- **Status**: All patterns work correctly
- **Why**: Conservative aliasing analysis doesn't assume aggressive optimizations

### With Link Time Optimization (LTO)
- **Risk**: HIGH for patterns 3 (stack) and 4 (NodeArray)
- **Why**: LTO can track pointer conversions across translation units and make assumptions

### With Full Program Optimization (-fwhole-program)
- **Risk**: MEDIUM for patterns 1 (TValue union) and 2 (GC conversions)
- **Why**: Could reorder checks or assume union members don't interfere

### With Aggressive Inlining
- **Risk**: MEDIUM for pattern 7 (Table array)
- **Why**: Might inline pointer arithmetic and lose context

---

## RECOMMENDATIONS (PRIORITY ORDER)

### 1. FIX: Stack Pointer Arithmetic (HIGH PRIORITY)
**File**: `src/core/lstack.h:118-125`

Replace char* intermediate with direct pointer arithmetic:
```cpp
inline ptrdiff_t save(StkId pt) const noexcept {
    return pt - stack.p;  // Direct, no char* conversion
}

inline StkId restore(ptrdiff_t n) const noexcept {
    return stack.p + n;
}
```

**Impact**: Safer with optimizing compilers, clearer intent
**Risk**: Very low - same operation, better expression
**Effort**: 10 minutes

---

### 2. IMPROVE: NodeArray Implementation (HIGH PRIORITY)
**File**: `src/objects/ltable.cpp:105-136`

Consider explicit structure instead of clever offset math:
```cpp
struct NodeHeader {
    Limbox limbox;
    Node nodes[1];
};

static Node* allocate(lua_State* L, unsigned int n, bool withLastfree) {
    if (withLastfree) {
        size_t sz = sizeof(Limbox) + n * sizeof(Node);
        NodeHeader* header = new (luaM_newblock(L, sz)) NodeHeader;
        header->limbox.lastfree = header->nodes + n;
        return header->nodes;
    }
    return luaM_newvector(L, n, Node);
}
```

**Impact**: Clearer intent, safer aliasing
**Risk**: Same memory layout, minimal performance impact
**Effort**: 30 minutes + testing

---

### 3. ADD: Assertions for Union Discrimination (MEDIUM PRIORITY)
**Files**: `src/objects/ltvalue.h`, `src/objects/lobject.h`

Add runtime checks in debug mode:
```cpp
inline lua_Number TValue::numberValue() const noexcept {
  if (tt_ == LUA_VNUMINT) {
    return static_cast<lua_Number>(value_.i);
  } else {
    lua_assert(tt_ == LUA_VNUMFLT);
    return value_.n;
  }
}
```

**Impact**: Catch union misuse in debug builds
**Risk**: No runtime cost in release builds
**Effort**: 2 hours

---

### 4. DOCUMENT: TString Variable-Size Layout (MEDIUM PRIORITY)
**File**: `src/objects/lobject.h:445-556`

Add detailed comments and static_asserts:
```cpp
// Short string layout (allocated with exact size):
//   [GCObject fields: 24 bytes]
//   [extra: 1 byte][shrlen: 1 byte][hash: 4 bytes][union u: 8 bytes]
//   [string data starts here: strlen+1 bytes]
// Long string layout (full size):
//   [GCObject fields]
//   [extra][shrlen][hash][u][*contents][*falloc][*ud]
```

**Impact**: Prevents accidental layout changes
**Risk**: None
**Effort**: 1 hour

---

### 5. ADD: Static Layout Verification (LOW PRIORITY)
**Files**: Various class definitions

Add compile-time assertions:
```cpp
static_assert(sizeof(TString) == offsetof(TString, contents) + sizeof(char*));
static_assert(alignof(StackValue) == alignof(TValue));
static_assert(offsetof(NodeHeader, nodes) == sizeof(Limbox));
```

**Impact**: Prevent layout surprises from compiler changes
**Risk**: None
**Effort**: 2 hours

---

## TESTING RECOMMENDATIONS

1. **Strict Aliasing Test Suite**
   - Create `test_aliasing.cpp` with scenarios exercising each pattern
   - Run with `-Wstrict-aliasing=2` to catch warnings
   - Verify behavior with UBSan

2. **Layout Verification**
   - Add `test_memory_layout.cpp` checking all sizeof() and offsetof()
   - Run on multiple platforms/compilers

3. **Compiler Variations**
   - Test with GCC -O3, Clang -O3
   - Test with LTO enabled (`-flto`)
   - Test with UBSan (`-fsanitize=undefined`)
   - Test with AddressSanitizer (`-fsanitize=address`)

4. **Integration Testing**
   - Run full test suite with strict compiler flags
   - Monitor for crashes under aggressive optimization

---

## CONCLUSION

The codebase inherits strict aliasing patterns from Lua C implementation. While most patterns work in practice, they're fragile and could break with:
- More aggressive compiler optimizations
- LTO becoming more sophisticated
- Port to new architecture with different alignment

**Priority fixes** (stack restore, NodeArray) should be addressed in Phase 116.

**Current status**: Compiles and runs correctly with test suite passing. No imminent issues expected with current compiler settings. Monitor closely if enabling LTO.

