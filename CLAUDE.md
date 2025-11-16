# Lua C++ Conversion Project - AI Assistant Guide

## Project Overview

Converting Lua 5.5 from C to modern C++23 with:
- Zero performance regression (strict requirement)
- C API compatibility preserved
- CRTP for static polymorphism
- Full encapsulation with private fields

**Repository**: `/home/user/lua_cpp`
**Performance**: Target ≤4.24s (≤1% regression from 4.20s baseline on current machine)
**Note**: Historical baseline was 2.17s on different hardware - current numbers are from new machine
**Status**: **CONSTRUCTOR INITIALIZATION COMPLETE** ✅ CallInfo and lua_State now use proper initialization!

---

## Current Status

### Completed ✅

**MAJOR MILESTONE: Full Encapsulation Achieved!**

- **19/19 structs → classes** (100%): Table, TString, Proto, UpVal, CClosure, LClosure, Udata, lua_State, global_State, CallInfo, GCObject, TValue, FuncState, LexState, expdesc, LocVar, AbsLineInfo, Upvaldesc, stringtable
- **19/19 classes fully encapsulated** (100%) with private fields ✅
- **~500 macros converted** to inline functions/methods (37% of total convertible)
- **CRTP inheritance active** - GCBase<Derived> for all GC objects
- **CommonHeader eliminated** - Pure C++ inheritance
- **C++ exceptions** - Replaced setjmp/longjmp
- **Modern CMake** - Build system with sanitizers, LTO support
- **Organized source tree** - 11 logical subdirectories
- **Zero warnings** - Compiles with -Werror
- **Comprehensive testing** - 30+ test files in testes/
- **Recent work** - Constructor initialization Phases 1-2 completed (CallInfo + lua_State)

### Recent Major Achievements

**Constructor Initialization (Phases 1-2)** - Completed Nov 16, 2025:

- **Phase 1 - CallInfo Constructor** ✅
  - Fixed CRITICAL BUG: 5/9 fields were uninitialized (undefined behavior)
  - Added CallInfo() noexcept constructor initializing all fields
  - Updated luaE_extendCI to use placement new
  - Performance: 4.20s avg (new baseline on current machine)
  - Zero warnings, all tests passing

- **Phase 2 - lua_State init() Method** ✅
  - Added init(global_State*) method to consolidate initialization
  - Replaces fragmented initialization across multiple functions
  - Initializes all 27+ fields in single location
  - Uses placement new for base_ci CallInfo initialization
  - Performance: 4.20s avg (no regression)
  - Simplified preinit_thread() implementation

**Single Responsibility Principle (SRP) Refactoring** - Completed Nov 15, 2025 (historical baseline 2.17s):

- **Phase 90 - FuncState SRP** (16 fields → 5 subsystems)
  - CodeBuffer, ConstantPool, VariableScope, RegisterAllocator, UpvalueTracker
  - Performance: 2.04s avg (baseline 2.17s) - 6% faster!
  - Net: +238 insertions, -84 deletions

- **Phase 91 - global_State SRP** (46+ fields → 7 subsystems)
  - MemoryAllocator, GCAccounting, GCParameters, GCObjectLists, StringCache, TypeSystem, RuntimeServices
  - Performance: 2.18s avg (baseline 2.17s) - essentially identical!
  - Net: +409 insertions, -181 deletions

- **Phase 92 - Proto SRP** (19 fields → 2 logical groups)
  - Runtime data + ProtoDebugInfo subsystem
  - Performance: 2.01s avg (baseline 2.17s) - 8% faster!
  - Net: +149 insertions, -85 deletions

**Total Impact**: Dramatically improved code organization, better separation of concerns, **zero performance regression** (actually faster!)

---

## Performance Requirements

### Critical Constraint

**ZERO regression tolerance** - Strict performance enforcement:
- **Current Machine**: Target ≤4.24s (≤1% from baseline 4.20s)
- **Historical**: Previous baseline was 2.17s on different hardware
- Must benchmark after EVERY significant change
- Revert immediately if regression detected

### Benchmark Command

```bash
cd /home/user/lua_cpp
cmake --build build

# 5-run benchmark
cd testes
for i in 1 2 3 4 5; do ../build/lua all.lua 2>&1 | grep "total time:"; done

# Current baseline (Nov 16, 2025): 4.20s avg (4.07-4.32s range)
# Historical baseline (different hardware): 2.17s avg
```

---

