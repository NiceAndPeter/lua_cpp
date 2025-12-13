# Lua C++ Conversion Project - AI Assistant Guide

## Project Overview

Converting Lua 5.5 from C to modern C++23:
- **Zero performance regression** (strict requirement)
- **C API compatibility** preserved
- **CRTP** for static polymorphism
- **Full encapsulation** with private fields

**Performance**: ~2.23s avg ✅ (47% faster than 4.20s baseline, target ≤4.33s)
**Status**: Phase 156 COMPLETE - Core API declare-on-first-use improvements (136 total improvements)!
**Completed**: Phases 1-127, 129-1, 130-ALL, 131, 133, 134, 135-Rev, 136-156 | **Quality**: 96.1% coverage, zero warnings

---

## Completed Milestones ✅

**Core Architecture** (100%):
- 19/19 structs → classes | CRTP inheritance | C++ exceptions | Modern CMake

**Code Modernization** (99.9%):
- ~520 macros → inline functions | Modern casts | enum class | nullptr
- std::array | [[nodiscard]] (102+ functions, found 5 bugs!) | bool returns
- Excellent const correctness | Pointer-to-reference conversions

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

**Phase 130**: Pointer-to-Reference Conversions ✅ **ALL 6 PARTS COMPLETE!**
- **Part 1**: expdesc* → expdesc& (~80 params, ~200+ call sites in parser/funcstate/lcode)
- **Part 2**: Table*/Proto* → References (~45 helpers in ltable/lundump/ldump, ~100+ call sites)
- **Part 3**: global_State* → global_State& (~42 GC functions in 4 modules: marking, sweeping, weak, finalizer)
- **Part 4**: TString* → TString& (~15 compiler functions: registerlocalvar, searchupvalue, newupvalue, searchvar, singlevaraux, stringK, new_localvar, buildglobal, buildvar, fornum, forlist, labelstat, checkrepeated, newgotoentry; updated 4x eqstr helpers)
- **Part 5**: ConsControl* & BlockCnt* → References (8 compiler infrastructure functions: solvegotos, enterblock, open_func, closelistfield, lastlistfield, recfield, listfield, field; 10 call sites)
- **Part 6**: Member Variables → References (FuncState: Proto& f, LexState& ls; Parser: LexState& ls; added constructors, updated ~150 call sites)
- **Benefits**: Type safety (no null), modern C++23 idiom, clearer semantics, explicit lifetimes
- **Total**: 210+ functions + 3 member variables converted
- **Result**: ~2.12s avg (50% faster than baseline!)
- See `docs/PHASE_130_POINTER_TO_REFERENCE.md`

**Phase 131**: Identifier Modernization - Quick Wins Batch 1 ✅
- **Part 1A**: StringInterner members (3 variables: `envn` → `environmentName`, `brkn` → `breakLabelName`, `glbn` → `globalKeywordName`)
- **Part 1B**: expdesc final fields (3 fields: `t` → `trueJumpList`, `f` → `falseJumpList`, `ro` → `isReadOnly`)
- **Impact**: Completed last remaining cryptic single/two-letter identifiers in core compiler data structures
- **Files Changed**: 21 files across compiler, core, objects, VM
- **Result**: ~2.09s avg (tests pass, maintained performance)

**Phase 133**: Compiler Expression Variables ✅
- Modernized ~200+ local variables in compiler code generation (lcode.cpp, parser.cpp)
- Patterns: `e1`/`e2` → `leftExpr`/`rightExpr`, `r1`/`r2` → `leftRegister`/`rightRegister`
- Updated ~50 functions including codebinexpval, exp2anyreg, discharge2reg
- **Impact**: Complex code generation logic now self-documenting
- **Result**: ~2.11s avg (compiler-only, no benchmark needed)

