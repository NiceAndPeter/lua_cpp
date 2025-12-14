# Phase 148+: Declare-on-First-Use Improvement Plan

## Overview

Comprehensive plan to improve variable declare-on-first-use throughout the entire codebase, following modern C++ best practices. This document outlines ~140 opportunities identified across all source files.

**Status**: Planning phase
**Estimated Impact**: Zero performance cost, significant readability improvement
**Completed**: Phase 148-Partial (7 functions in 3 files)

---

## Phases Breakdown

### ✅ Phase 148-Partial: Initial Improvements (COMPLETED)

**Files Changed**: 3 files, 7 functions
- `lcode.cpp`: `codeorder()`, `codeeq()` - Moved variables to if-init-statement
- `ldo.cpp`: `genMoveResults()`, `pCall()` - Combined declaration with initialization
- `parser.cpp`: `recfield()`, `funcargs()` - Moved variables closer to first use

**Result**: ~2.31s avg ✅ (45% faster than 4.20s baseline)

---

### 📋 Phase 148-A: VM Hot Path (Priority 1)

**Target**: ~15 functions in VM execution paths
**Estimated Impact**: HIGH - These are the most frequently executed code paths

#### **File: src/vm/lvm_comparison.cpp** (~8 opportunities)

**1. l_strcmp() (lines 36-53)**
```cpp
// CURRENT:
size_t rl1;
auto *s1 = getStringWithLength(ts1, rl1);
size_t rl2;
auto *s2 = getStringWithLength(ts2, rl2);

// SHOULD BE:
size_t rl1;
auto const* s1 = getStringWithLength(ts1, rl1);
size_t rl2;
auto const* s2 = getStringWithLength(ts2, rl2);
```

**2. LTintfloat() (line 79-89)**
```cpp
// CURRENT:
lua_Integer fi;
if (VirtualMachine::flttointeger(f, &fi, F2Imod::F2Iceil))

// SHOULD BE:
if (lua_Integer fi; VirtualMachine::flttointeger(f, &fi, F2Imod::F2Iceil))
```

**3-5. Similar patterns in**: `LEintfloat()` (line 96), `LTfloatint()` (line 113), `LEfloatint()` (line 130)

**6-8. Loop variables in comparison functions**: Move to for-loop initializers

---

#### **File: src/vm/lvm_conversion.cpp** (~5 opportunities)

**1. l_strton() (lines 30-40)**
```cpp
// CURRENT:
TString *st = tsvalue(obj);
size_t stlen;
const char *s = getStringWithLength(st, stlen);

// SHOULD BE:
TString* const st = tsvalue(obj);
size_t stlen;
const char* const s = getStringWithLength(st, stlen);
```

**2-5. Similar patterns**: Add const correctness throughout conversion functions

---

### 📋 Phase 148-B: Core Execution (Priority 1)

**Target**: ~20 functions in core execution paths
**Estimated Impact**: HIGH - Called on every function call, hook, error

#### **File: src/core/ldo.cpp** (~10 opportunities)

**1. rawRunProtected() (lines 214-236)**
```cpp
// CURRENT:
l_uint32 oldnCcalls = getNumberOfCCalls();
lua_longjmp lj;
lj.status = LUA_OK;
lj.previous = getErrorJmp();

// SHOULD BE:
const l_uint32 oldnCcalls = getNumberOfCCalls();
lua_longjmp lj{LUA_OK, getErrorJmp()};
```

**2. callHook() (lines 337-366)**
```cpp
// CURRENT:
lua_Hook hook_func = getHook();
if (hook_func && getAllowHook()) {
  CallInfo *ci_local = ci;

// SHOULD BE:
if (lua_Hook hook_func = getHook(); hook_func && getAllowHook()) {
  CallInfo* const ci_local = ci;
```

**3. retHook() (lines 394-411)** - Add const correctness to firstres, delta

**4. tryFuncTM() (lines 424-438)** - Move metamethod and p closer to use

**5. genMoveResults() (lines 443-456)** - Split into two separate loops with own counters

**6-10. Additional functions**: moveresults, adjust_varargs, correctstack, etc.

---

#### **File: src/core/lapi.cpp** (~10 opportunities)