## Architecture Decisions

### 1. CRTP (Curiously Recurring Template Pattern) - ACTIVE ✅

Static polymorphism without vtable overhead:

```cpp
template<typename Derived>
class GCBase {
public:
    GCObject* next;
    lu_byte tt;
    lu_byte marked;

    bool isWhite() const noexcept { return testbits(marked, WHITEBITS); }
    bool isBlack() const noexcept { return testbit(marked, BLACKBIT); }
    lu_byte getAge() const noexcept { return getbits(marked, AGEBITS); }
};

class Table : public GCBase<Table> { /* ... */ };
class TString : public GCBase<TString> { /* ... */ };
```

All 9 GC-managed classes inherit from GCBase<Derived>.

### 2. Full Encapsulation Pattern

All classes now have private fields with comprehensive accessor suites:

```cpp
// Pure C++ - no conditional compilation
class Table : public GCBase<Table> {
private:
    lu_byte flags;
    unsigned int asize;
    Value *array;
    Node *node;
    Table *metatable;
    GCObject *gclist;

public:
    // Inline accessors
    inline unsigned int arraySize() const noexcept { return asize; }
    inline Value* getArray() noexcept { return array; }

    // Methods
    lu_byte get(const TValue* key, TValue* res);
    void set(lua_State* L, const TValue* key, TValue* value);
};
```

### 3. Exception Handling

Modern C++ exceptions replaced setjmp/longjmp:

```cpp
class LuaException : public std::exception {
    int status_;
public:
    explicit LuaException(int status) : status_(status) {}
    int getStatus() const { return status_; }
};
```

### 4. Zero-Cost Forwarding

Methods forward to existing C functions for compatibility:

```cpp
lu_byte Table::get(const TValue* key, TValue* res) {
    return luaH_get(this, key, res);
}

// C function wrapper for API compatibility
inline lu_byte luaH_get(Table *t, const TValue *key, TValue *res) {
    return t->get(key, res);
}
```

---

## Codebase Structure

### Directory Organization

```
src/
├── auxiliary/     - Auxiliary library (lauxlib)
├── compiler/      - Parser, lexer, code generator (lparser, llex, lcode)
├── core/          - VM core (lapi, ldo, ldebug, lstate, ltm)
├── interpreter/   - Interactive interpreter (lua.cpp)
├── libraries/     - Standard libraries (base, string, table, math, io, os, etc.)
├── memory/        - GC and memory management (lgc, lmem, llimits)
├── objects/       - Core data types (Table, TString, Proto, UpVal, lobject)
├── serialization/ - Bytecode dump/undump (lundump, ldump, lzio)
├── testing/       - Test infrastructure (ltests)
└── vm/            - Bytecode interpreter (lvm, lopcodes)
```

**Code Metrics:**
- 60 source files (30 headers + 30 implementations)
- ~35,124 total lines of code
- 11 logical subdirectories

### Module Organization

| Module | Prefix | Primary Classes | Status |
|--------|--------|----------------|--------|
| Table | luaH_ | Table | ✅ Fully encapsulated |
| String | luaS_ | TString | ✅ Fully encapsulated |
| Object | luaO_ | TValue, GCObject | ✅ Fully encapsulated |
| Func | luaF_ | Proto, UpVal, Closures | ✅ Fully encapsulated |
| Do | luaD_ | CallInfo | ✅ Fully encapsulated |
| State | luaE_ | lua_State, global_State | ✅ Fully encapsulated |
| GC | luaC_ | GCObject | ✅ Fully encapsulated |
| Compiler | luaK_ | FuncState | ✅ Fully encapsulated |
| Lexer | luaX_ | LexState | ✅ Fully encapsulated |

---

## Testing & Validation

### Test Suite

**Location**: `/home/user/lua_cpp/testes/`
**Files**: 30+ comprehensive test files
- `all.lua` - Main test runner
- `api.lua` - C API tests
- `gc.lua`, `gengc.lua` - Garbage collection
- `calls.lua`, `closure.lua` - Function/closure tests
- `coroutine.lua` - Coroutine tests
- `errors.lua` - Error handling
- `strings.lua`, `math.lua` - Standard library tests
- And many more...

**Expected output**: `final OK !!!`

### Build Commands

```bash
# Initial CMake configuration
cd /home/user/lua_cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Full rebuild
cmake --build build --clean-first

# Run tests
cd testes
../build/lua all.lua

# CTest integration
cd build && ctest --output-on-failure
```