**Phase 134**: VM Dispatch Lambda Names ✅
- Renamed 20 VM interpreter lambdas from cryptic abbreviations to descriptive names
- **Register Access** (9): `RA` → `getRegisterA`, `vRA` → `getValueA`, `KB` → `getConstantB`, etc.
- **State Management** (5): `updatetrap` → `updateTrap`, `savepc` → `saveProgramCounter`, etc.
- **Control Flow** (3): `dojump` → `performJump`, `donextjump` → `performNextJump`, etc.
- **Exception Handling** (3): `Protect` → `protectCall`, `ProtectNT` → `protectCallNoTop`, etc.
- **Impact**: ~105+ uses in VM hot path, dramatic clarity improvement (⭐⭐ → ⭐⭐⭐⭐⭐)
- **Files Changed**: src/vm/lvirtualmachine.cpp (1 file, 20 lambdas + ~105 uses)
- **Result**: ~2.11s avg ✅ (zero performance regression in VM hot path!)

**Phase 135**: StackValue Simplification ❌ **REJECTED**
- **Goal**: Convert `union StackValue` to `typedef TValue StackValue` via parallel delta array
- **Attempted**: 2025-12-05, ~2 hours investigation + implementation
- **Why failed**: Parallel array allocation complexity, exception-safety issues, memory tracking bugs
- **Decision**: Union approach is superior (simple, zero overhead, exception-safe)
- **See**: `docs/PHASE_135_FAILED_ATTEMPT.md` for detailed analysis
- **Status**: Archived - Do Not Pursue

**Phase 135-Rev**: Single-Block Allocation for Stack + Deltas ✅
- **Goal**: Improve exception-safety by allocating stack+deltas as single block
- **Implementation**: Added `tbc_deltas` field to LuaStack, single allocation in `realloc()`
- **Impact**: Eliminated split allocation failure modes, cleaner ownership
- **Result**: ~2.11s avg (maintained performance)

**Phase 136**: VM Core Variables - Identifier Modernization ✅
- **Already complete**: VM core variables already modernized in earlier phases
- **Verified**: execute() function locals use clear names (currentClosure, constants, stackFrameBase, etc.)
- **No changes needed**: Variables properly named

**Phase 137**: Metamethod Variable Modernization ✅
- **Pattern**: `tm` → `metamethod` throughout codebase
- **Files Changed**: Multiple files across VM, GC, and core
- **Impact**: Metamethod code now self-documenting
- **Result**: ~2.11s avg (maintained)

**Phase 138**: Additional [[nodiscard]] Annotations ✅
- Added [[nodiscard]] to 6 core functions where ignoring return value is likely a bug
- **Debug functions** (ldebug.h): `luaG_findlocal()`, `luaG_addinfo()`
- **Metamethod functions** (ltm.h): `luaT_objtypename()`, `luaT_gettm()`, `luaT_gettmbyobj()`
- **State management** (lstate.h): `luaE_extendCI()`
- **Decision**: Did NOT add to `luaO_pushfstring/pushvfstring` (legitimately used for side effects)
- **Total**: 96 → 102 [[nodiscard]] annotations
- **Result**: ~2.25s avg ✅ (zero performance regression)

**Phase 139**: Const Correctness Expansion **SKIPPED** ✅
- **Analysis**: Comprehensive survey revealed codebase already has excellent const-correctness
- **Findings**: All getter/query methods already const, all non-const methods genuinely modify state
- **Classes surveyed**: VirtualMachine, Table, Proto, UpVal, Closures, TString
- **Conclusion**: No improvements needed - phase already complete from previous work!
- **Status**: Skipped (work already done)

**Phase 140**: GC Loop Iterator Modernization ✅
- Modernized 8 single-letter loop iterators in GC modules for clarity
- **gc_marking.cpp** (5): `i` → `userValueIndex`/`upvalueIndex`/`typeIndex`/`arrayIndex`
- **gc_weak.cpp** (3): `i` → `arrayIndex`/`nodeIndex`
- **Patterns**: Iterator names now describe what they're indexing
- **Impact**: Self-documenting GC traversal code, clearer iteration context
- **Files Changed**: 2 files, 8 loop variables
- **Result**: ~2.34s avg ✅ (maintained performance)

**Phase 141**: Debug Module Loop Iterators ✅
- Modernized 2 loop iterators in debug module for improved clarity
- **ldebug.cpp** (2): `i` → `instructionIndex` (instruction iteration), `i` → `upvalueIndex` (upvalue search)
- **Impact**: Debug info processing now self-documenting
- **Files Changed**: 1 file, 2 loop variables