**1. lua_checkstack() (lines 49-63)**
```cpp
// CURRENT:
int res;
CallInfo *ci;
lua_lock(L);
ci = L->getCI();
// ... later
if (L->getStackLast().p - L->getTop().p > n)
  res = 1;
else
  res = L->growStack(n, 0);

// SHOULD BE:
lua_lock(L);
CallInfo* const ci = L->getCI();
// ... later
const int res = (L->getStackLast().p - L->getTop().p > n)
  ? 1 : L->growStack(n, 0);
```

**2. lua_rotate() (lines 176-187)** - Add const to segmentEnd, segmentStart, prefixEnd

**3. lua_tonumberx() (lines 323-330)** - Add const to isnum

**4-10. Additional API functions**: lua_copy, lua_pushvalue, lua_settable, etc.

---

### 📋 Phase 148-C: Table Operations (Priority 1)

**Target**: ~15 functions in table hot paths
**Estimated Impact**: HIGH - Every table access goes through these

#### **File: src/objects/ltable.cpp** (~15 opportunities)

**1. hashint() (line 267-273)**
```cpp
// CURRENT:
static Node *hashint (const Table& t, lua_Integer i) {
  lua_Unsigned ui = l_castS2U(i);

// SHOULD BE:
static Node *hashint (const Table& t, lua_Integer i) {
  const lua_Unsigned ui = l_castS2U(i);
```

**2. l_hashfloat() (lines 290-302)**
```cpp
// CURRENT:
int i;
n = l_mathop(frexp)(n, &i) * -cast_num(INT_MIN);
lua_Integer ni;
if (!lua_numbertointeger(n, &ni)) {

// SHOULD BE:
int i;
n = l_mathop(frexp)(n, &i) * -cast_num(INT_MIN);
if (lua_Integer ni; !lua_numbertointeger(n, &ni)) {
```

**3. mainpositionTV() (lines 310-345)** - Eliminate intermediate variables, use direct returns
```cpp
// CURRENT:
case LuaT::NUMINT: {
  lua_Integer i = ivalue(key);
  return hashint(t, i);
}

// SHOULD BE:
case LuaT::NUMINT:
  return hashint(t, ivalue(key));
```

**4. getgeneric() (lines 413-428)** - Add const correctness to base, limit, nextIndex

**5. findindex() (lines 472-491)** - Use if-init-statement for i, add const throughout

**6. computesizes() (lines 560-579)** - Add const to elementCount

**7-15. Additional table functions**: numusearray, numusehash, setnodevector, rehash, etc.

---

## Medium Priority Phases

### 📋 Phase 149: Compiler Path (Priority 2)

**Target**: ~40 functions in compiler
**Estimated Impact**: MEDIUM - Compile-time only, not runtime

#### Files to improve:
- `src/compiler/lcode.cpp` (~15 opportunities)
- `src/compiler/parser.cpp` (~15 opportunities)
- `src/compiler/funcstate.cpp` (~10 opportunities)

**Key patterns**:
- Variables declared at function start, used in branches
- Expression temporaries declared too early
- Register allocations that could be const

---

### 📋 Phase 150: GC & Memory (Priority 2)

**Target**: ~10 functions in GC subsystem
**Estimated Impact**: MEDIUM - GC pauses only

#### Files to improve:
- `src/memory/gc/gc_marking.cpp`
- `src/memory/gc/gc_sweep.cpp`
- `src/memory/gc/gc_weak.cpp`

---

## Low Priority Phases

### 📋 Phase 151: Library Functions (Priority 3)

**Target**: ~50 functions across all libraries
**Estimated Impact**: LOW - Rarely executed

#### Files to improve:
- `src/libraries/lbaselib.cpp`
- `src/libraries/lstrlib.cpp`
- `src/libraries/ltablib.cpp`
- `src/libraries/lmathlib.cpp`
- `src/libraries/liolib.cpp`
- `src/libraries/loslib.cpp`
- `src/libraries/ldblib.cpp`
- `src/libraries/lutf8lib.cpp`
- `src/libraries/lcorolib.cpp`

---

## Common Patterns to Fix

### 1. **Early Status/Result Variables**
```cpp
// BEFORE:
int result;
if (condition)
  result = doSomething();
else
  result = doOtherThing();
return result;

// AFTER:
const int result = condition ? doSomething() : doOtherThing();
return result;
```

