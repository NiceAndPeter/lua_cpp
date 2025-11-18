# TValue std::variant Replacement Plan

**Created**: 2025-11-18
**Status**: Planning Phase
**Complexity**: VERY HIGH
**Risk Level**: CRITICAL - Performance sensitive
**Estimated Effort**: 80-120 hours

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Current State Analysis](#current-state-analysis)
3. [Goals and Constraints](#goals-and-constraints)
4. [Architecture Options](#architecture-options)
5. [Recommended Approach](#recommended-approach)
6. [Implementation Roadmap](#implementation-roadmap)
7. [Performance Analysis](#performance-analysis)
8. [Risk Assessment](#risk-assessment)
9. [Migration Strategy](#migration-strategy)
10. [Success Criteria](#success-criteria)

---

## Executive Summary

### Current Situation

TValue is Lua's **most critical data structure**, used billions of times per execution. It currently uses:
- Manual discriminated union (`Value` union + `lu_byte tt_` tag)
- 9 bytes total (8-byte union + 1-byte tag, typically padded to 16 bytes)
- Sophisticated type tagging with variants and collectable bit
- Zero-cost abstractions via inline methods

### Proposal

Replace TValue's discriminated union with `std::variant`, achieving:
- ✅ Modern C++ type safety
- ✅ Compile-time correctness
- ✅ Better tooling support (sanitizers, static analysis)
- ⚠️ **CRITICAL**: Must maintain zero performance regression

### Challenge

TValue is used in **VM hot path** (lvm.cpp, 1534 lines of interpreter loop). Any overhead is multiplied by billions. Previous union analysis (UNION_REMOVAL_ANALYSIS.md) recommended **RETAINING** the Value union for performance.

### Three Possible Approaches

1. **Pure std::variant** - Complete replacement (highest risk)
2. **Hybrid TValue** - std::variant with custom tag optimization (medium risk)
3. **Type-Safe Wrapper** - Keep union, wrap with variant-like API (lowest risk)

---

## Current State Analysis

### TValue Structure

```cpp
// Current implementation (ltvalue.h:69-216)
class TValue {
private:
  Value value_;     // 8-byte union
  lu_byte tt_;      // 1-byte type tag

public:
  // 50+ inline accessor methods
  // Type checking (isNil, isNumber, isString, etc.)
  // Value extraction (intValue, floatValue, gcValue, etc.)
  // Setters (setInt, setFloat, setString, etc.)
};
```

### Value Union

```cpp
// Current union (ltvalue.h:41-49)
typedef union Value {
  GCObject *gc;      // Collectable objects (strings, tables, closures, etc.)
  void *p;           // Light userdata
  lua_CFunction f;   // Light C functions
  lua_Integer i;     // Integer numbers (typically int64_t)
  lua_Number n;      // Float numbers (typically double)
  lu_byte ub;        // Padding/initialization
} Value;
```

**All members are 8 bytes** (pointer-sized on 64-bit systems)

### Type Tag Encoding (Sophisticated!)

```cpp
// bits 0-3: actual tag (LUA_TNIL, LUA_TNUMBER, etc.)
// bits 4-5: variant bits (e.g., VNUMINT vs VNUMFLT)
// bit 6: whether value is collectable (BIT_ISCOLLECTABLE)

// Examples:
LUA_VNIL       = makevariant(LUA_TNIL, 0)      // 0x00
LUA_VEMPTY     = makevariant(LUA_TNIL, 1)      // 0x10
LUA_VNUMINT    = makevariant(LUA_TNUMBER, 0)   // 0x03
LUA_VNUMFLT    = makevariant(LUA_TNUMBER, 1)   // 0x13
LUA_VSHRSTR    = ctb(makevariant(LUA_TSTRING, 0))  // 0x44 (collectable bit set)
```

**Key insight**: Lua needs 20+ distinct type tags, not just 9 base types!

### Lua Type System (Complex!)

**Base Types** (9):
- LUA_TNIL, LUA_TBOOLEAN, LUA_TNUMBER, LUA_TSTRING, LUA_TTABLE,
  LUA_TFUNCTION, LUA_TUSERDATA, LUA_TLIGHTUSERDATA, LUA_TTHREAD

**Variants** (20+ total):
- Nil: VNIL, VEMPTY, VABSTKEY, VNOTABLE (4 variants!)
- Boolean: VFALSE, VTRUE
- Number: VNUMINT, VNUMFLT
- String: VSHRSTR, VLNGSTR
- Function: VLCL (Lua closure), VLCF (light C function), VCCL (C closure)
- Userdata: VLIGHTUSERDATA, VUSERDATA
- Table: VTABLE
- Thread: VTHREAD

**Collectable vs Non-Collectable**:
- Collectable: Strings, tables, closures, full userdata, threads (need GC)
- Non-collectable: Nil, booleans, numbers, light userdata, light C functions

### Usage Patterns

**VM Hot Path** (lvm.cpp):
- Stack slot access: `s2v(ra)`, `s2v(rb)` (every instruction)
- Type checking: `ttisinteger`, `ttisnumber`, `ttisstring`
- Value extraction: `ivalue`, `fltvalue`, `tsvalue`
- Arithmetic: Direct value_.i, value_.n access
- Frequency: **Billions of times per benchmark**

**API Layer** (lapi.cpp):
- Stack manipulation
- Type conversion
- Value creation
- Moderate frequency

**Compiler** (lparser.cpp, lcode.cpp):
- Constant handling
- Type checking
- Low frequency (compile-time only)

### Performance Profile

**From UNION_REMOVAL_ANALYSIS.md**:
> Risk if removed: 5-15% performance regression (unacceptable)

**Current Performance**:
- Baseline: 4.20s (Nov 2025)
- Target: ≤4.33s (≤3% regression tolerance)
- **Budget**: Only 0.13s for ALL changes

**Critical Insight**: Even 1ns overhead per TValue access → several seconds total

---

## Goals and Constraints

### Goals

1. **Type Safety**: Replace manual discriminated union with type-safe std::variant
2. **Modernization**: Use standard C++ instead of C-style type punning
3. **Tool Support**: Enable better static analysis, sanitizers, debugging
4. **Maintainability**: Clearer code with compile-time guarantees
5. **Correctness**: Eliminate undefined behavior from union type punning

### Hard Constraints

1. **Performance**: ≤4.33s (≤3% regression) - **NON-NEGOTIABLE**
2. **C API Compatibility**: Public API (lua.h) unchanged
3. **Memory Layout**: Stack arrays (StkId) must remain efficient
4. **Zero-Cost Abstraction**: All accessors must inline
5. **GC Integration**: Collectable bit must be preserved/accessible

### Soft Constraints

1. Incremental migration possible
2. Minimize code churn (35,000+ LOC codebase)
3. Preserve git history (no massive search-replace)
4. Test coverage maintained throughout

---

## Architecture Options

### Option 1: Pure std::variant (Radical Approach)

#### Design

```cpp
// Pure std::variant approach
class TValue {
private:
  using ValueVariant = std::variant<
    std::monostate,        // Nil (but which variant?)
    bool,                  // Boolean
    lua_Integer,           // Integer
    lua_Number,            // Float
    void*,                 // Light userdata
    lua_CFunction,         // Light C function
    GCObject*              // ALL collectable types
  >;

  ValueVariant value_;
  lu_byte tt_;  // STILL NEEDED for Lua's 20+ type variants!

public:
  // Type checks using std::holds_alternative
  bool isInteger() const noexcept {
    return std::holds_alternative<lua_Integer>(value_) && tt_ == LUA_VNUMINT;
  }

  // Value access using std::get
  lua_Integer intValue() const noexcept {
    return std::get<lua_Integer>(value_);
  }
};
```

#### Problems (SHOW-STOPPERS!)

**Problem 1: Type Tag Redundancy**
- std::variant has internal discriminator (size_t, typically 8 bytes!)
- We STILL need tt_ for Lua's 20+ variants
- **Result**: 8 (union) + 8 (discriminator) + 1 (tt_) = **17 bytes** (vs current 9)
- Stack arrays become **2x larger** → cache thrashing → massive slowdown

**Problem 2: Multiple Nil Variants**
- Lua has 4 nil variants: VNIL, VEMPTY, VABSTKEY, VNOTABLE
- std::variant can't represent this with single monostate
- Need separate types? `struct Nil{}, struct Empty{}, struct AbstKey{}` → combinatorial explosion

**Problem 3: GC Integration**
- All collectable types → single `GCObject*` variant
- Loses type information at variant level
- Still need tt_ to distinguish TString* vs Table* vs Closure*
- **No benefit from variant type safety!**

**Problem 4: Access Overhead**
- `std::get<lua_Integer>(value_)` not guaranteed zero-cost
- Exception throwing on wrong type (can disable with `std::get_if`)
- Visitor pattern adds indirection: `std::visit([](auto&& arg) { ... }, value_)`

**Problem 5: Hot Path Performance**
```cpp
// Current (zero-cost):
lua_Integer i = v->value_.i;  // Direct union access

// With std::variant:
lua_Integer i = std::get<lua_Integer>(v->value_);  // Function call, bounds check
```

**Estimated Performance Impact**: ❌ **10-20% regression** - UNACCEPTABLE

#### Verdict

🚫 **NOT VIABLE** - Violates all performance constraints

---

### Option 2: Hybrid std::variant (Optimized Approach)

#### Design Idea

Keep std::variant for type safety, but optimize for Lua's specific needs:

```cpp
// Attempt to optimize std::variant
class TValue {
private:
  // Custom variant with 1-byte discriminator
  using ValueVariant = std::variant<
    lua_Integer,
    lua_Number,
    GCObject*,
    void*,
    lua_CFunction
    // Nil represented as std::monostate or special value
  >;

  ValueVariant value_;
  // Try to reuse variant's discriminator instead of separate tt_?

public:
  // Inline accessors
  inline lua_Integer intValue() const noexcept {
    return std::get<lua_Integer>(value_);
  }
};
```

#### Optimization Attempts

**Attempt 1: Small Discriminator**
- Problem: std::variant discriminator size not controllable (implementation-defined)
- Typical: size_t (8 bytes), best case: size_t optimized to 1 byte
- **Unpredictable across compilers**

**Attempt 2: Reuse Variant Discriminator**
- Idea: Map Lua's tt_ to variant index
- Problem: Lua has 20+ types, variant has 5-7 alternatives
- **Can't eliminate tt_ anyway**

**Attempt 3: Custom Variant Implementation**
```cpp
// Hand-rolled variant with 1-byte discriminator
template<typename... Ts>
class SmallVariant {
  union { Ts... } data_;
  uint8_t index_;
};
```
- **Problem: This is just reinventing the current union!**

#### Challenges

1. **std::variant spec doesn't guarantee zero-cost** in hot paths
2. **Compiler optimizations unpredictable** across GCC, Clang, MSVC
3. **We can't eliminate tt_** due to Lua's type system complexity
4. **Cache line size still grows** (variant discriminator + tt_)

**Estimated Performance Impact**: ⚠️ **3-10% regression** - HIGH RISK

#### Verdict

⚠️ **HIGH RISK** - Might work but unpredictable, hard to guarantee performance

---

### Option 3: Type-Safe Wrapper (Conservative Approach)

#### Design Philosophy

Keep current zero-cost union, wrap with variant-like type-safe API:

```cpp
// Keep existing union for performance
typedef union Value {
  GCObject *gc;
  void *p;
  lua_CFunction f;
  lua_Integer i;
  lua_Number n;
} Value;

// New: Type-safe wrapper class
class TValue {
private:
  Value value_;     // Keep union (zero-cost)
  lu_byte tt_;      // Keep tag

public:
  // Add std::variant-LIKE API (but using union underneath)

  // Visitor pattern (type-safe access)
  template<typename Visitor>
  auto visit(Visitor&& vis) const {
    switch (baseType()) {
      case LUA_TNIL:     return vis(std::monostate{});
      case LUA_TBOOLEAN: return vis(isTrue());
      case LUA_TNUMBER:
        return isInteger() ? vis(intValue()) : vis(floatValue());
      case LUA_TSTRING:  return vis(stringValue());
      case LUA_TTABLE:   return vis(tableValue());
      // ... etc
    }
  }

  // std::get-like type-safe extraction
  template<typename T>
  T get() const {
    if constexpr (std::is_same_v<T, lua_Integer>) {
      if (!isInteger()) throw std::bad_variant_access();
      return value_.i;
    } else if constexpr (std::is_same_v<T, lua_Number>) {
      if (!isFloat()) throw std::bad_variant_access();
      return value_.n;
    }
    // ... etc
  }

  // std::get_if-like safe extraction
  template<typename T>
  std::optional<T> get_if() const noexcept {
    if constexpr (std::is_same_v<T, lua_Integer>) {
      return isInteger() ? std::optional(value_.i) : std::nullopt;
    }
    // ... etc
  }

  // Keep existing zero-cost accessors for hot path
  inline lua_Integer intValue() const noexcept { return value_.i; }
  inline lua_Number floatValue() const noexcept { return value_.n; }
  // ... all existing methods unchanged
};
```

#### Benefits

✅ **Zero Performance Regression**
- Hot path uses existing direct accessors: `v->intValue()` → `value_.i`
- No std::variant overhead

✅ **Type Safety Available**
- New code can use `visit()`, `get<T>()`, `get_if<T>()`
- Visitor pattern catches missing cases at compile-time

✅ **Incremental Migration**
- Existing code unchanged
- New features can adopt type-safe API
- Gradually migrate non-critical paths

✅ **Best of Both Worlds**
- Performance: union
- Safety: variant-like API
- Compatibility: existing code works

#### Example Usage

```cpp
// Hot path (VM): Keep using direct accessors (zero-cost)
if (ttisinteger(ra)) {
  lua_Integer ia = ivalue(ra);  // Direct union access
  // ... fast arithmetic
}

// Non-critical path: Use type-safe API
tvalue->visit([](auto&& val) {
  using T = std::decay_t<decltype(val)>;
  if constexpr (std::is_same_v<T, lua_Integer>) {
    std::cout << "Integer: " << val;
  } else if constexpr (std::is_same_v<T, TString*>) {
    std::cout << "String: " << val->c_str();
  }
  // Compile error if we forget a type!
});

// Safe optional extraction
if (auto i = tvalue->get_if<lua_Integer>()) {
  // Use *i safely
}
```

#### Tradeoffs

**Pros**:
- ✅ Zero performance regression
- ✅ Type safety available where needed
- ✅ Incremental migration
- ✅ Backward compatible
- ✅ Best of both worlds

**Cons**:
- ⚠️ Still uses union (not "pure" modern C++)
- ⚠️ Duplicate API (old + new)
- ⚠️ Potential for misuse (mixing APIs)
- ⚠️ Not true std::variant (wrapper around union)

**Estimated Performance Impact**: ✅ **0% regression** (hot path unchanged)

#### Verdict

✅ **RECOMMENDED** - Achieves goals without performance risk

---

## Recommended Approach

### Strategy: Hybrid Wrapper (Option 3) with Future Migration Path

**Phase 1 (Immediate)**: Type-Safe Wrapper
- Keep union for zero-cost hot path
- Add variant-like API (visit, get, get_if)
- Migrate non-critical code to new API
- **Duration**: 20-30 hours
- **Risk**: LOW

**Phase 2 (After Proving Zero-Cost)**: Selective std::variant
- Profile and identify cold paths
- Replace TValue with true std::variant in cold paths only
- Keep union in hot paths (VM, stack operations)
- **Duration**: 30-40 hours
- **Risk**: MEDIUM

**Phase 3 (Future, Optional)**: Full std::variant
- Only if compiler technology improves
- Only if we can prove zero-cost in hot path
- Requires extensive benchmarking
- **Duration**: 40-60 hours
- **Risk**: HIGH

### Recommended Implementation: Phase 1 Details

```cpp
// File: src/objects/ltvalue_variant.h (NEW)

#ifndef ltvalue_variant_h
#define ltvalue_variant_h

#include "ltvalue.h"
#include <optional>
#include <variant>

// Type-safe visitor support for TValue
namespace TValueVariant {

// Type list for variant-like operations
using NilType = std::monostate;
using BoolType = bool;
using IntType = lua_Integer;
using FloatType = lua_Number;
using LightUserdataType = void*;
using LightCFunctionType = lua_CFunction;
using StringType = TString*;
using TableType = Table*;
using ClosureType = Closure*;
using UserdataType = Udata*;
using ThreadType = lua_State*;

} // namespace TValueVariant

// Extension to TValue class (in ltvalue.h)
class TValue {
  // ... existing members ...

public:
  // Variant-like visitor pattern (compile-time type safety)
  template<typename Visitor>
  inline auto visit(Visitor&& vis) const {
    using namespace TValueVariant;

    // Dispatch based on type tag
    switch (baseType()) {
      case LUA_TNIL:
        return vis(NilType{});

      case LUA_TBOOLEAN:
        return vis(isTrue());

      case LUA_TNUMBER:
        if (isInteger())
          return vis(intValue());
        else
          return vis(floatValue());

      case LUA_TSTRING:
        return vis(stringValue());

      case LUA_TTABLE:
        return vis(tableValue());

      case LUA_TFUNCTION:
        if (isLightCFunction())
          return vis(functionValue());
        else
          return vis(closureValue());

      case LUA_TUSERDATA:
        return vis(userdataValue());

      case LUA_TLIGHTUSERDATA:
        return vis(pointerValue());

      case LUA_TTHREAD:
        return vis(threadValue());

      default:
        lua_assert(false);  // Should never happen
        return vis(NilType{});
    }
  }

  // Type-safe get<T>() with exception (like std::variant)
  template<typename T>
  inline T get() const {
    if (auto result = get_if<T>()) {
      return *result;
    }
    throw std::bad_variant_access();
  }

  // Type-safe get_if<T>() (like std::variant)
  template<typename T>
  inline std::optional<T> get_if() const noexcept {
    using namespace TValueVariant;

    if constexpr (std::is_same_v<T, IntType>) {
      return isInteger() ? std::optional(intValue()) : std::nullopt;
    }
    else if constexpr (std::is_same_v<T, FloatType>) {
      return isFloat() ? std::optional(floatValue()) : std::nullopt;
    }
    else if constexpr (std::is_same_v<T, BoolType>) {
      return isBoolean() ? std::optional(isTrue()) : std::nullopt;
    }
    else if constexpr (std::is_same_v<T, StringType>) {
      return isString() ? std::optional(stringValue()) : std::nullopt;
    }
    else if constexpr (std::is_same_v<T, TableType>) {
      return isTable() ? std::optional(tableValue()) : std::nullopt;
    }
    else if constexpr (std::is_same_v<T, LightUserdataType>) {
      return isLightUserdata() ? std::optional(pointerValue()) : std::nullopt;
    }
    else if constexpr (std::is_same_v<T, LightCFunctionType>) {
      return isLightCFunction() ? std::optional(functionValue()) : std::nullopt;
    }
    else if constexpr (std::is_same_v<T, ClosureType>) {
      return isClosure() ? std::optional(closureValue()) : std::nullopt;
    }
    else if constexpr (std::is_same_v<T, UserdataType>) {
      return isFullUserdata() ? std::optional(userdataValue()) : std::nullopt;
    }
    else if constexpr (std::is_same_v<T, ThreadType>) {
      return isThread() ? std::optional(threadValue()) : std::nullopt;
    }
    else {
      static_assert(sizeof(T) == 0, "Unsupported type for TValue::get_if");
      return std::nullopt;
    }
  }

  // holds_alternative<T>() (like std::variant)
  template<typename T>
  inline bool holds_alternative() const noexcept {
    return get_if<T>().has_value();
  }
};

#endif // ltvalue_variant_h
```

---

## Implementation Roadmap

### Phase 1: Type-Safe Wrapper API (20-30 hours)

**Goal**: Add variant-like API without changing union

#### Step 1.1: Add Visitor Support (4 hours)
- [ ] Create `src/objects/ltvalue_variant.h`
- [ ] Implement `visit()` method
- [ ] Add comprehensive tests for all type combinations
- [ ] Benchmark: Ensure zero overhead when not used

**Files Modified**:
- NEW: `src/objects/ltvalue_variant.h`
- `src/objects/ltvalue.h` (add visit method)
- NEW: `testes/variant.lua` (tests)

#### Step 1.2: Add get<T>() Support (4 hours)
- [ ] Implement `get<T>()` with exceptions
- [ ] Implement `get_if<T>()` with std::optional
- [ ] Implement `holds_alternative<T>()`
- [ ] Add tests for type safety (compile-time + runtime)

**Files Modified**:
- `src/objects/ltvalue_variant.h`
- `testes/variant.lua`

#### Step 1.3: Migrate Non-Critical Code (8-12 hours)
- [ ] Identify cold paths (using profiling data)
- [ ] Migrate debug code (ldebug.cpp) to visitor API
- [ ] Migrate auxiliary code (lauxlib.cpp) to get_if<T>
- [ ] Migrate standard library (lbaselib.cpp, etc.) selectively

**Files Modified** (selective):
- `src/core/ldebug.cpp` (~50 call sites)
- `src/auxiliary/lauxlib.cpp` (~30 call sites)
- `src/libraries/lbaselib.cpp` (~20 call sites)

#### Step 1.4: Documentation & Examples (2 hours)
- [ ] Add TVALUE_VARIANT_USAGE.md guide
- [ ] Document when to use old vs new API
- [ ] Add examples for common patterns

#### Step 1.5: Benchmarking (2 hours)
- [ ] Run full benchmark suite (5 runs)
- [ ] Verify ≤4.33s performance target
- [ ] Profile hot paths to ensure zero regression

**Success Criteria**:
- ✅ All tests passing
- ✅ Performance ≤4.33s (≤3% regression)
- ✅ Variant API available for new code
- ✅ Existing code unchanged and working

---

### Phase 2: Selective std::variant (30-40 hours) - OPTIONAL

**Goal**: Replace union with true std::variant in cold paths

**Prerequisites**:
- Phase 1 complete
- Extensive profiling showing cold paths
- Compiler guarantees on std::variant size/performance

#### Step 2.1: Create TValueVariant Class (8 hours)
```cpp
// For COLD paths only
class TValueVariant {
private:
  using Variant = std::variant<
    std::monostate,    // Nil
    bool,              // Boolean
    lua_Integer,       // Int
    lua_Number,        // Float
    TString*,          // String
    Table*,            // Table
    Closure*,          // Closure
    Udata*,            // Userdata
    void*,             // Light userdata
    lua_CFunction,     // Light C function
    lua_State*         // Thread
  >;
  Variant value_;

public:
  // std::variant API
  template<typename T> T& get();
  template<typename T> bool holds_alternative() const;
  template<typename Visitor> auto visit(Visitor&& v);
};
```

#### Step 2.2: Migrate Cold Paths (15-20 hours)
- [ ] Debug code (ldebug.cpp)
- [ ] Auxiliary library (lauxlib.cpp)
- [ ] Standard libraries (lbaselib.cpp, lstrlib.cpp, etc.)
- [ ] Serialization (lundump.cpp, ldump.cpp)

**Constraint**: **NEVER touch hot paths** (lvm.cpp, ldo.cpp, lapi.cpp stack operations)

#### Step 2.3: Dual-Mode Support (5-7 hours)
- [ ] TValue (union-based) for hot paths
- [ ] TValueVariant (std::variant) for cold paths
- [ ] Conversion functions between them
- [ ] Benchmark to ensure no cross-contamination

#### Step 2.4: Extensive Benchmarking (2-5 hours)
- [ ] 20-run benchmark (statistical significance)
- [ ] Profiling to verify no hot path impact
- [ ] Memory usage analysis
- [ ] Revert if >3% regression

**Success Criteria**:
- ✅ Cold paths use true std::variant
- ✅ Hot paths unchanged (union)
- ✅ Performance ≤4.33s
- ✅ Type safety improved

---

### Phase 3: Full std::variant (40-60 hours) - FUTURE/RESEARCH

**Status**: **NOT RECOMMENDED** until compiler technology improves

**Blockers**:
1. std::variant discriminator size (8 bytes typical)
2. No control over memory layout
3. Unpredictable optimization across compilers
4. Lua's 20+ type variants don't map to std::variant well

**Research Directions**:
- Wait for C++26/29 improvements to std::variant
- Monitor compiler optimizations (GCC 15+, Clang 18+)
- Investigate custom variant implementations
- Consider P2900R6 (constexpr std::variant)

**Only proceed if**:
- Compiler guarantees zero-cost
- Benchmarking proves no regression
- Community consensus reached

---

## Performance Analysis

### Memory Layout Impact

#### Current TValue
```
Offset  Size  Field       Description
0       8     value_      Union (8 bytes: pointer/int64/double)
8       1     tt_         Type tag
9       7     (padding)   Alignment to 16 bytes
Total: 16 bytes per TValue
```

**Stack Array** (256 slots):
- 256 * 16 = **4,096 bytes** (fits in L1 cache: 32KB typical)

#### With std::variant (Naive)
```
Offset  Size  Field           Description
0       8     value_          Variant data
8       8     discriminator_  Variant index (size_t)
16      1     tt_             Lua type tag (STILL NEEDED!)
17      7     (padding)
Total: 24 bytes per TValue  (+50% size!)
```

**Stack Array** (256 slots):
- 256 * 24 = **6,144 bytes** (cache pressure increased)

**Impact**: Cache misses → 5-10% slowdown

#### With Optimized std::variant (Best Case)
```
Offset  Size  Field           Description
0       8     value_          Variant data
8       1     discriminator_  Variant index (IF compiler optimizes)
9       1     tt_             Lua type tag
10      6     (padding)
Total: 16 bytes (SAME as current)
```

**Challenge**: Not guaranteed by standard!

### Access Pattern Impact

#### Current (Union)
```cpp
// Direct memory access (1 cycle)
lua_Integer i = v->value_.i;

// Assembly (x86-64):
mov rax, [rdi]  ; Load 8 bytes from offset 0
```

#### With std::variant
```cpp
// std::get with bounds checking
lua_Integer i = std::get<lua_Integer>(v->value_);

// Assembly (typical):
mov rax, [rdi+8]      ; Load discriminator
cmp rax, 2            ; Check if index == lua_Integer
jne throw_exception   ; Branch if wrong type
mov rax, [rdi]        ; Load value
```

**Extra cost**: 1 load, 1 compare, 1 branch → 3-5 cycles per access

**In VM loop** (billions of accesses):
- 3 cycles * 2 billion accesses = 6 billion cycles
- At 3 GHz: +2 seconds **just from variant overhead**

### Benchmark Predictions

| Approach | Memory/TValue | Cache Impact | Access Cost | Predicted Time | Verdict |
|----------|---------------|--------------|-------------|----------------|---------|
| Current (union) | 16 bytes | Baseline | 1 cycle | 4.20s | ✅ Baseline |
| Option 1 (Pure variant) | 24 bytes | +50% misses | 3-5 cycles | 5.5-6.0s | ❌ FAIL |
| Option 2 (Hybrid variant) | 16-20 bytes | +10-25% misses | 2-4 cycles | 4.8-5.2s | ⚠️ RISK |
| Option 3 (Wrapper) | 16 bytes | No change | 1 cycle | 4.20s | ✅ SAFE |

---

## Risk Assessment

### Critical Risks

#### Risk 1: Performance Regression
**Severity**: CRITICAL
**Probability**: HIGH (Options 1-2), LOW (Option 3)
**Impact**: Project failure (violates core constraint)

**Mitigation**:
- ✅ Extensive benchmarking before/after
- ✅ Revert immediately if >3% regression
- ✅ Start with Option 3 (zero risk)
- ✅ Profile every change

#### Risk 2: Memory Bloat
**Severity**: HIGH
**Probability**: MEDIUM (Options 1-2), ZERO (Option 3)
**Impact**: Cache thrashing, memory pressure

**Mitigation**:
- ✅ Measure sizeof(TValue) at every step
- ✅ Use static_assert to enforce size
- ✅ Monitor stack memory usage

#### Risk 3: Compiler Dependency
**Severity**: MEDIUM
**Probability**: HIGH (Options 1-2), LOW (Option 3)
**Impact**: Unpredictable performance across compilers

**Mitigation**:
- ✅ Test on GCC, Clang, MSVC
- ✅ Use compiler-explorer to check codegen
- ✅ Avoid compiler-specific optimizations

#### Risk 4: Type System Impedance Mismatch
**Severity**: HIGH
**Probability**: HIGH (All options)
**Impact**: Complex code, hard to maintain

**Challenge**: Lua has 20+ type variants, std::variant doesn't map well

**Mitigation**:
- ✅ Option 3 avoids this (keeps tt_)
- ✅ Document type mapping clearly
- ✅ Use strong typing where possible

### Medium Risks

#### Risk 5: Code Churn
**Severity**: MEDIUM
**Probability**: MEDIUM
**Impact**: Large diffs, merge conflicts, review burden

**Mitigation**:
- ✅ Incremental migration
- ✅ Small, focused commits
- ✅ Keep old API working

#### Risk 6: Testing Gaps
**Severity**: MEDIUM
**Probability**: LOW
**Impact**: Bugs in production

**Mitigation**:
- ✅ Comprehensive test suite (30+ files)
- ✅ Test every phase
- ✅ Use sanitizers (ASAN, UBSAN)

### Low Risks

#### Risk 7: API Confusion
**Severity**: LOW
**Probability**: MEDIUM
**Impact**: Developers use wrong API

**Mitigation**:
- ✅ Clear documentation
- ✅ Naming conventions
- ✅ Code review guidelines

---

## Migration Strategy

### Recommended Incremental Path

```
Week 1-2: Phase 1.1-1.2 (Visitor + get API)
├─ Add variant-like wrapper
├─ Comprehensive tests
└─ Benchmark (must be ≤4.33s)

Week 3-4: Phase 1.3 (Migrate cold paths)
├─ ldebug.cpp → visitor API
├─ lauxlib.cpp → get_if API
└─ Benchmark (must be ≤4.33s)

Week 5: Phase 1.4-1.5 (Document + final bench)
├─ Usage guide
├─ 20-run benchmark
└─ Commit Phase 1

--- STOP HERE and evaluate ---

Week 6-8: Phase 2 (OPTIONAL - if Phase 1 successful)
├─ TValueVariant class for cold paths
├─ Selective migration
└─ Extensive benchmarking

--- STOP HERE and evaluate ---

Future: Phase 3 (RESEARCH ONLY)
└─ Wait for compiler/language improvements
```

### Git Strategy

```bash
# Phase 1: Type-safe wrapper
git checkout -b claude/tvalue-variant-wrapper-<session-id>
# ... implement Phase 1.1-1.5 ...
git commit -m "Phase 1.1: Add TValue visitor support"
git commit -m "Phase 1.2: Add TValue get<T> support"
git commit -m "Phase 1.3: Migrate ldebug to visitor API"
git commit -m "Phase 1.4: Document variant API usage"
git commit -m "Phase 1.5: Benchmark - 4.20s (zero regression)"
git push -u origin claude/tvalue-variant-wrapper-<session-id>

# Create PR only after all benchmarks pass
gh pr create --title "TValue variant-like wrapper API" --body "..."
```

### Testing Strategy

#### Unit Tests
```lua
-- testes/variant.lua
print("testing TValue variant API")

-- Test visitor pattern
local function test_visitor()
  local v = create_integer(42)
  v:visit({
    [integer_type] = function(i) assert(i == 42) end,
    [other] = function() error("wrong type") end
  })
end

-- Test get<T>()
local function test_get()
  local v = create_string("hello")
  local s = v:get(string_type)
  assert(s == "hello")

  -- Should throw
  local ok, err = pcall(function() v:get(integer_type) end)
  assert(not ok)
end

-- Test get_if<T>()
local function test_get_if()
  local v = create_integer(42)
  local i = v:get_if(integer_type)
  assert(i == 42)

  local s = v:get_if(string_type)
  assert(s == nil)  -- Wrong type returns nil
end

test_visitor()
test_get()
test_get_if()
print("variant API tests OK")
```

#### Performance Tests
```bash
# Before any change
for i in 1 2 3 4 5; do \
  ../build/lua all.lua 2>&1 | grep "total time:"; \
done
# Average: 4.20s

# After Phase 1.1
# ... same benchmark ...
# Average: 4.20s (must match!)

# After Phase 1.3
# ... same benchmark ...
# Average: ≤4.33s (≤3% regression OK if necessary)
```

#### Regression Tests
```bash
# Run full test suite
cd testes
../build/lua all.lua
# Must output: "final OK !!!"

# Run with sanitizers
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
  -DLUA_ENABLE_ASAN=ON -DLUA_ENABLE_UBSAN=ON
cmake --build build
cd testes
../build/lua all.lua
# No sanitizer errors
```

---

## Success Criteria

### Phase 1 Success
- ✅ Variant-like API available (visit, get, get_if, holds_alternative)
- ✅ Performance ≤4.33s (≤3% regression from 4.20s baseline)
- ✅ All 30+ test files passing
- ✅ Zero sanitizer errors
- ✅ Cold paths migrated to type-safe API
- ✅ Hot paths unchanged (VM interpreter)
- ✅ Documentation complete

### Phase 2 Success (if pursued)
- ✅ True std::variant used in cold paths
- ✅ Performance ≤4.33s
- ✅ Type safety improved
- ✅ Memory usage unchanged

### Overall Project Success
- ✅ Modern C++ type safety achieved
- ✅ Zero performance regression
- ✅ C API compatibility preserved
- ✅ Maintainability improved
- ✅ Tool support improved (sanitizers, static analysis)

### Failure Conditions (Revert Triggers)
- ❌ Performance >4.33s (>3% regression)
- ❌ Test failures
- ❌ Memory bloat (sizeof(TValue) > 16 bytes)
- ❌ Sanitizer errors
- ❌ C API breakage

---

## Conclusion

### Recommended Approach

**Implement Phase 1 (Type-Safe Wrapper) ONLY**

1. ✅ Add variant-like API to existing TValue
2. ✅ Keep union for zero-cost performance
3. ✅ Migrate cold paths to type-safe API incrementally
4. ✅ Achieve type safety goals WITHOUT performance risk

**Do NOT pursue**:
- ❌ Pure std::variant (Option 1) - violates performance constraints
- ⚠️ Hybrid std::variant (Option 2) - too risky, unpredictable

**Consider later** (after Phase 1 success):
- ⏸️ Phase 2 (Selective std::variant) - ONLY if profiling shows it's safe
- ⏸️ Phase 3 (Full std::variant) - RESEARCH ONLY, wait for language improvements

### Expected Outcomes

**After Phase 1**:
- Modern C++ API available for new code
- Existing performance maintained (4.20s)
- Type safety improved in cold paths
- Best of both worlds: performance + safety

### Next Steps

1. Get stakeholder approval for Phase 1
2. Create feature branch
3. Implement Phase 1.1 (Visitor support)
4. Benchmark and iterate
5. Only proceed to Phase 1.2 if benchmarks pass

### Final Recommendation

> **Start with Phase 1 (Type-Safe Wrapper). This achieves 80% of the goals with 0% of the performance risk.**
>
> Do NOT attempt full std::variant replacement until compiler technology guarantees zero-cost abstractions for our specific use case.

---

## Appendices

### A. Reference Implementation Snippets

See inline code examples in Architecture Options section.

### B. Benchmarking Commands

```bash
# Standard 5-run benchmark
cd /home/user/lua_cpp/testes
for i in 1 2 3 4 5; do \
  ../build/lua all.lua 2>&1 | grep "total time:"; \
done

# 20-run for statistical significance
for i in {1..20}; do \
  ../build/lua all.lua 2>&1 | grep "total time:"; \
done | awk '{sum+=$3; sumsq+=$3*$3} END {
  mean=sum/NR;
  stddev=sqrt(sumsq/NR - mean*mean);
  printf "Mean: %.2fs, StdDev: %.3fs, Max: %.2fs\n", mean, stddev, mean+2*stddev
}'
```

### C. Related Documents

- `UNION_REMOVAL_ANALYSIS.md` - General union analysis
- `CLAUDE.md` - Project overview and guidelines
- `SRP_ANALYSIS.md` - Single Responsibility Principle refactoring
- `GC_SIMPLIFICATION_ANALYSIS.md` - GC architecture analysis

### D. References

- [P0088R3: Variant](https://wg21.link/p0088r3)
- [P2900R6: Contracts for C++](https://wg21.link/p2900r6)
- Lua 5.5 source code analysis
- Previous performance analysis (UNION_REMOVAL_ANALYSIS.md)

---

**Document Version**: 1.0
**Last Updated**: 2025-11-18
**Author**: Claude (Sonnet 4.5)
**Status**: Ready for Review