**Phase 142**: Table Rehashing Loop Iterators ✅
- Modernized 5 loop iterators in table rehashing algorithm (very high impact)
- **ltable.cpp** (5): `i`/`j` → `arrayKey`/`nodeIndex`/`arrayIndex`
- **Functions**: numusearray, numusehash, setnodevector, reinserthash, reinsertOldSlice
- **Impact**: Complex rehashing algorithm now self-documenting
- **Files Changed**: 1 file, 5 loop variables

**Phase 143**: Core & Compiler Loop Iterators ✅
- Modernized 11 loop iterators across core execution and compiler modules
- **Core modules** (3): ldo.cpp (`argumentIndex`), lstack.cpp (`stackIndex`), lstring.cpp (`userValueIndex`)
- **Compiler modules** (8): parser.cpp (`variableIndex`), lcode.cpp (`instructionIndex`), funcstate.cpp (`upvalueIndex`, `localIndex`)
- **Impact**: Execution, memory, and compilation paths now self-documenting
- **Files Changed**: 5 files, 11 loop variables
- **Result**: ~2.14s avg ✅ (49% faster than baseline!)

**Phase 144**: API & Object Module Loop Iterators ✅
- Modernized 5 additional loop iterators in API and object modules
- **API module** (2): lapi.cpp - `lua_xmove` (`valueIndex`), `lua_pushcclosure` (`upvalueIndex`)
- **Table module** (2): ltable.cpp - `Table::unbound` (`vicinityStep` - vicinity search optimization)
- **Function module** (1): lfunc.cpp - `LClosure::initUpvals` (`upvalueIndex`)
- **Impact**: Public API functions and table operations now self-documenting
- **Files Changed**: 3 files, 5 loop variables
- **Result**: ~2.33s avg ✅ (45% faster than baseline!)

**Phase 145**: VM Instruction Variables - Identifier Modernization ✅
- Modernized 15 cryptic instruction field variables across 14 instruction handlers
- **Patterns**: `b`/`c` → descriptive names based on semantic meaning
- **execute() main loop** (14):
  - OP_LOADI/LOADF: `b` → `immediateValue`
  - OP_LOADNIL: `b` → `nilCount`
  - OP_GETUPVAL: `b` → `upvalueIndex`
  - OP_GETI/SETI: `c`/`b` → `integerKey`
  - OP_NEWTABLE: `b` → `hashSizeLog2`, `c` → `arraySize`
  - OP_CONCAT: `n` → `elementCount`
  - OP_EQI: `im` → `immediateValue`
  - OP_CALL/TAILCALL: `b` → `argumentCount`
  - OP_RETURN/VARARG: `n` → `resultCount`
  - OP_SETLIST: `n` → `elementCount`
- **finishOp()** (1): OP_CONCAT: `a` → `firstElementRegister`
- **Impact**: VM hot path now crystal clear (⭐⭐ → ⭐⭐⭐⭐⭐)
- **Files Changed**: 1 file (lvirtualmachine.cpp), 15 variables
- **Result**: ~2.34s avg ✅ (44% faster than baseline, zero performance impact!)

**Phase 146**: Debug & API Identifier Modernization ✅
- Modernized 11 cryptic variables in debug info generation and API functions
- **ldebug.cpp** (8 variables - debug info for error messages & stack traces):
  - OP_LOADNIL: `b` → `nilCount` (register range size)
  - OP_JMP: `b` → `jumpOffset` (jump destination)
  - OP_MOVE: `b` → `sourceRegister` (move source)
  - isEnv(): `t` → `tableIndex` (table being indexed)
  - OP_GETTABUP/GETTABLE/GETFIELD/SELF: `k` → `keyIndex` (4 occurrences)
- **lapi.cpp** (3 variables - stack rotation algorithm):
  - lua_rotate(): `t` → `segmentEnd`, `p` → `segmentStart`, `m` → `prefixEnd`
- **Impact**: Error messages & debug info generation now self-documenting
- **Files Changed**: 2 files, 11 variables
- **Result**: ~2.32s avg ✅ (45% faster than baseline, slight improvement!)

