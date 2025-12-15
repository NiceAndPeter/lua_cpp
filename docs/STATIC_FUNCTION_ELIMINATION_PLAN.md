# Phase Plan: Complete Elimination of File-Scope Static Functions

## Goal
Eliminate all 609 file-scope `static` functions using modern C++ patterns:
- **Private class methods** when function takes class argument (~160 functions)
- **Anonymous namespaces** for pure utilities and library APIs (~450 functions)
- **Zero** file-scope `static` functions remaining

## Design Principle
**RULE**: When a static function takes a class argument (pointer or reference), it becomes a private member function of that class.

**EXCEPTIONS**:
- Library API functions (lstrlib.cpp, lmathlib.cpp, etc.) → anonymous namespace
- Test infrastructure (ltests.cpp) → anonymous namespace

---

## Current State Analysis

**609 static functions** across 48 .cpp files, categorized as follows:

### Class Member Candidates (~160 functions)
Functions that take specific class arguments and should become private methods:

| Class | Functions | Source Files |
|-------|-----------|--------------|
| Table | 40 | ltable.cpp |
| LoadState | 23 | lundump.cpp |
| DumpState | 17 | ldump.cpp |
| lua_State | ~35 | ldo.cpp (7), lstate.cpp (5), lfunc.cpp (4), ldebug.cpp (11), lapi.cpp (1), lobject.cpp (2) |
| FuncState | 13 | lcode.cpp |
| CallInfo | 6 | ldebug.cpp |
| Proto | 3 | ldebug.cpp |
| TString | 5 | lstring.cpp |
| BuffFS | 3 | lobject.cpp |
| Others | ~15 | Various |

### Anonymous Namespace Candidates (~450 functions)
Pure utilities, library APIs, test infrastructure:

| Category | Functions | Files |
|----------|-----------|-------|
| Library APIs | 327 | lstrlib (75), lmathlib (50), liolib (47), loadlib (37), lbaselib (32), ldblib (27), ltablib (17), loslib (18), lcorolib (13), lutf8lib (11) |
| Test infrastructure | 92 | ltests.cpp |
| Interpreter utilities | 31 | lua.cpp |
| Auxiliary library | 21 | lauxlib.cpp |
| Pure utilities | ~10 | ldebug.cpp (3), lobject.cpp (6), lstring.cpp (1) |

---

## Implementation Strategy

### Three-Track Approach

**Track 1: High-Impact Class Refactoring** (93 functions)
- Clear ownership, significant encapsulation improvement
- Files: ltable.cpp, lundump.cpp, ldump.cpp, lcode.cpp

**Track 2: lua_State Consolidation** (~35 functions)
- Centralize state management functions
- Files: ldo.cpp, lstate.cpp, lfunc.cpp, ldebug.cpp (partial), lapi.cpp (partial)

**Track 3: Remaining Classes + Anonymous Namespaces** (~480 functions)
- Other class methods + all pure utilities
- Files: All library files, test files, remaining utilities

---

## TRACK 1: High-Impact Class Refactoring (4 Phases)

### Phase 1A: Table Class - Hash & Key Operations
**File**: src/objects/ltable.cpp | **Functions**: 40 → Table private methods | **Risk**: ⭐⭐⭐ (Medium-High)

**Convert to private methods**:

```cpp
class Table : public GCBase<Table> {
private:
    // === HASH FUNCTIONS (3) ===
    static unsigned int hashInt(lua_Integer i);
    static unsigned int hashFloat(lua_Number n);
    Node* mainPosition(const TValue& key);
    Node* mainPositionFromNode(const Node& nd);

    // === KEY OPERATIONS (8) ===
    static bool equalKey(const TValue& k1, const Node& n2);
    bool keyInArray(const TValue& key, lua_Unsigned* index);
    static bool findIndex(lua_State* L, TValue& key, lua_Unsigned i);
    static void checkRange(lua_State* L, lua_Integer i);
    bool hashKeyIsEmpty(const TValue* key);

    // === GET/SET HELPERS (8) ===
    int getIntFromHash(lua_Integer key);
    void finishNodeGet(const TValue* p1, TValue* res);
    const TValue* getGeneric(const TValue& key, bool deadok);
    const TValue* getStr(TString* key);
    const TValue* getLongStr(TString* key);

    // === REHASHING (10) ===
    void computeSizes(unsigned int nums[], unsigned int& numArray);
    unsigned int numUseHash(const unsigned int* nums, unsigned int totalUse);
    unsigned int numUseArray(const unsigned int* nums, unsigned int& numArray);
    unsigned int countInt(lua_Integer key, unsigned int* nums);
    void rehash(lua_State* L, const TValue& ek);
    void reinsertHash(lua_State* L);
    void reinsertOldSlice(lua_State* L, unsigned int size);
    void exchangeHashPart(lua_State* L, Table* t2);
    void resizeArray(lua_State* L, unsigned int nasize, unsigned int nhsize);

    // === NODE MANAGEMENT (7) ===
    Node* getFreePos();
    TValue* newKey(lua_State* L, const TValue& key);
    TValue* newCheckedKey(lua_State* L, const TValue& key);
    void setNodeVector(lua_State* L, unsigned int size);
    void freeHash(lua_State* L);
    void insertKey(lua_State* L, const TValue& key, TValue* dest);

    // === BOUNDARY SEARCH (4) ===
    lua_Unsigned hashSearch(lua_Unsigned j);
    lua_Unsigned binsearch(lua_Unsigned i, lua_Unsigned j);
    lua_Unsigned newHint(lua_Unsigned oldhint, lua_Unsigned val);

public:
    // Existing public interface unchanged
};
```

**Critical Files**: src/objects/ltable.cpp, include/objects/ltable.h

**Benefits**:
- ✅ Direct access to private members (sizenode, node, lastfree, etc.)
- ✅ Clear ownership (these ARE Table operations)
- ✅ Better encapsulation (hide implementation details)
- ✅ Eliminate 40 global-scope static functions

**Testing**: Critical - Table is a hot path. Full benchmark required.

---

### Phase 1B: LoadState Class - Binary Loading
**File**: src/serialization/lundump.cpp | **Functions**: 23 → LoadState private methods | **Risk**: ⭐⭐ (Low-Medium)

**Convert to private methods**:

```cpp
class LoadState {
private:
    // === PRIMITIVES (8) ===
    void loadBlock(void* b, size_t size);
    void loadAlign(int align);
    lu_byte loadByte();
    size_t loadVarint();
    size_t loadSize();
    int loadInt();
    lua_Number loadNumber();
    lua_Integer loadInteger();

    // === STRING LOADING (1) ===
    TString* loadString();

    // === PROTO LOADING (6) ===
    void loadCode(Proto& f);
    void loadConstants(Proto& f);
    void loadProtos(Proto& f);
    void loadUpvalues(Proto& f);
    void loadDebug(Proto& f);
    Proto* loadFunction(TString* psource);

    // === VALIDATION (5) ===
    void checkLiteral(const char* s, const char* msg);
    void numError(const char* msg);
    void checkNumSize(size_t size, const char* msg);
    void checkNumFormat(lua_Number num, const char* msg);
    void checkHeader();

    // === HELPERS (2) ===
    void* getAddress_(size_t n);
    [[noreturn]] void error(const char* why);

public:
    // Existing interface
};
```

**Critical Files**: src/serialization/lundump.cpp, include/serialization/lundump.h

**Benefits**: Self-contained, clearer API, better error handling encapsulation

**Testing**: Verify bytecode loading works correctly (compile Lua files, load them)

---

### Phase 1C: DumpState Class - Binary Dumping
**File**: src/serialization/ldump.cpp | **Functions**: 17 → DumpState private methods | **Risk**: ⭐⭐ (Low-Medium)

**Convert to private methods**:

```cpp
class DumpState {
private:
    // === PRIMITIVES (8) ===
    void dumpBlock(const void* b, size_t size);
    void dumpAlign(int align);
    void dumpByte(lu_byte x);
    void dumpVarint(size_t x);
    void dumpSize(size_t x);
    void dumpInt(int x);
    void dumpNumber(lua_Number x);
    void dumpInteger(lua_Integer x);

    // === STRING DUMPING (1) ===
    void dumpString(const TString* s);

    // === PROTO DUMPING (6) ===
    void dumpCode(const Proto& f);
    void dumpConstants(const Proto& f);
    void dumpProtos(const Proto& f);
    void dumpUpvalues(const Proto& f);
    void dumpDebug(const Proto& f);
    void dumpFunction(const Proto& f, TString* psource);

    // === HEADER (1) ===
    void dumpHeader();

public:
    // Existing interface
};
```

**Critical Files**: src/serialization/ldump.cpp, include/serialization/ldump.h

**Benefits**: Symmetric with LoadState, clear serialization encapsulation

**Testing**: Verify bytecode dumping works (lua_dump API)

---

### Phase 1D: FuncState Class - Code Generation Helpers
**File**: src/compiler/lcode.cpp | **Functions**: 13 → FuncState private methods | **Risk**: ⭐⭐⭐ (Medium)

**Convert to private methods**:

```cpp
class FuncState {
private:
    // === JUMP PATCHING (9) ===
    void removeLastSkip();
    int lastTarget();
    void patchTestReg(int node, int reg);
    Instruction* getJumpControl(int pc);
    bool needValue(int list);
    void patchListAux(int list, int vtarget, int reg, int dtarget);
    void dischargeJpc();
    void patchList(int list, int target);
    void patchToHere(int list);

    // === CLOSE PATCHING (4) ===
    void patchClose(int list, int level);

    // === INLINE HELPERS (3) - keep inline ===
    inline static OpCode binOpr2Op(BinOpr opr);
    inline static OpCode unOpr2Op(UnOpr opr);
    inline static TMS binOpr2TM(BinOpr opr);

public:
    // Existing public interface
    // Public versions of patchList, patchToHere remain
};
```

**Critical Files**: src/compiler/lcode.cpp, src/compiler/funcstate.h

**Benefits**: Code generation is inherently FuncState operations, better encapsulation

**Testing**: Compile Lua code, verify correct bytecode generation

---

## TRACK 2: lua_State Consolidation (5 Phases)

### Phase 2A: Core Execution Functions (ldo.cpp)
**File**: src/core/ldo.cpp | **Functions**: 7 → lua_State private methods | **Risk**: ⭐⭐⭐⭐ (High)

**Functions taking lua_State***:
- `resume(lua_State* L, int n)`
- `resume_error(lua_State* L, const char* msg, int narg)`
- `unroll(lua_State* L, void* ud)`
- `recover(lua_State* L, int status)`
- `finishCcall(lua_State* L, int n)`
- `checkmode(lua_State* L, const char* mode, const char* x)`
- `f_parser(lua_State* L, void* ud)`

**Convert to**:
```cpp
class lua_State {
private:
    void resume(int n);
    int resumeError(const char* msg, int narg);
    CallInfo* unroll(void* ud);
    void recover(int status);
    void finishCCall(int n);
    static void checkMode(const char* mode, const char* x);
    static void parseFunction(void* ud);  // Or keep in anonymous namespace as callback
};
```

**Note**: Some may need to stay as free functions if used as callbacks (f_parser)

**Critical Files**: src/core/ldo.cpp, src/core/lstate.h

**Testing**: Critical path - run full test suite + benchmarks

---

### Phase 2B: State Management Functions (lstate.cpp)
**File**: src/core/lstate.cpp | **Functions**: 5 → lua_State/global_State private methods | **Risk**: ⭐⭐⭐ (Medium)