### Performance Validation

```bash
cd /home/user/lua_cpp/testes

# 5-run benchmark
for i in 1 2 3 4 5; do \
    ../build/lua all.lua 2>&1 | grep "total time:"; \
done

# Target: ≤2.21s (≤1% regression from 2.17s baseline)
```

---

## Code Style & Conventions

### Naming

- **Classes**: PascalCase (Table, TString, FuncState)
- **Methods**: camelCase (get, arraySize, getGlobalState)
- **Members**: snake_case (asize, lsizenode, nuvalue)
- **Constants**: UPPER_SNAKE_CASE (LUA_TNIL, WHITEBITS)

### Const-Correctness

```cpp
// Read-only
inline bool isDummy() const noexcept { return ...; }
lu_byte get(const TValue* key, TValue* res) const;

// Mutating
void set(lua_State* L, const TValue* key, TValue* value);
void resize(lua_State* L, unsigned nasize, unsigned nhsize);
```

### Inline Strategy

- Field accessors: inline
- Simple computations: inline constexpr
- Forwarding functions: inline
- Complex logic: separate .cpp implementation

### Hot-Path Performance

For VM-critical code (lvm.cpp, ldo.cpp), use reference accessors:

```cpp
// Hot path - avoid copies
StkIdRel& topRef() noexcept { return top; }
CallInfo*& ciRef() noexcept { return ci; }

// Instead of
StkIdRel getTop() const noexcept { return top; }  // Copy - slower!
```

---

## Important Files

### Core Headers

- `include/lua.h` - Public C API (C-compatible)
- `src/objects/lobject.h` - Core type definitions
- `src/objects/ltvalue.h` - TValue class
- `src/core/lstate.h` - VM state (lua_State, global_State)
- `src/memory/lgc.h` - GC with GCBase<T> CRTP
- `src/compiler/lparser.h` - FuncState, parser infrastructure
- `src/compiler/llex.h` - LexState, lexer infrastructure

### Implementation Files

- `src/objects/ltable.cpp` - Table methods
- `src/objects/lstring.cpp` - TString methods
- `src/objects/lfunc.cpp` - Proto, UpVal, Closure methods
- `src/memory/lgc.cpp` - GC implementation
- `src/vm/lvm.cpp` - VM bytecode interpreter (HOT PATH)
- `src/core/ldo.cpp` - lua_State methods (call/return/error handling)
- `src/compiler/lcode.cpp` - Code generation (FuncState methods)
- `src/compiler/lparser.cpp` - Parser (uses FuncState, LexState)
- `src/compiler/llex.cpp` - Lexer (LexState methods)

### Build Files

- `CMakeLists.txt` - Modern CMake configuration
- `build/` - Out-of-tree build directory
- `cmake/` - CMake modules

---

## Build System

### CMake Configuration

**Features:**
- ✅ C++23 standard
- ✅ Zero warnings with `-Werror -Wfatal-errors`
- ✅ Comprehensive warning flags
- ✅ Optimization: -O3, -fno-stack-protector
- ✅ Optional sanitizers (ASAN, UBSAN)
- ✅ Optional Link Time Optimization (LTO)
- ✅ Test mode with ltests.h integration

**Build Options:**

```bash
# Standard release build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# With sanitizers (for debugging)
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
    -DLUA_ENABLE_ASAN=ON \
    -DLUA_ENABLE_UBSAN=ON
cmake --build build

# With LTO (maximum optimization)
cmake -B build -DCMAKE_BUILD_TYPE=Release \
    -DLUA_ENABLE_LTO=ON
cmake --build build
```

**Available Options:**
- `LUA_BUILD_TESTS=ON` (default) - Enables test infrastructure
- `LUA_ENABLE_ASSERTIONS=ON` (default)
- `LUA_ENABLE_ASAN=OFF` - AddressSanitizer
- `LUA_ENABLE_UBSAN=OFF` - UndefinedBehaviorSanitizer
- `LUA_ENABLE_LTO=OFF` - Link Time Optimization
- `LUA_BUILD_SHARED=OFF` - Build shared library

---

## Common Patterns

### Pattern 1: Struct → Class Conversion

