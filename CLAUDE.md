# Lua C++ Conversion Project - AI Assistant Guide

## Project Overview

Converting Lua 5.5 from C to modern C++23:
- **Zero performance regression** (strict requirement)
- **C API compatibility** preserved
- **CRTP** for static polymorphism
- **Full encapsulation** with private fields

**Performance**: ~2.12s avg ✅ (50% faster than 4.20s baseline, target ≤4.33s)
**Status**: Phase 130 Part 5 complete - ConsControl* & BlockCnt* → References done!
**Completed**: Phases 1-127, 129-1, 130-1/2/3/4/5 | **Quality**: 96.1% coverage, zero warnings

---

## Completed Milestones ✅

**Core Architecture** (100%):
- 19/19 structs → classes | CRTP inheritance | C++ exceptions | Modern CMake

**Code Modernization** (99.9%):
- ~520 macros → inline functions | Modern casts | enum class | nullptr
- std::array | [[nodiscard]] (55+ functions, found 5 bugs!) | bool returns
- Const correctness | Pointer-to-reference conversions

**Architecture**:
- VirtualMachine class (all wrappers eliminated) | Header modularization (-79% lobject.h)
- LuaStack centralization | GC modularization (6 modules) | SRP refactoring

---

## Recent Phase History

**Phases 115-124** (Earlier modernization):
- **115-116**: std::span adoption, UB fixes
- **117-118**: Bool predicates, [[nodiscard]] (15+ annotations, found 1 bug)
- **119**: std::array conversion (4 arrays, 5.5% perf improvement)
- **121**: Header modularization (lobject.h -79%, 6 focused headers)
- **122**: VirtualMachine class creation (21 methods, ~2.26s avg)
- **123**: Final macro conversions (20+ macros → functions/templates, 99.9% complete)

**Phase 125**: luaV_* Wrapper Elimination ✅
- Converted 90+ call sites from luaV_* wrappers to direct VirtualMachine methods
- Deleted 3 wrapper files, removed 277 lines
- **Result**: ~2.16s avg, cleaner architecture, -0.8% code size

**Phase 126**: Const Correctness ✅
- Added `const` to 5 getter methods (Table, Udata, CClosure, LClosure)
- Made `Table::powerOfTwo()` constexpr
- **Result**: ~2.15s avg (maintained)

**Phase 127**: [[nodiscard]] Expansion ✅
- Added to ~40 critical functions (stack ops, GC, factory methods, strings)
- **Found & fixed 4 real bugs** (closepaux, resetthread, OP_CLOSE, return handler)
- **Result**: ~2.15s avg (maintained)

**Phase 129**: Range-Based For Loops (Part 1) ✅
- Converted 4 traditional loops in lundump.cpp to C++23 range-based for
- Fixed scoping bug in upvalues string loading
- **Result**: ~2.20s avg (maintained)

**Phase 130**: Pointer-to-Reference Conversions ✅
- **Part 1**: expdesc* → expdesc& (~80 params, ~200+ call sites in parser/funcstate/lcode)
- **Part 2**: Table*/Proto* → References (~45 helpers in ltable/lundump/ldump, ~100+ call sites)
- **Part 3**: global_State* → global_State& (~42 GC functions in 4 modules: marking, sweeping, weak, finalizer)
- **Part 4**: TString* → TString& (~15 compiler functions: registerlocalvar, searchupvalue, newupvalue, searchvar, singlevaraux, stringK, new_localvar, buildglobal, buildvar, fornum, forlist, labelstat, checkrepeated, newgotoentry; updated 4x eqstr helpers)
- **Part 5**: ConsControl* & BlockCnt* → References (8 compiler infrastructure functions: solvegotos, enterblock, open_func, closelistfield, lastlistfield, recfield, listfield, field; 10 call sites)
- **Benefits**: Type safety (no null), modern C++23 idiom, clearer semantics
- **Result**: ~2.12s avg (50% faster than baseline!)
- See `docs/PHASE_130_POINTER_TO_REFERENCE.md`

---