**Phase 147**: Core Module Loop Iterators ✅
- Modernized 5 critical loop iterators in metamethod and execution core modules
- **ltm.cpp** (3 variables - metamethod & vararg handling):
  - luaT_init(): `i` → `metamethodIndex` (metamethod initialization loop)
  - luaT_adjustvarargs(): `i` → `parameterIndex` (fixed parameter adjustment)
  - luaT_getvarargs(): `i` → `argumentIndex` (vararg argument copying)
- **ldo.cpp** (1 variable - result handling):
  - genMoveResults(): `i` → `resultIndex` (result moving & nil-filling)
- **lstate.cpp** (1 variable - state initialization):
  - lua_newstate(): `i` → `typeIndex` (metatable initialization for all types)
- **Impact**: Metamethod, vararg, and state initialization code now self-documenting
- **Files Changed**: 3 files, 5 loop variables
- **Result**: ~2.31s avg ✅ (45% faster than baseline, continued improvement!)

**Phase 148-A**: VM Hot Path Declare-on-First-Use ✅
- Applied modern C++ declare-on-first-use patterns to VM comparison/conversion functions
- **lvm_comparison.cpp**: Added const correctness to comparison helpers
- **lvm_conversion.cpp**: Improved variable scoping in conversion functions
- **Impact**: VM hot path now follows modern C++ best practices
- **Files Changed**: 2 files (VM modules)
- **Result**: ~2.11s avg ✅ (maintained performance)

**Phase 148-B**: Core Execution Declare-on-First-Use ✅ **(3 parts, 10 functions)**
- **Part 1** (4 functions - Critical bug fix + improvements):
  - **BUG FIX**: Fixed uninitialized `isfloat` in `lcode.cpp::codeorder()` (test failure)
  - `rawRunProtected()`: Aggregate initialization + const correctness
  - `callHook()`: If-init-statement (C++17) + const correctness
  - `retHook()`: Const correctness for computed values
  - `tryFuncTM()`: Combined declaration/initialization + scoping
- **Part 2** (3 functions - Function call hot path):
  - `preCallC()`: C function call preparation (const correctness)
  - `preCall()`: General function call (const for p, nfixparams, fsize, ci_new)
  - `preTailCall()`: Tail call optimization (const correctness)
- **Part 3** (3 functions - Error handling & continuation):
  - `closeProtected()`: Protected upvalue closing (const saved state)
  - `pCall()`: Protected function call (const saved state)
  - `finishCCall()`: C function continuation (ternary for status_val)
- **Impact**: Core execution path now uses modern C++ idioms throughout
- **Files Changed**: 2 files (lcode.cpp, ldo.cpp), 10 functions, ~40 variable improvements
- **Result**: ~2.25s avg ✅ (46% faster than baseline!)

**Phase 148-C**: Table Hash Hot Path Declare-on-First-Use ✅ **(4 functions)**
- **MAJOR REFACTORING**: Eliminated 6 intermediate variables in `mainpositionTV()`
- **hashint()**: Made `ui` const (computed value never modified)
- **l_hashfloat()**: If-init-statement for `ni`, const for `u` (C++17)
- **mainpositionTV()**: Eliminated ALL intermediate variables (i, n, ts×2, p, f, o)
  - Direct returns in all switch cases - simpler, faster code
  - Removed unnecessary braces from switch cases
  - **Code reduction**: 15 lines removed!
- **getgeneric()**: Const correctness for `base`, `limit`, `nextIndex`
- **Impact**: Table hash path (called on EVERY table access) now highly optimized
- **Files Changed**: 1 file (ltable.cpp), 4 functions, 10+ variable improvements
- **Result**: ~2.26s avg ✅ (46% faster than baseline!)
- **See**: `docs/PHASE_148_DECLARE_ON_FIRST_USE_PLAN.md` for full improvement plan

