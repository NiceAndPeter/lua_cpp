# ⚠️ DOCUMENTATION MOVED

**This file is outdated. Please refer to the updated documentation:**

# → See [CLAUDE.md](CLAUDE.md) for current documentation ←

---

## Quick Summary

**Repository**: `/home/user/lua_cpp`
**Status**: **100% ENCAPSULATION COMPLETE** ✅ (All 19 classes fully encapsulated)

For the comprehensive, up-to-date guide for AI assistants, see **[CLAUDE.md](CLAUDE.md)**

---

## Current Status

### Completed ✅
- **19 structs → classes**: Table, TString, Proto, UpVal, CClosure, LClosure, Udata, lua_State, global_State, CallInfo, GCObject, TValue, FuncState, LexState, expdesc, LocVar, AbsLineInfo, Upvaldesc, stringtable
- **13 classes fully encapsulated (68%)** with private fields: LocVar, AbsLineInfo, Upvaldesc, stringtable, GCObject, TString, Table, Proto, UpVal, CClosure, LClosure, CallInfo, expdesc
- **~500 macros converted** to inline functions/methods (37% of total convertible)
- **CRTP inheritance active** - GCBase<Derived> for all GC objects
- **CommonHeader eliminated** - Pure C++ inheritance
- **C++ exceptions** - Replaced setjmp/longjmp
- **Modern CMake** - Build system
- **Organized source tree** - Logical subdirectories
- **Zero warnings** - Compiles with -Werror

### In Progress 🔄
- **Encapsulation Phases 37-42**: FuncState, LexState, Udata, Udata0, global_State, lua_State
- **Macro Conversion**: ~75 remaining convertible macros identified

---

## Performance Requirements

### Critical Constraint
**ZERO regression tolerance** - Strict performance enforcement:
- Target: ≤2.21s (≤1% from baseline 2.17s)
- Current: **2.14s ✓ (3% faster!)**
- Must benchmark after EVERY change
- Revert immediately if regression detected

### Benchmark Command
```bash
cd /home/peter/claude/lua
make -C build

# 5-run benchmark
cd testes
for i in 1 2 3 4 5; do ../build/lua all.lua 2>&1 | grep "total time:"; done
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

### 2. Class Conversion Pattern

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
├── objects/        - Core data types (Table, TString, Proto, UpVal)
├── core/          - VM core (ldo, lapi, ldebug, lstate)
├── vm/            - Bytecode interpreter (lvm)
├── compiler/      - Parser and code generator (lparser, lcode)
├── memory/        - GC and memory management (lgc)
├── libraries/     - Standard libraries
├── auxiliary/     - Auxiliary library
├── serialization/ - Bytecode dump/undump
├── interpreter/   - Interactive interpreter
└── testing/       - Test infrastructure
```

### Module Organization
| Module | Prefix | Primary Class | Status |
|--------|--------|---------------|--------|
| Table | luaH_ | Table | ✅ Fully encapsulated |
| String | luaS_ | TString | ✅ Fully encapsulated |
| Object | luaO_ | TValue, GCObject | ✅ Fully encapsulated |
| Func | luaF_ | Proto, UpVal, Closures | ✅ Proto/UpVal encapsulated |
| Do | luaD_ | CallInfo | ✅ Class with methods |
| State | luaE_ | lua_State, global_State | ✅ Class with methods |
| GC | luaC_ | GCObject | ✅ Fully encapsulated |

---

## Testing & Validation

### Test Suite
**Location**: `/home/peter/claude/lua/testes/all.lua`
**Expected output**: `final OK !!!`

### Build Commands
```bash
# Build
cd /home/peter/claude/lua
make -C build

# Full rebuild
make -C build clean && make -C build

# Run tests
cd testes
../build/lua all.lua
```

### Performance Validation
```bash
cd /home/peter/claude/lua/testes

# 5-run benchmark
for i in 1 2 3 4 5; do \
    ../build/lua all.lua 2>&1 | grep "total time:"; \
done

# Target: ≤2.21s
# Current: ~2.14s ✓
```

---

## Code Style & Conventions

### Naming
- **Classes**: PascalCase (Table, TString)
- **Methods**: camelCase (get, arraySize)
- **Members**: snake_case (asize, lsizenode)
- **Constants**: UPPER_SNAKE_CASE (LUA_TNIL)

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

---

## Important Files