## Performance

**Baseline**: 4.20s (Nov 2025) | **Target**: ≤4.33s (3% tolerance)
**Current**: ~2.12s avg ✅ **50% faster than baseline!**

```bash
# Benchmark (5 runs)
cd /home/user/lua_cpp/testes
for i in 1 2 3 4 5; do ../build/lua all.lua 2>&1 | grep "total time:"; done
```

---

## Architecture

**CRTP Polymorphism**: `GCBase<Derived>` for zero-cost static dispatch (9 GC types)
**Encapsulation**: All fields private, comprehensive accessors with inline/constexpr
**Modern C++23**: enum class | [[nodiscard]] | constexpr | std::span | std::array | nullptr

**Structure** (81 files, ~35k lines):
```
src/
├── auxiliary/     - Auxiliary library
├── compiler/      - Parser, lexer, codegen
├── core/          - VM core (lapi, ldo, lstate, ltm)
├── memory/gc/     - 6 focused GC modules
├── objects/       - Table, TString, Proto, etc.
├── vm/            - Bytecode interpreter
└── [5 more dirs]  - libraries, serialization, testing, etc.
```

---

## Build & Test

```bash
# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build

# Test
cd testes && ../build/lua all.lua  # Expected: "final OK !!!"

# Sanitizers
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DLUA_ENABLE_ASAN=ON -DLUA_ENABLE_UBSAN=ON
```

**Build Options**: LUA_BUILD_TESTS | LUA_ENABLE_ASSERTIONS | LUA_ENABLE_ASAN | LUA_ENABLE_UBSAN | LUA_ENABLE_LTO

---

## Code Style

**Naming**: Classes (PascalCase) | Methods (camelCase) | Members (snake_case) | Constants (UPPER_SNAKE)
**Inline**: Accessors (inline) | Simple compute (inline constexpr) | Complex logic (.cpp)

**Key Files**:
- `include/lua.h` - Public C API
- `src/core/lstate.h` - lua_State, global_State
- `src/objects/lobject.h` - Core types
- `src/memory/lgc.h` - GCBase<T> CRTP
- `src/vm/lvm.cpp` - Bytecode interpreter (HOT PATH)
- `src/compiler/lcode.cpp` - Code generation

---

## Macros: 99.9% Complete ✅

**Converted** (~520): Type checks | Field accessors | Instruction ops | Casts | Memory | GC

**Remaining** (~140 necessary - see `docs/NECESSARY_MACROS.md`):
- Public C API (87) - C compatibility required
- Platform abstraction (41) - POSIX/Windows/ISO C
- Preprocessor (5) - Token pasting, stringification
- Conditional compilation (7) - Debug/HARDMEMTESTS
- VM dispatch (3) - Performance-critical computed goto
- Forward declaration (1) - luaM_error (incomplete type)
- User-customizable (10) - Designed for override
- Test-only (13) - Low priority

---

## Git & Workflow

**Branch**: `claude/*` branches | **Commit**: `git commit -m "Phase N: Description"` | **Push**: `-u origin <branch>`

---

## Process Rules ⚠️

**NEVER**:
1. Batch processing - Use Edit tool for EACH change
2. sed/awk/perl for bulk edits
3. Skip testing/benchmarking after changes
4. Accept >3% performance regression
5. Break public C API (lua.h, lauxlib.h, lualib.h)

**ALWAYS**:
1. Test after every phase
2. Benchmark significant changes
3. Revert if performance degrades >3%
4. Commit frequently (clean history)
5. Keep internal code pure C++ (no `#ifdef __cplusplus`)

---

## Success Metrics ✅

19/19 classes | ~520 macros converted (99.9%) | VirtualMachine complete | GC modularized
All casts modern | All enums type-safe | CRTP active (9 types) | CI/CD with sanitizers
Zero warnings | 96.1% coverage | 30+ tests passing | **50% faster than baseline!**
Phases 1-127, 129-1, 130-1/2/3/4/5 complete

**Result**: Modern C++23 codebase with exceptional performance!

---