**Phase 149**: Compiler Path Declare-on-First-Use ✅ **(3 parts, 11 functions)**
- **Part 1** (5 functions - Code generation & function state):
  - `codeeq()`: If-init-statement for `im` (C++17 pattern)
  - `settablesize()`: Const correctness for all 5 computed values
  - `errorlimit()`: Eliminated `where` intermediate variable (code reduction)
  - `fixforjump()`: Ternary operator for `offset`, const for `jmp`
  - `searchupvalue()`: Const for span, cached cast (eliminates repeated conversion)
- **Part 2** (3 functions - Parser & variable lookup):
  - `nil()`: Const correctness for `previous`, `pfrom`, `pl`
  - `singlevaraux()`: Const correctness for `v` (variable lookup result)
  - `subexpr()`: Declare-on-first-use for `uop`/`op`, const for `line` declarations
- **Part 3** (3 functions - Additional const correctness):
  - `solvegotos()`: Made `outlevel` const
  - `leaveblock()`: Made `stklevel` const
  - `indexed()`: Made `keystr` const
- **Impact**: Compiler code generation and parsing now follow modern C++ idioms
- **Files Changed**: 3 files (lcode.cpp, funcstate.cpp, parser.cpp), 11 functions
- **Result**: ~2.25s avg ✅ (46% faster than baseline, maintained!)

**Phase 150**: Compiler Path - Declare-on-First-Use Improvements ✅ **(7 functions)**
- **parselabels.cpp** (3 functions, 6 improvements):
  - `closegoto()`: Moved loop counter `i` to for-loop, added const to `gl`, `gt`, `stklevel`
  - `findlabel()`: Made `dynData` const pointer
  - `newlabelentry()`: Made `n` and `desc` const
- **lcode.cpp** (4 functions, 7 improvements):
  - `codeAsBx()`: Made `b` const (computed offset)
  - `codek()`: Made `p` const (instruction position)
  - `addk()`: Made `L`, `k`, `constantsSpan` const
  - `k2proto()`: Made `tag` and `k` const (both branches)
- **Impact**: Compiler code generation now follows modern C++ idioms throughout
- **Files Changed**: 2 files, 7 functions, 13 variable improvements
- **Result**: ~2.34s avg ✅ (44% faster than baseline!)

**Phase 151**: Parser Functions - Const Correctness ✅ **(3 functions)**
- **parser.cpp** (3 functions, 4 improvements):
  - `recfield()`: Made `reg` const (register saved for restoration)
  - `constructor()`: Made `line` and `pc` const
  - `funcargs()`: Made `base` const (base register for function call)
- **Impact**: Parser code generation functions now const-correct
- **Files Changed**: 1 file, 3 functions, 4 variable improvements
- **Result**: ~2.39s avg ✅ (43% faster than baseline!)

**Phase 152**: Binary Expression Code Generation - Const Correctness ✅ **(6 functions)**
- **lcode.cpp** (6 functions, 11 improvements):
  - `codeunexpval()`: Made `targetRegister` const
  - `finishbinexpval()`: Made `leftRegister` and `instructionPosition` const
  - `codebinexpval()`: Made `operation` and `rightRegister` const
  - `codebini()`: Made `rightValue` const
  - `codebinK()`: Made `event`, `constantIndex`, and `operation` const
  - `finishbinexpneg()`: Made `i2` and `v2` const
- **Impact**: Expression code generation now consistently const-correct
- **Files Changed**: 1 file, 6 functions, 11 variable improvements
- **Result**: ~2.34s avg ✅ (44% faster than baseline!)

**Phase 153**: Code Generation Helpers - Const Correctness ✅ **(6 functions)**
- **lcode.cpp** (6 functions, 8 improvements):
  - `codeconcat()`: Made `n` const (element count)
  - `finaltarget()`: Made `codeSpan` and `instr` const
  - `codeABx()`, `codeABCk()`, `codevABCk()`: Made `op` const (opcode after cast)
  - `codesJ()`: Made `j` const (adjusted jump offset)
- **Impact**: Instruction encoding functions now const-correct
- **Files Changed**: 1 file, 6 functions, 8 variable improvements
- **Result**: ~2.34s avg ✅ (44% faster than baseline!)