```cpp
class StructName : public GCBase<StructName> {
private:
    // All fields private
    type field1;
    type field2;

public:
    // Inline accessors
    inline type getField1() const noexcept { return field1; }
    inline void setField1(type val) noexcept { field1 = val; }

    // Reference accessors for hot paths
    inline type& field1Ref() noexcept { return field1; }

    // Pointer accessors for GC/external manipulation
    inline type* getField1Ptr() noexcept { return &field1; }

    // Methods
    void methodName(params);
};
```

### Pattern 2: Inline Constexpr Replacement

```cpp
// Before
#define ttisnil(v)  (ttype(v) == LUA_TNIL)

// After
inline constexpr bool ttisnil(const TValue* v) noexcept {
    return ttype(v) == LUA_TNIL;
}
```

### Pattern 3: C Function → Method Conversion

```cpp
// Before: Free function
void luaK_codeABC(FuncState *fs, OpCode o, int a, int b, int c);

// After: Method on FuncState
class FuncState {
public:
    void codeABC(OpCode o, int a, int b, int c);
};

// Wrapper for C API compatibility
inline void luaK_codeABC(FuncState *fs, OpCode o, int a, int b, int c) {
    fs->codeABC(o, a, b, c);
}
```

---

## Key Learnings

1. **Inline functions are zero-cost** - No measurable overhead vs macros
2. **C++ can be faster** - Potential 3% improvement with optimizations
3. **CRTP is zero-cost** - Static dispatch without vtables
4. **Encapsulation doesn't hurt performance** - Same compiled code with good accessors
5. **Exceptions are efficient** - Faster than setjmp/longjmp
6. **Incremental conversion works** - Small phases with frequent testing
7. **Reference accessors critical for hot paths** - Avoid copies in VM interpreter
8. **Comprehensive testing essential** - 30+ test files catch regressions

---

## Encapsulation Achievement ✅

### All 19 Classes Fully Encapsulated (100%)

**Parser/Compiler Classes:**
1. ✅ **FuncState** - All 16 fields private (lparser.h:256-475)
2. ✅ **LexState** - All 11 fields private (llex.h:68-164)
3. ✅ **expdesc** - All fields private

**VM Core Classes:**
4. ✅ **lua_State** - All 27 fields private (lstate.h:374-604)
   - 100+ accessor methods
   - Reference accessors for hot-path performance
   - Pointer accessors for external manipulation

5. ✅ **global_State** - All 46+ fields private (lstate.h:644-872)
   - Extensive accessors for GC lists, parameters, state
   - Pointer accessors for efficient GC manipulation

6. ✅ **CallInfo** - All fields private

**Object Classes:**
7. ✅ **Table** - All fields private
8. ✅ **TString** - All fields private
9. ✅ **Proto** - All fields private
10. ✅ **UpVal** - All fields private
11. ✅ **CClosure** - All fields private
12. ✅ **LClosure** - All fields private
13. ✅ **Udata** - All 5 fields private (lobject.h:672-726)

**Base Classes:**
14. ✅ **GCObject** - Protected fields (base class)
15. ✅ **TValue** - Fully encapsulated

**Helper Classes:**
16. ✅ **LocVar** - All fields private
17. ✅ **AbsLineInfo** - All fields private
18. ✅ **Upvaldesc** - All fields private
19. ✅ **stringtable** - All fields private

---

## Macro Conversion Status

### Completed (~500 macros converted)

**Categories converted:**
- Type checks (ttisnil, ttisstring, etc.)
- Type tests (ttype, ttisnumber, etc.)
- Field accessors (many converted to methods)
- Simple expressions
- Character type checks (some)

### Remaining (~75 convertible macros)

**Identified batches:**
- **Batch 1**: 10 simple expression macros (lcode.h)
- **Batch 2**: 25 instruction manipulation macros (lopcodes.h)
- **Batch 3**: 15 type method macros (ltm.h)
- **Batch 4**: 10 character type macros (lctype.h)
- **Batch 5**: 15 remaining simple macros

**Estimated time**: 8-10 hours total

### Keep as Macros (Do NOT Convert)

**Token-Pasting Macros**:
```cpp
// MUST remain macro - uses token pasting (##)
#define setgcparam(g,p,v)  (g->gc##p = (v))
```

**Public API Macros** (C compatibility):
```cpp
// MUST remain macro - part of public C API
#define lua_call(L,n,r)  lua_callk(L, (n), (r), 0, NULL)
```

**Configuration Macros**:
```cpp
// Keep as macro - compile-time configuration
#define LUAI_MAXSHORTLEN 40
```

---

## Development Workflow

### Git Branch Strategy