### Core Headers
- `include/lua.h` - Public C API (C-compatible)
- `src/objects/lobject.h` - Core type definitions
- `src/objects/ltvalue.h` - TValue class
- `src/core/lstate.h` - VM state
- `src/memory/lgc.h` - GC with GCBase<T> CRTP

### Implementation Files
- `src/objects/ltable.cpp` - Table methods
- `src/objects/lstring.cpp` - TString methods
- `src/objects/lfunc.cpp` - Proto, UpVal, Closure methods
- `src/memory/lgc.cpp` - GC implementation
- `src/vm/lvm.cpp` - VM bytecode interpreter (hot path)
- `src/core/ldo.cpp` - lua_State methods

### Build Files
- `CMakeLists.txt` - CMake configuration
- `build/` - Out-of-tree build directory

---

## Common Patterns

### Pattern 1: Struct → Class
```cpp
class StructName : public GCBase<StructName> {
private:
    // All fields private

public:
    // Inline accessors
    inline type accessorName() const noexcept { return field; }

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

---

## Key Learnings

1. **Inline functions are zero-cost** - No measurable overhead vs macros
2. **C++ can be faster** - 2.14s vs 2.17s baseline
3. **CRTP is zero-cost** - Static dispatch without vtables
4. **Encapsulation doesn't hurt performance** - Same compiled code
5. **Exceptions are efficient** - Faster than setjmp/longjmp
6. **Incremental conversion works** - Small phases with frequent testing

---

## Analysis Findings

### Project Assessment: EXCELLENT
- **Architecture**: Well-designed CRTP pattern with zero-cost abstraction
- **Performance**: 3% improvement over baseline (2.14s vs 2.17s)
- **Code Quality**: Zero warnings, 915 noexcept specifications, modern C++23
- **Documentation**: Comprehensive plans (ENCAPSULATION_PLAN.md, CONSTRUCTOR_PLAN.md)
- **Technical Debt**: LOW-MEDIUM (primarily incomplete encapsulation)

### Strengths
1. ✅ **Zero-cost modernization** - Performance improved, not degraded
2. ✅ **Type safety** - enum classes, inline constexpr, template functions
3. ✅ **Strong discipline** - 1% regression tolerance enforced
4. ✅ **Comprehensive testing** - 30+ test files in testes/
5. ✅ **Modern build system** - CMake with sanitizers, LTO, CTest integration

### Key Gaps
1. ⚠️ **68% encapsulation** - 6 classes remaining (plan exists)
2. ⚠️ **Unknown test coverage** - Need gcov/lcov integration
3. ⚠️ **~75 convertible macros** - Simple expression macros remain
4. ⚠️ **Header complexity** - Some circular dependencies

### Achievements
- **Converted ~500 macros** to inline constexpr functions
- **CRTP implementation** across all 9 GC types
- **Performance improvement** despite adding type safety
- **Zero API breakage** - Full C compatibility maintained

---

## Remaining Work

### Encapsulation (Phases 37-42)

**Phase 37: Udata0 Encapsulation**
- Risk: TRIVIAL | Time: 30 mins | Call Sites: ~5
- Status: Has constructor, just needs field verification

**Phase 38: Udata Encapsulation**
- Risk: LOW | Time: 1-2 hours | Call Sites: 10-20
- Files: lstring.cpp, lgc.cpp, lapi.cpp
- Has 9 accessors, needs 3 more (setLen, setNumUserValues, pointer accessors)

**Phase 39: FuncState Encapsulation**
- Risk: MEDIUM | Time: 2-3 hours | Call Sites: ~50-100
- Files: lcode.cpp, lparser.cpp
- Has 6 accessors, needs comprehensive encapsulation (20+ fields)

**Phase 40: LexState Encapsulation**
- Risk: MEDIUM | Time: 2-3 hours | Call Sites: ~50-100
- Files: llex.cpp, lparser.cpp
- Has 4 accessors, needs comprehensive encapsulation (11 fields)

**Phase 41: global_State Encapsulation**
- Risk: HIGH | Time: 4-6 hours | Call Sites: 100+
- Status: **Fields already private!** Just needs verification
- Has ~100 accessors already implemented
- Strategy: Batched updates by module

**Phase 42: lua_State Encapsulation**
- Risk: EXTREME | Time: 1 week | Call Sites: 200-300+
- Status: **Fields already private!** Just needs verification
- Hot path: VM interpreter, call/return handling
- Strategy: Ultra-conservative batching with micro-benchmarks

### Macro Conversion (~75 macros)

**Batch 1**: 10 simple expression macros (lcode.h) - 1 hour
**Batch 2**: 25 instruction manipulation macros (lopcodes.h) - 2-3 hours
**Batch 3**: 15 type check macros (ltm.h) - 1-2 hours
**Batch 4**: 10 character type macros (lctype.h) - 1 hour
**Batch 5**: 15 remaining simple macros - 2 hours

**Total**: 75 macros, ~8-10 hours

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
#define SETARG_A(i,v) setarg(i, v, POS_A, SIZE_A)

// After
inline constexpr int GETARG_A(Instruction i) noexcept {
    return getarg(i, POS_A, SIZE_A);
}
inline void SETARG_A(Instruction& i, int v) noexcept {
    setarg(i, v, POS_A, SIZE_A);
}
```