### 2. **Loop Counters Outside Loops**
```cpp
// BEFORE:
int i;
for (i = 0; i < n; i++)
  process(i);

// AFTER:
for (int i = 0; i < n; i++)
  process(i);
```

### 3. **Variables in If-Conditions (C++17)**
```cpp
// BEFORE:
int value;
if (tryGet(&value)) {
  use(value);
}

// AFTER:
if (int value; tryGet(&value)) {
  use(value);
}
```

### 4. **Missing Const Correctness**
```cpp
// BEFORE:
auto ptr = getData();
process(ptr);

// AFTER:
auto const* ptr = getData();
process(ptr);
```

### 5. **Eliminate Intermediate Variables**
```cpp
// BEFORE:
int x = getValue();
return compute(x);

// AFTER:
return compute(getValue());
```

---

## Benefits Summary

### Code Quality
- ✅ **Reduced variable scope** - Better encapsulation
- ✅ **Clearer data flow** - Initialization visible at declaration
- ✅ **Modern C++ idiom** - C++17/C++23 best practices
- ✅ **Easier maintenance** - Variables near usage context
- ✅ **Const correctness** - Prevents accidental modification

### Performance
- ✅ **Zero runtime cost** - Compiler optimizes equally
- ✅ **Potential improvements** - Better register allocation hints
- ✅ **No regressions** - Proven in Phase 148-Partial

### Safety
- ✅ **Reduced uninitialized variables** - Declaration with initialization
- ✅ **Clearer lifetime** - RAII destructors run closer to construction
- ✅ **Exception safety** - Smaller scope = fewer cleanup issues

---

## Execution Guidelines

### Before Each Phase
1. Read all functions to be modified
2. Identify the pattern (see "Common Patterns" above)
3. Plan the improvement strategy

### During Each Phase
1. Use Edit tool for EACH change (never batch)
2. Apply one pattern type at a time
3. Keep changes focused and minimal
4. Update 5-10 functions per commit

### After Each Phase
1. Build and run full test suite
2. Run 5-iteration performance benchmark
3. Verify no regressions (target ≤4.33s, current ~2.31s)
4. Commit with descriptive message
5. Update this plan document

### Rollback Criteria
- Tests fail
- Performance regression >3% (≥2.38s)
- Compilation errors
- Sanitizer warnings

---

## Progress Tracking

### ✅ Completed
- Phase 148-Partial: 7 functions (lcode.cpp, ldo.cpp, parser.cpp)

### 🎯 Next Up
- Phase 148-A: VM Hot Path (~15 functions)
  - lvm_comparison.cpp
  - lvm_conversion.cpp

### 📋 Pending
- Phase 148-B: Core Execution (~20 functions)
- Phase 148-C: Table Operations (~15 functions)
- Phase 149: Compiler Path (~40 functions)
- Phase 150: GC & Memory (~10 functions)
- Phase 151: Library Functions (~50 functions)

---

## Estimated Timeline

**Phase 148-A**: ~15 changes, 1 session
**Phase 148-B**: ~20 changes, 1 session
**Phase 148-C**: ~15 changes, 1 session
**Phase 149**: ~40 changes, 2 sessions
**Phase 150**: ~10 changes, 1 session
**Phase 151**: ~50 changes, 2 sessions (low priority)

**Total**: ~8 sessions to complete all high/medium priority work

---

## Success Metrics

- ✅ All tests pass
- ✅ Performance maintained (≤4.33s, target ~2.31s)
- ✅ Zero compiler warnings
- ✅ Sanitizers clean
- ✅ Code review: Improved clarity
- ✅ Grep for anti-patterns: Significantly reduced

---

## Related Documentation

- [CLAUDE.md](../CLAUDE.md) - Main project documentation
- [CPP_MODERNIZATION_ANALYSIS.md](CPP_MODERNIZATION_ANALYSIS.md) - Overall modernization strategy
- [TYPE_MODERNIZATION_ANALYSIS.md](TYPE_MODERNIZATION_ANALYSIS.md) - Type system improvements

---

**Last Updated**: 2025-12-11
**Status**: Planning phase - Ready for Phase 148-A execution
**Maintainer**: Claude Code