Development occurs on `claude/*` branches:
- Current branch pattern: `claude/claude-md-<session-id>`
- Feature branches: `claude/fix-<description>-<id>`
- Always push to `-u origin <branch-name>`

### Making Changes

1. **Edit code** using Edit/Read/Write tools (NO scripts)
2. **Build** after every change
3. **Test** after every change
4. **Benchmark** after significant changes
5. **Commit** immediately after successful phase
6. **Revert** if performance regression detected

### Commit Convention

```bash
git add <files>
git commit -m "Phase N: Description of changes"

# Example:
git commit -m "Phase 87: Convert label and goto management utilities to methods"
```

### Testing Workflow

```bash
# 1. Build
cmake --build build

# 2. Quick test
cd testes && ../build/lua all.lua

# 3. If significant change, benchmark
for i in 1 2 3 4 5; do \
    ../build/lua all.lua 2>&1 | grep "total time:"; \
done

# 4. If all passes, commit
git add . && git commit -m "Phase N: Description"
```

---

## Process Rules (CRITICAL)

### Never Violate These Rules

1. **ASK before benchmarks** - Never run without permission (if user has requested this)
2. **NO automation scripts** - Use Edit/Read/Write tools only
3. **Manual editing** - No Python/shell scripts for code changes
4. **Incremental changes** - Test and benchmark after every phase
5. **Immediate revert** - If performance > 2.21s
6. **Commit after every phase** - Clean history for easy rollback

### Architecture Rules

1. **C compatibility ONLY for public API** (lua.h, lauxlib.h, lualib.h)
2. **Internal code is pure C++** - No `#ifdef __cplusplus`
3. **Performance target**: ≤2.21s (strict)
4. **Zero C API breakage** - Public interface unchanged
5. **All fields private** - Use accessors (already achieved!)

---

## Macro Conversion Guidelines

### Convertible Macros (Convert These)

**Simple Expressions** - High Priority:
```cpp
// Before
#define lmod(s,size) (check_exp((size&(size-1))==0, (cast_uint(s) & cast_uint((size)-1))))

// After
inline constexpr unsigned int lmod(int s, int size) noexcept {
    return (size & (size-1)) == 0 ? (cast_uint(s) & cast_uint(size-1)) : 0;
}
```

**Type Checks** - Medium Priority:
```cpp
// Before
#define isreserved(s) ((s)->tt == LUA_VSHRSTR && (s)->extra > 0)

// After
inline bool isreserved(const TString* s) noexcept {
    return s->tt == LUA_VSHRSTR && s->extra > 0;
}
```

**Instruction Manipulation** - High Priority (VM critical):
```cpp
// Before
#define GETARG_A(i) getarg(i, POS_A, SIZE_A)

// After
inline constexpr int GETARG_A(Instruction i) noexcept {
    return getarg(i, POS_A, SIZE_A);
}
```

### Conversion Strategy

1. **Identify candidates** - Use Grep to find macro definitions
2. **Batch by header** - Convert 10-20 macros at a time
3. **Preserve semantics** - Ensure exact same behavior
4. **Use constexpr** - For compile-time computation
5. **Add noexcept** - For exception safety
6. **Benchmark** - After every batch
7. **Revert if regression** - Performance > 2.21s

---

## Analysis Findings

### Project Assessment: EXCELLENT ✅

- **Architecture**: Well-designed CRTP pattern with zero-cost abstraction
- **Performance**: Meets or exceeds baseline (target ≤2.21s)
- **Code Quality**: Zero warnings, 915+ noexcept specifications, modern C++23
- **Documentation**: Comprehensive plans and guides
- **Technical Debt**: LOW - minimal TODOs, clean code
- **Encapsulation**: **100% COMPLETE** ✅

### Strengths

1. ✅ **Zero-cost modernization** - Performance maintained or improved
2. ✅ **Type safety** - enum classes, inline constexpr, template functions
3. ✅ **Strong discipline** - 1% regression tolerance enforced
4. ✅ **Comprehensive testing** - 30+ test files
5. ✅ **Modern build system** - CMake with sanitizers, LTO, CTest
6. ✅ **Full encapsulation** - All 19 classes with private fields
7. ✅ **Active development** - Recent phases 80-87 completed

### Remaining Opportunities

1. ⚠️ **Macro conversion** - ~75 convertible macros identified (~37% converted)
2. ⚠️ **CI/CD** - No automated testing infrastructure detected
3. ⚠️ **Test coverage metrics** - No gcov/lcov integration
4. ⚠️ **Performance benchmarking** - Could add automated regression detection