### Keep as Macros (Do NOT Convert)

**Token-Pasting Macros**:
```cpp
// MUST remain macro - uses token pasting (##)
#define setgcparam(g,p,v)  (g->gc##p = (v))
#define applygcparam(g,p,x)  (g->gc##p = applymul100(g->gc##p, x))
```

**Public API Macros** (C compatibility):
```cpp
// MUST remain macro - part of public C API
#define lua_call(L,n,r)  lua_callk(L, (n), (r), 0, NULL)
#define lua_pcall(L,n,r,f) lua_pcallk(L, (n), (r), (f), 0, NULL)
```

**Hot Path Complex Macros**:
```cpp
// Keep as macro - used in VM interpreter hot path
#define luaH_fastgeti(t,k,res,tag) /* ... complex multi-line ... */
```

**Configuration Macros**:
```cpp
// Keep as macro - compile-time configuration
#define LUAI_MAXSHORTLEN 40
#define LUA_IDSIZE 60
```

### Conversion Strategy

1. **Identify candidates** - Use grep to find macro definitions
2. **Batch by header** - Convert 10-20 macros at a time
3. **Preserve semantics** - Ensure exact same behavior
4. **Use constexpr** - For compile-time computation
5. **Add noexcept** - For exception safety
6. **Benchmark** - After every batch
7. **Revert if regression** - Performance > 2.21s

### Priority Order

1. **lcode.h** - Simple compiler helpers (10 macros)
2. **lopcodes.h** - Instruction manipulation (25 macros)
3. **ltm.h** - Type method helpers (15 macros)
4. **lctype.h** - Character type checks (10 macros)
5. **Remaining** - Miscellaneous simple macros (15 macros)

**Avoid**: lobject.h (complex), lgc.h (has token-pasting), lua.h/lauxlib.h (public API)

---

## Process Rules (CRITICAL)

1. **ASK before benchmarks** - Never run without permission
2. **NO automation scripts** - Use Edit/Read/Write tools only
3. **Manual editing** - No Python/shell scripts for code changes
4. **Incremental changes** - Test and benchmark after every phase
5. **Immediate revert** - If performance > 2.21s

### Architecture Rules
1. **C compatibility ONLY for public API** (lua.h, lauxlib.h, lualib.h)
2. **Internal code is pure C++** - No `#ifdef __cplusplus`
3. **Performance target**: ≤2.21s (strict)
4. **Zero C API breakage** - Public interface unchanged

---

## Quick Reference

```bash
# Build
make -C build

# Test
cd testes && ../build/lua all.lua

# Benchmark
for i in 1 2 3 4 5; do ../build/lua all.lua 2>&1 | grep "total time:"; done

# Git
git status
git log --oneline -5
git add files && git commit -m "Phase N: Description"
```

---

## Success Metrics

- ✅ 19 structs → classes (100%)
- ⏳ 13/19 classes fully encapsulated (68%) - 6 remaining
- ⏳ ~500 macros converted (37%) - 75 convertible macros remaining
- ✅ CRTP active - All 9 GC types
- ✅ Exceptions implemented
- ✅ CMake build system
- ✅ Zero warnings (-Werror)
- ✅ Performance: 2.14s (3% better than baseline!)
- ✅ All tests passing
- ✅ Zero C API breakage

**Status**: Major architectural modernization complete with performance improvement ✅
**Next**: Complete remaining encapsulation phases and macro conversion

---

**Last Updated**: Analysis and documentation update - Ready for Phases 37-42 and macro conversion