**Functions**:
- `freeCI(lua_State* L)` → lua_State private
- `resetCI(lua_State* L)` → lua_State private
- `stack_init(lua_State* L)` → lua_State private
- `freestack(lua_State* L)` → lua_State private
- `preinit_thread(lua_State* L)` → lua_State private
- `init_registry(lua_State* L)` → global_State private (takes g via L->l_G)
- `close_state(lua_State* L)` → lua_State private
- `f_luaopen(lua_State* L, void* ud)` → Anonymous namespace (callback)

**Critical Files**: src/core/lstate.cpp, src/core/lstate.h

**Testing**: State creation/destruction tests

---

### Phase 2C: Upvalue & Closure Functions (lfunc.cpp)
**File**: src/objects/lfunc.cpp | **Functions**: 4 → lua_State private methods | **Risk**: ⭐⭐ (Low-Medium)

**Functions taking lua_State***:
- `callclosemethod(lua_State* L, TValue& obj, TValue& err, int status)`
- `checkclosemth(lua_State* L, StkId level, int status)`
- `prepcallclosemth(lua_State* L, StkId level, int status, int yy)`
- `poptbclist(lua_State* L, StkId level)`

**Convert to lua_State private methods**:
```cpp
class lua_State {
private:
    static void callCloseMethod(TValue& obj, TValue& err, int status);
    int checkCloseMethod(StkId level, int status);
    void prepCallCloseMethod(StkId level, int status, int yy);
    void popTBCList(StkId level);
};
```

**Critical Files**: src/objects/lfunc.cpp, src/core/lstate.h

**Testing**: To-be-closed variables tests

---

### Phase 2D: Debug Functions → lua_State (ldebug.cpp partial)
**File**: src/core/ldebug.cpp | **Functions**: 11 → lua_State private methods | **Risk**: ⭐⭐⭐ (Medium)

**Functions taking lua_State***:
- Various debug info functions that operate on lua_State

**Convert to lua_State private methods**

**Critical Files**: src/core/ldebug.cpp, src/core/lstate.h

**Testing**: Error messages, stack traces, debug API

---

### Phase 2E: API & Object Helpers
**Files**: lapi.cpp (1), lobject.cpp (2) | **Functions**: 3 → lua_State private methods | **Risk**: ⭐⭐ (Low)

**lapi.cpp**:
- `reverse(lua_State* L, StkId from, StkId to)` → lua_State private

**lobject.cpp**:
- `intarith(lua_State* L, int op, lua_Integer v1, lua_Integer v2)` → lua_State private
- `numarith(lua_State* L, int op, lua_Number v1, lua_Number v2)` → lua_State private

**Critical Files**: src/core/lapi.cpp, src/objects/lobject.cpp, src/core/lstate.h

**Testing**: API tests, arithmetic operations

---

## TRACK 3: Remaining Classes + Anonymous Namespaces (6 Phases)

### Phase 3A: Other Class Methods
**Files**: ldebug.cpp, lstring.cpp, lobject.cpp | **Functions**: ~20 | **Risk**: ⭐⭐ (Low-Medium)

**CallInfo private methods** (6 from ldebug.cpp):
- `currentpc(CallInfo* ci)` → CallInfo::currentPc()
- Functions taking CallInfo*

**Proto private methods** (3 from ldebug.cpp):
- `getbaseline(const Proto& f, ...)` → Proto::getBaseline()
- Functions taking Proto*

**TString/stringtable methods** (5 from lstring.cpp):
- `tablerehash(stringtable* tb, ...)` → stringtable::rehash()
- String factory helpers

**BuffFS methods** (3 from lobject.cpp):
- `pushbuff(BuffFS* buff, ...)` → BuffFS::push()
- Buffer operations

**Critical Files**: Various class headers and implementation files

**Testing**: Debug info, string interning, formatting

---

### Phase 3B: Library Files - Anonymous Namespace (Part 1)
**Files**: lstrlib.cpp, lmathlib.cpp, liolib.cpp | **Functions**: 172 | **Risk**: ⭐⭐ (Low)