### Future Work & Architectural Opportunities

**📋 Single Responsibility Principle Analysis** - See `SRP_ANALYSIS.md`

**Analysis Date**: 2025-11-15
**Status**: Analysis complete, implementation planning phase

**Priority Classes for Refactoring**:
1. ⭐⭐⭐ **FuncState** (16 fields → 5 subsystems) - LOW RISK, compile-time only
   - Recommended starting point for SRP refactoring
   - CodeBuffer, ConstantPool, VariableScope, RegisterAllocator, UpvalueTracker
   - Estimated: 30-40 hours, no performance risk

2. ⭐⭐ **global_State** (46+ fields → 7 components) - MEDIUM RISK, high value
   - MemoryAllocator, GCAccounting, GCParameters, GCObjectLists, StringCache, TypeSystem, RuntimeServices
   - Estimated: 40-60 hours, requires careful benchmarking

3. ⭐ **Proto** (19 fields) - LOW RISK, medium value
   - Potential separation of debug info from runtime data
   - Estimated: 20-30 hours

4. ⚠️ **lua_State** (27 fields) - VERY HIGH RISK, defer
   - VM hot path - only consider after proving benefits with other classes
   - Estimated: 60-80 hours, significant performance risk

**Critical Constraints**:
- ✅ Must maintain ≤2.21s performance (≤1% regression)
- ✅ Must preserve C API compatibility
- ✅ All refactoring must be zero-cost abstractions (inline accessors)
- ✅ Benchmark after every significant change

**See `SRP_ANALYSIS.md` for detailed analysis, decomposition proposals, and implementation roadmap.**

---

## Success Metrics

- ✅ 19 structs → classes (100%)
- ✅ **19/19 classes fully encapsulated (100%)** ✅
- ✅ **3/3 major SRP refactorings complete (100%)** ✅
  - FuncState (16 fields → 5 subsystems)
  - global_State (46+ fields → 7 subsystems)
  - Proto (19 fields → 2 logical groups)
- ⏳ ~500 macros converted (~37%) - Remaining are VM-critical or config
- ✅ CRTP active - All 9 GC types
- ✅ Exceptions implemented
- ✅ CMake build system
- ✅ Zero warnings (-Werror)
- ✅ Performance: **2.08s avg** (4% FASTER than 2.17s baseline!)
- ✅ All tests passing (30+ test files)
- ✅ Zero C API breakage
- ✅ Phases 80-92 completed - Method conversion + SRP refactoring

**Status**: **SRP REFACTORING COMPLETE** ✅ - All major architectural improvements achieved!
**Result**: Dramatically better code organization + faster performance!

---

## Quick Reference

```bash
# Repository location
cd /home/user/lua_cpp

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Test
cd testes && ../build/lua all.lua
# Expected: "final OK !!!"

# Benchmark (5 runs)
cd testes
for i in 1 2 3 4 5; do \
    ../build/lua all.lua 2>&1 | grep "total time:"; \
done
# Target: ≤2.21s

# Git workflow
git status
git log --oneline -10
git add <files> && git commit -m "Phase N: Description"
git push -u origin <branch-name>
```

---

## File Organization Summary

**Total**: ~35,124 lines of code across 60 files

**Headers** (~7K lines): 30 header files
**Implementations** (~28K lines): 30 .cpp files
**Tests**: 30+ .lua test files
**Build**: CMake with modern C++23

---

## Additional Resources

### Architectural Planning
- **SRP_ANALYSIS.md** - Single Responsibility Principle analysis & refactoring roadmap (2025-11-15)
- **ENCAPSULATION_PLAN.md** - Detailed phase-by-phase encapsulation plan (may be outdated)
- **CONSTRUCTOR_PLAN.md** - Constructor implementation strategy
- **PHASE_36_2_PLAN.md** - Specific phase documentation

### Build & Development
- **CMAKE_BUILD.md** - CMake build instructions

---

**Last Updated**: 2025-11-15 - After completing SRP refactoring (Phases 90-92)
**Current Phase**: SRP refactoring complete! All major classes decomposed.
**Performance Status**: ✅ EXCELLENT - Avg 2.08s across all refactorings (4% faster than baseline!)
**Achievements**: FuncState, global_State, and Proto successfully refactored with zero performance regression