**Phase 154**: Table & FuncState - Const Correctness ✅ **(3 functions)**
- **ltable.cpp** (2 functions, 2 improvements):
  - `countint()`: Made `k` const (array index check result)
  - `numusearray()`: Made `arraySize` const (used in loop)
- **funcstate.cpp** (1 function, 1 improvement):
  - `maxtostore()`: Made `numfreeregs` const (free register count)
- **Impact**: Table rehashing and register allocation now const-correct
- **Files Changed**: 2 files, 3 functions, 3 variable improvements
- **Result**: ~2.35s avg ✅ (44% faster than baseline!)

**Phase 155**: GC & Memory Modules - Declare-on-First-Use ✅ **(40 improvements)**
- **gc_marking.cpp** (15 improvements):
  - Moved 8 loop variables to for-loop initializers: `userValueIndex`, `upvalueIndex` (2×), `typeIndex`, `arrayIndex`, `o`, `uv`, `p`, `n`
  - Moved 1 while-loop variable to condition: `thread` in `remarkupvals()`
  - Added const to 4 computed values: `uv`, `u` (`reallymarkobject`), `asize`, `limit`
- **gc_weak.cpp** (15 improvements):
  - Moved 5 loop variables to for-loop/while-loop: `arrayIndex` (2×), `n` (3×), `w`
  - Added const to 9 computed values: `asize` (2×), `limit` (4×), `h` (3×)
  - Reordered `changed`/`dir` declarations for better locality
- **gc_sweeping.cpp** (10 improvements):
  - Moved 2 loop variables to while-loop conditions: `curr` (2×)
  - Added const to 8 computed values: `g` (3×), `ow`, `white` (2×), `old`, `age`
- **Impact**: All GC modules now follow modern C++ declare-on-first-use best practices
- **Code Quality**: Improved variable scoping, clearer lifetimes, const correctness throughout
- **Files Changed**: 3 GC module files, 40 variable improvements
- **Result**: ~2.32s avg ✅ (45% faster than baseline, maintained!)

**Phase 156**: Core API Declare-on-First-Use ✅ **(3 parts, 27 functions, 29 improvements)**
- **Part 1 - Simple Const Additions** (13 functions, 16 variables):
  - `lua_copy()`: Made `fr`, `to` const
  - `lua_numbertocstring()`: Made `len` const (refactored to avoid increment)
  - `lua_next()`: Made `t`, `more` const
  - `lua_toclose()`: Made `o` const
  - `lua_len()`: Made `t` const
  - `lua_getallocf()`: Made `f` const
  - `lua_newuserdatauv()`: Made `u` const
  - `aux_upvalue()`: Made `f` (CCL/LCL), `p`, `name` const
  - `getupvalref()`: Made `fi`, `f` const (also combined declaration/initialization)
  - `lua_upvalueid()`: Made `fi`, `f` const
  - `lua_load()`: Made `f` const
  - `lua_dump()`: Made `f` const
  - `lua_gc()`: Made `oldstp`, `param`, `value` const
- **Part 2 - Combine Declaration/Initialization** (7 functions, 9 variables):
  - `lua_tolstring()`: Combined `o` declaration with initialization
  - `lua_error()`: Combined `errobj` declaration with initialization (+ const)
  - `lua_getupvalue()`: Combined `name` declaration with initialization (+ const)
  - `lua_setupvalue()`: Combined `name`, `fi` declarations with initialization (+ const)
  - `lua_load()`: Combined `status` declaration with initialization (+ const)
  - `lua_dump()`: Combined `status` declaration with initialization (+ const)
- **Part 3 - Ternary Operators for Cleaner Logic** (4 functions):
  - `lua_getiuservalue()`: Used ternary with comma operator for `t` (+ const)
  - `lua_setmetatable()`: Used ternary with comma operator for `mt` (+ const)
  - `lua_setiuservalue()`: Used ternary with comma operator for `res` (+ const)
  - `lua_pcallk()`: Used ternary with comma operator for `func` (+ const)
- **Impact**: Core API now follows modern C++ best practices throughout
- **Code Quality**: Better const correctness, improved variable scoping, clearer lifetimes
- **Benefits**: Reduced variable scope, clearer data flow, prevents accidental modifications
- **Code Reduction**: -29 lines (cleaner, more concise code)
- **Files Changed**: 1 file (lapi.cpp), 27 functions, 29 variable improvements
- **Result**: ~2.23s avg ✅ (47% faster than baseline, continued improvement!)