## Key Learnings

1. Inline functions = zero-cost (no overhead vs macros)
2. CRTP = zero-cost (static dispatch, no vtables)
3. Encapsulation doesn't hurt perf (same compiled code)
4. std::span has subtle costs (Phase 115: 11.9% regression)
5. std::array can improve perf (Phase 119: 5.5% improvement)
6. Exceptions are efficient (faster than setjmp/longjmp)
7. Incremental conversion works (small phases + frequent testing)
8. Reference accessors critical (avoid copies in hot paths)
9. [[nodiscard]] finds real bugs (caught 5 bugs in Phases 118, 127!)
10. Template functions + lambdas = zero overhead (Phase 123 GC macros)
11. Eliminate wrappers aggressively (Phase 125: better perf + cleaner code)

---

## Future Opportunities

**Potential Next Steps** (see `docs/PHASE_SUGGESTIONS.md` for details):
- Phase 129 Part 2: Range-based for loops in ldebug.cpp (medium risk)
- Phase 128: std::span performance optimization (if needed)
- Additional pointer-to-reference conversions (Phase 130 continuation)
- More const correctness improvements

**Defer (Low Value/High Risk)**:
- Boolean conversions (8 remaining, diminishing returns)
- Loop counter conversion (400 instances, high risk)
- Size variable conversion (underflow risk)
- Register index strong types (very invasive)
- lua_State SRP refactoring (VM hot path risk)

See `docs/TYPE_MODERNIZATION_ANALYSIS.md` and `docs/PHASE_SUGGESTIONS.md`

---

## Documentation

**Primary**: [CLAUDE.md](CLAUDE.md) (this file) | [README.md](README.md) | [CMAKE_BUILD.md](docs/CMAKE_BUILD.md)

**Architecture**: [SRP_ANALYSIS.md](docs/SRP_ANALYSIS.md) | [CPP_MODERNIZATION_ANALYSIS.md](docs/CPP_MODERNIZATION_ANALYSIS.md) | [TYPE_MODERNIZATION_ANALYSIS.md](docs/TYPE_MODERNIZATION_ANALYSIS.md)

**Specialized**: [NECESSARY_MACROS.md](docs/NECESSARY_MACROS.md) | [PHASE_125_LUAV_WRAPPER_ELIMINATION.md](docs/PHASE_125_LUAV_WRAPPER_ELIMINATION.md) | [PHASE_130_POINTER_TO_REFERENCE.md](docs/PHASE_130_POINTER_TO_REFERENCE.md) | [GC_SIMPLIFICATION_ANALYSIS.md](docs/GC_SIMPLIFICATION_ANALYSIS.md) | [SPAN_MODERNIZATION_PLAN.md](docs/SPAN_MODERNIZATION_PLAN.md) | [COVERAGE_ANALYSIS.md](docs/COVERAGE_ANALYSIS.md) | [UNDEFINED_BEHAVIOR_ANALYSIS.md](docs/UNDEFINED_BEHAVIOR_ANALYSIS.md)

**CI/CD**: [.github/workflows/ci.yml](.github/workflows/ci.yml) | [coverage.yml](.github/workflows/coverage.yml) | [static-analysis.yml](.github/workflows/static-analysis.yml)

---

## Quick Reference

```bash
# Build & Test
cd /home/user/lua_cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
cd testes && ../build/lua all.lua  # Expected: "final OK !!!"

# Benchmark (5 runs, target ≤4.33s)
for i in 1 2 3 4 5; do ../build/lua all.lua 2>&1 | grep "total time:"; done

# Git
git status && git log --oneline -10
git add <files> && git commit -m "Phase N: Description" && git push -u origin <branch>
```

---

**Updated**: 2025-12-03 | **Phases**: 1-127, 129-1, 130-1/2/3/4/5 | **Performance**: ~2.12s ✅ (50% faster!)
**Status**: Modern C++23, VirtualMachine complete, const-correct, [[nodiscard]] safety, Phase 130 Part 5 complete (ConsControl* & BlockCnt* → References)!
