# Lua C++ Conversion Project - Essential Guide

## Project Overview

Converting Lua 5.5 from C to modern C++23 with:
- Zero performance regression (strict requirement)
- C API compatibility preserved
- CRTP for static polymorphism
- Full encapsulation with private fields

**Repository**: `/home/peter/claude/lua`
**Performance**: 2.14s ✓ (3% better than 2.17s baseline!)
**Status**: All 19 structs converted, 9+ fully encapsulated

---

## Current Status

### Completed ✅
- **19 structs → classes**: Table, TString, Proto, UpVal, CClosure, LClosure, Udata, lua_State, global_State, CallInfo, GCObject, TValue, FuncState, LexState, expdesc, LocVar, AbsLineInfo, Upvaldesc, stringtable
- **9+ classes fully encapsulated** with private fields: LocVar, AbsLineInfo, Upvaldesc, stringtable, GCObject, TString, Table, Proto, UpVal
- **~500 macros converted** to inline functions/methods
- **CRTP inheritance active** - GCBase<Derived> for all GC objects
- **CommonHeader eliminated** - Pure C++ inheritance
- **C++ exceptions** - Replaced setjmp/longjmp
- **Modern CMake** - Build system
- **Organized source tree** - Logical subdirectories
- **Zero warnings** - Compiles with -Werror

### In Progress 🔄
- Continue encapsulating remaining classes (CClosure, LClosure, CallInfo, lua_State, global_State)

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
- ✅ 9+ classes fully encapsulated (50%+)
- ✅ ~500 macros converted (37% of convertible)
- ✅ CRTP active
- ✅ Exceptions implemented
- ✅ CMake build system
- ✅ Zero warnings (-Werror)
- ✅ Performance: 2.14s (3% better than baseline!)
- ✅ All tests passing
- ✅ Zero C API breakage

**Status**: Major architectural modernization complete with performance improvement ✅

---

**Last Updated**: Phase 33 (Encapsulation) - UpVal complete