---

## Performance

**Baseline**: 4.20s (Nov 2025) | **Target**: ≤4.33s (3% tolerance)
**Current**: ~2.23s avg ✅ **47% faster than baseline!**

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
Zero warnings | 96.1% coverage | 30+ tests passing | **47% faster than baseline!**
Phases 1-127, 129-1, 130-ALL, 131, 133, 134, 135-Rev, 136-156 complete | Phase 135, 139 skipped ✅
[[nodiscard]]: 102 annotations | Const correctness: Excellent ✅ | Identifiers: 62 modernized (36 loop iterators + 26 instruction/API variables) ✅
Declare-on-first-use: 136 total improvements (Phases 148-A/B/C: 14, Phases 149-154: 53, Phase 155: 40, Phase 156: 29) | Code reduction: 45 lines removed ✅

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
12. **Parallel arrays are complex** (Phase 135: allocation failure modes, exception-safety, invariant tracking make them error-prone; avoid unless essential)
13. **Not all functions with return values need [[nodiscard]]** (Phase 138: functions used for side effects like pushfstring are legitimate dual-use)
14. **Const correctness pays dividends** (Phase 139: previous incremental const work meant no improvements needed!)
15. **Descriptive identifiers improve code clarity dramatically** (Phases 140-147: 62 cryptic identifiers → self-documenting names; 36 loop iterators + 26 instruction/API variables; zero performance impact, continued improvement)
16. **Declare-on-first-use eliminates waste** (Phase 148: eliminated 6 intermediate variables in mainpositionTV(); direct returns are faster and clearer; if-init-statements reduce scope; const prevents accidents)

---

## Future Opportunities

**High-Priority Next Steps**:
- **Phase 157+**: Continue declare-on-first-use improvements (see `docs/PHASE_148_DECLARE_ON_FIRST_USE_PLAN.md`)
  - Library functions (~50 functions - low priority)
  - Core execution functions (ldo.cpp remaining opportunities)
- Phase 129 Part 2: Range-based for loops in ldebug.cpp (medium risk)
- Phase 128: std::span performance optimization (if needed - current perf excellent)
- Code documentation / comment improvements (if needed)

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

**Specialized**: [NECESSARY_MACROS.md](docs/NECESSARY_MACROS.md) | [PHASE_125_LUAV_WRAPPER_ELIMINATION.md](docs/PHASE_125_LUAV_WRAPPER_ELIMINATION.md) | [PHASE_130_POINTER_TO_REFERENCE.md](docs/PHASE_130_POINTER_TO_REFERENCE.md) | [PHASE_135_FAILED_ATTEMPT.md](docs/PHASE_135_FAILED_ATTEMPT.md) | [PHASE_148_DECLARE_ON_FIRST_USE_PLAN.md](docs/PHASE_148_DECLARE_ON_FIRST_USE_PLAN.md) | [GC_SIMPLIFICATION_ANALYSIS.md](docs/GC_SIMPLIFICATION_ANALYSIS.md) | [SPAN_MODERNIZATION_PLAN.md](docs/SPAN_MODERNIZATION_PLAN.md) | [COVERAGE_ANALYSIS.md](docs/COVERAGE_ANALYSIS.md) | [UNDEFINED_BEHAVIOR_ANALYSIS.md](docs/UNDEFINED_BEHAVIOR_ANALYSIS.md)

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

**Updated**: 2025-12-13 | **Phases**: 1-127, 129-1, 130-ALL, 131, 133, 134, 135-Rev, 136-156 ✅ | Phase 135, 139 skipped
**Performance**: ~2.23s ✅ (47% faster than baseline!) | **Status**: Modern C++23, [[nodiscard]]: 102 annotations, excellent const-correctness, 62 identifiers modernized, 136 total declare-on-first-use improvements (Phases 148-A/B/C: 14, Phases 149-154: 53, Phase 155: 40, Phase 156: 29)