**Pattern for all library files**:
```cpp
// lstrlib.cpp
namespace {
    int str_len(lua_State* L) {
        size_t len;
        luaL_checklstring(L, 1, &len);
        lua_pushinteger(L, static_cast<lua_Integer>(len));
        return 1;
    }

    // ... all 75 string library functions
}

static const luaL_Reg strlib[] = {
    {"len", str_len},      // ✅ Can reference namespace functions
    {"sub", str_sub},
    // ... rest of registration table
    {nullptr, nullptr}
};
```

**Files**:
- lstrlib.cpp (75 functions) - String library
- lmathlib.cpp (50 functions) - Math library
- liolib.cpp (47 functions) - I/O library

**Key**: The `luaL_Reg` arrays stay at file scope, functions move to anonymous namespace

**Critical Files**: src/libraries/*.cpp

**Testing**: Run standard library tests

---

### Phase 3C: Library Files - Anonymous Namespace (Part 2)
**Files**: loadlib.cpp, lbaselib.cpp, ldblib.cpp | **Functions**: 96 | **Risk**: ⭐⭐ (Low)

**Files**:
- loadlib.cpp (37 functions) - Dynamic loading
- lbaselib.cpp (32 functions) - Base library
- ldblib.cpp (27 functions) - Debug library

**Pattern**: Same as Phase 3B

**Critical Files**: src/libraries/*.cpp

**Testing**: Module loading, base functions, debug library

---

### Phase 3D: Library Files - Anonymous Namespace (Part 3)
**Files**: ltablib.cpp, loslib.cpp, lcorolib.cpp, lutf8lib.cpp | **Functions**: 59 | **Risk**: ⭐⭐ (Low)

**Files**:
- ltablib.cpp (17 functions) - Table library
- loslib.cpp (18 functions) - OS library
- lcorolib.cpp (13 functions) - Coroutine library
- lutf8lib.cpp (11 functions) - UTF-8 library

**Pattern**: Same as Phase 3B

**Critical Files**: src/libraries/*.cpp

**Testing**: Table manipulation, OS functions, coroutines, UTF-8

---

### Phase 3E: Auxiliary & Interpreter - Anonymous Namespace
**Files**: lauxlib.cpp, lua.cpp | **Functions**: 52 | **Risk**: ⭐ (Very Low)

**lauxlib.cpp** (21 functions):
```cpp
namespace {
    void findfield(lua_State* L, int objidx, int level) { ... }
    int pushglobalfuncname(lua_State* L, lua_Debug* ar) { ... }
    // ... all auxiliary helper functions
}
```

**lua.cpp** (31 functions):
```cpp
namespace {
    static void lstop(lua_State* L, lua_Debug* ar) { ... }
    static void laction(int i) { ... }
    // ... all interpreter utilities
}
```

**Critical Files**: src/auxiliary/lauxlib.cpp, src/interpreter/lua.cpp

**Testing**: Auxiliary library functions, standalone interpreter

---

### Phase 3F: Testing & Pure Utilities - Anonymous Namespace
**Files**: ltests.cpp, remaining utilities | **Functions**: ~100 | **Risk**: ⭐ (Very Low)

**ltests.cpp** (92 functions):
```cpp
namespace {
    int testC(lua_State* L) { ... }
    int gc_query(lua_State* L) { ... }
    // ... all test infrastructure
}
```

**Remaining pure utilities** from ldebug.cpp (3), lobject.cpp (6), lstring.cpp (1):
- Functions with no class arguments
- Pure computation helpers

**Critical Files**: src/testing/ltests.cpp, various

**Testing**: Test suite functionality

---

## Implementation Order (Recommended)

### **Batch 1: Prove Class Method Approach** (Phases 1A-1D)
- Table class (40 functions) - highest impact
- LoadState/DumpState (40 functions) - clean, isolated
- FuncState (13 functions) - compiler
- **Total**: 93 functions → class methods
- **Goal**: Validate class method refactoring works

### **Batch 2: Consolidate lua_State** (Phases 2A-2E)
- Core execution, state management, upvalues, debug, API
- **Total**: ~35 functions → lua_State private methods
- **Goal**: Centralize state operations

### **Batch 3: Other Classes** (Phase 3A)
- CallInfo, Proto, TString, BuffFS, etc.
- **Total**: ~20 functions → respective class methods
- **Goal**: Complete class method conversions

### **Batch 4: Anonymous Namespace Cleanup** (Phases 3B-3F)
- All library files, auxiliary, interpreter, tests, utilities
- **Total**: ~460 functions → anonymous namespaces
- **Goal**: Eliminate remaining static functions

**Final Result**: 0 file-scope static functions, 100% modern C++ patterns

---

## Testing Strategy

### After Each Phase:
1. **Compile**: Zero warnings, zero errors
2. **Unit Tests**: `cd testes && ../build/lua all.lua` → "final OK !!!"
3. **Sanitizers**: Run with ASAN + UBSAN for memory-sensitive changes
4. **Benchmarks**: For hot paths (Table, lua_State, FuncState), run 5-iteration benchmark

### Acceptance Criteria Per Phase:
- ✅ All tests pass
- ✅ Zero sanitizer errors
- ✅ Performance maintained (≤3% regression, target ≤2.31s)
- ✅ Zero compiler warnings
- ✅ No public API changes

---

## Special Considerations

### 1. Inline Static Functions (15 total)
**Keep `inline` keyword** when converting:
```cpp
class FuncState {
private:
    inline static OpCode binOpr2Op(BinOpr opr) { ... }  // ✅ Correct
};

namespace {
    inline void clearkey(Node& n) { ... }  // ✅ Correct for anonymous namespace
}
```

### 2. Callback Functions
Some functions are used as callbacks (e.g., `f_parser`, `f_luaopen`):
- **Option 1**: Keep in anonymous namespace (cleaner)
- **Option 2**: Make static private method with public wrapper
- **Recommendation**: Anonymous namespace for callbacks

### 3. Forward Declarations
Convert forward declarations appropriately:
```cpp
// Before
static void someFunction();
static void someFunction() { ... }

// After (class method)
class MyClass {
private:
    void someFunction();  // Declaration in header
};
void MyClass::someFunction() { ... }  // Definition in .cpp

// After (anonymous namespace)
namespace {
    void someFunction();  // Forward declaration
    void someFunction() { ... }  // Definition
}
```

### 4. Static Data vs Static Functions
Some files have `static` data (e.g., `dummynode_` in ltable.cpp):
- **Keep as file-scope static** (data, not functions)
- Or convert to `constexpr` class members where appropriate

### 5. Library Registration Arrays
`luaL_Reg` arrays can reference functions in anonymous namespaces:
```cpp
namespace {
    int str_len(lua_State* L) { ... }
}

static const luaL_Reg strlib[] = {
    {"len", str_len},  // ✅ Valid C++
    {nullptr, nullptr}
};
```

---

## Risk Assessment

| Phase | Risk Level | Mitigation |
|-------|------------|------------|
| 1A (Table) | ⭐⭐⭐⭐ High | Hot path - extensive benchmarking required |
| 1B (LoadState) | ⭐⭐ Low-Med | Well-isolated, good test coverage |
| 1C (DumpState) | ⭐⭐ Low-Med | Symmetric with LoadState |
| 1D (FuncState) | ⭐⭐⭐ Medium | Compiler path - verify bytecode |
| 2A (ldo.cpp) | ⭐⭐⭐⭐ High | Core execution - critical path |
| 2B-2E (lua_State) | ⭐⭐⭐ Medium | State management - careful testing |
| 3A (Other classes) | ⭐⭐ Low-Med | Small, isolated changes |
| 3B-3F (Anonymous) | ⭐⭐ Low | Mechanical transformation |

**Overall Risk**: Medium (significant refactoring, but well-scoped)

---

## Expected Outcomes

### Code Quality
- ✅ **Zero file-scope static functions** (609 → 0)
- ✅ **~160 new private class methods** (better encapsulation)
- ✅ **~450 functions in anonymous namespaces** (modern C++)
- ✅ Clearer ownership and responsibilities
- ✅ Better testability (can mock/test class methods)

### Performance
- 🎯 **Target**: Maintain ≤2.31s average
- 📊 **Expected**: 0-2% variation (mostly semantic changes)
- ⚡ **Potential**: Improved inlining opportunities for class methods

### Maintainability
- 📖 Class methods clearly show what operates on what
- 🔒 Private members accessible without getters
- 🎯 Anonymous namespace makes internal linkage explicit
- 🧪 Easier to test (class methods vs global functions)

---

## Estimated Effort

**Batch 1** (Phases 1A-1D): ~3-4 sessions
- Phase 1A (Table): 1.5-2 sessions (most complex)
- Phases 1B-1D: 1.5-2 sessions (straightforward)

**Batch 2** (Phases 2A-2E): ~2-3 sessions
- lua_State consolidation requires careful coordination

**Batch 3** (Phase 3A): ~1 session
- Small class method additions

**Batch 4** (Phases 3B-3F): ~2-3 sessions
- High volume but mechanical changes

**Total**: ~8-11 development sessions for complete elimination

---

## Success Criteria

### Phase Completion
- ✅ All targeted static functions converted
- ✅ All tests pass (testes/all.lua → "final OK !!!")
- ✅ Zero compiler warnings (-Wall -Wextra -Wpedantic)
- ✅ Zero sanitizer errors (ASAN + UBSAN)
- ✅ Performance maintained (≤3% regression from baseline)

### Project Completion
- ✅ **Zero file-scope static functions** across entire codebase
- ✅ All 609 functions converted to either:
  - Private class methods (~160 functions)
  - Anonymous namespace functions (~450 functions)
- ✅ Documentation updated (CLAUDE.md, phase docs)
- ✅ Performance maintained or improved
- ✅ Clean git history (one commit per phase)

---

## Critical Files to Monitor

### Performance-Critical (Benchmark Required)
1. **src/objects/ltable.cpp** - Table operations (40 functions → methods)
2. **src/core/ldo.cpp** - Execution core (7 functions → lua_State methods)
3. **src/compiler/lcode.cpp** - Code generation (13 functions → FuncState methods)
4. **src/vm/lvirtualmachine.cpp** - VM (already mostly done)

### High Function Count
1. **src/libraries/lstrlib.cpp** (75 → anonymous namespace)
2. **src/testing/ltests.cpp** (92 → anonymous namespace)
3. **src/libraries/lmathlib.cpp** (50 → anonymous namespace)
4. **src/libraries/liolib.cpp** (47 → anonymous namespace)

### Structural Changes
1. **src/core/lstate.h** - lua_State class (will gain ~35 private methods)
2. **include/objects/ltable.h** - Table class (will gain 40 private methods)
3. **src/compiler/funcstate.h** - FuncState class (will gain 13 private methods)

---

## Next Steps

1. ✅ **User approval** of this plan
2. **Start with Phase 1A** (Table class - 40 functions)
   - Highest impact, validates approach
   - Extensive testing due to hot path
3. **Continue with Phases 1B-1D** (LoadState, DumpState, FuncState)
   - Prove class method refactoring pattern
4. **Proceed to lua_State consolidation** (Phases 2A-2E)
   - Centralize state management
5. **Complete with anonymous namespaces** (Phases 3A-3F)
   - Mechanical cleanup of remaining functions
6. **Update documentation** (CLAUDE.md, create phase doc)

---

## Plan Summary

**Approach**: Two-track refactoring (class methods + anonymous namespaces)
**Total Functions**: 609 → 0 (complete elimination of file-scope static)
**Class Methods**: ~160 functions (better encapsulation)
**Anonymous Namespaces**: ~450 functions (modern C++)
**Estimated Timeline**: 8-11 development sessions
**Risk Level**: Medium (significant but well-scoped refactoring)
**Expected Performance Impact**: 0-2% variation (maintain ≤2.31s)

**Status**: Ready for implementation 🚀
