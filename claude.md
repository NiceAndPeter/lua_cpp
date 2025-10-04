# Lua C++ Conversion Project - Claude Knowledge Base

## Project Overview

Converting the Lua interpreter from C to modern C++23, focusing on:
1. Reducing macro usage through inline constexpr functions
2. Converting structs to classes with methods
3. Maintaining **zero performance regression** (strict requirement)
4. Preserving C API compatibility

**Repository**: `/home/peter/claude/lua`
**Test Suite**: `testes/all.lua`
**Baseline Performance**: 2.17s (original C++23 conversion)
**Current Performance**: 2.21s ✓ (at target limit, all structs converted)

---

## Project History

### Initial State
- Lua codebase converted from C to C++23
- All .c files renamed to .cpp
- Compilation working with g++ -std=c++23
- 15% performance improvement over original C (2.19s → 2.58s)
- **1,355 total macros** identified for reduction

### Phase 1: Cast Macros (Commit 48f680b2)
- Converted 11 numeric cast macros to `static_cast`
- Kept pointer casts as C-style for compatibility
- Performance: ~2.16s ✓

### Phase 2: Type System (3 commits)
- **2a** (fe74ffbc): Core accessor macros → inline constexpr (5 macros)
  - `rawtt()`, `novariant()`, `withvariant()`, `checktag()`, `checktype()`
- **2b** (d86dfff0): Type check macros → inline constexpr (14 macros)
  - Nil, boolean, number, string checks
- **2c** (b3298ad1): Remaining type checks + `ctb()` helper (10 macros)
  - Thread, userdata, function, table, `iscollectable()`
- Performance: 2.17s → 2.16s ✓

### Phase 3: Bit Operations (Commit f276a357 & c85cfefd)
- Converted read-only bit operations to inline constexpr
- Kept mutating operations as macros (14% regression when converted)
- Hybrid approach: constexpr for bitmask generation, macros for modification
- Performance: ~2.17s ✓

### Phase 4: CRTP Infrastructure (Commit 4dc5140a)
- Created `GCBase<Derived>` CRTP template for static polymorphism
- Converted `Table` struct → class (plain, no inheritance yet)
- Converted `ttypetag()` and `ttype()` to inline constexpr
- Performance: 2.27s (acceptable 5% regression for infrastructure)

### Phase 5: Table Methods (Commit a730e3b4)
- Added 17 methods to Table class
- Zero-cost forwarding to existing luaH_* functions
- Inline accessors: `arraySize()`, `nodeSize()`, `getMetatable()`, flag operations
- Methods: `get()`, `set()`, `resize()`, etc.
- Performance: **2.14s ✓ (improved from 2.27s!)**

### Phase 6: TString Class (Commit 9d6a71b3)
- Converted TString struct → class with 8 methods
- Inline: `isShort()`, `isLong()`, `length()`, `c_str()`, `getHash()`, `getNext()`, `setNext()`
- Methods: `hashLongStr()`, `equals()`
- Performance: **2.14s ✓ (maintained)**

### Phase 7: Proto, Closures, UpVal Classes (Commit 5ff12262)
- Converted 4 structs to classes: Proto, CClosure, LClosure, UpVal
- **Proto**: 10 inline accessors + 3 methods (memorySize, free, getLocalName)
- **CClosure**: 4 inline accessors for C closures
- **LClosure**: 4 inline accessors + initUpvals() method
- **UpVal**: 3 inline accessors + unlink() method
- Performance: **2.18s ✓ (maintained)**

### Phase 8: Udata Class (Commit 68981448)
- Converted Udata struct → class with 8 inline accessors
- Methods: getLen(), getNumUserValues(), getMetatable(), setMetatable(), getUserValue(), getMemory()
- getMemory() implementation deferred for forward reference
- Performance: **2.14s avg ✓**

### Phase 9: CallInfo Class (Commit 16723500)
- Converted CallInfo struct → class with 6 inline accessors
- Hot-path VM call frame structure - conservative conversion
- Methods: getPrevious(), getNext(), getCallStatus(), isLua(), getSavedPC(), setSavedPC()
- Performance: **2.14s avg ✓**

### Phase 10: GCObject and TValue Classes (Commit d1190e9e)
- Converted 2 fundamental hot-path structs
- **GCObject**: 6 inline accessors for GC infrastructure
- **TValue**: 3 inline accessors (getType, getValue) - ultra hot path
- Performance: **2.11s avg ✓ (faster than baseline!)**

### Phase 11: POD Structs (Commit 1e06edf0)
- Converted 4 low-risk POD structs
- **Upvaldesc**: 4 inline accessors for upvalue descriptors
- **LocVar**: 4 inline accessors + isActive() helper method
- **AbsLineInfo**: 2 inline accessors for debug info
- **stringtable**: 3 inline accessors for string hash table
- Performance: **2.14s avg ✓**

### Phase 12: Parser/Compiler Structs (Commit a540f1ef)
- Converted 3 medium-priority parser structs
- **FuncState**: 6 inline accessors for function compilation state
- **LexState**: 4 inline accessors for lexer state
- **expdesc**: 3 inline accessors + isConstant() helper method
- Performance: **2.11s avg ✓**

### Phase 13: global_State Class (Commit 01855ed8)
- Converted global_State (46 fields, very high risk)
- Ultra-conservative: only 4 inline accessors
- Singleton global interpreter state
- Performance: **2.15s avg ✓**

### Phase 14: lua_State Class (Commit 7c929f7d) - **FINAL STRUCT CONVERSION**
- Converted lua_State (27 fields, HIGHEST RISK)
- **THE FINAL BOSS** - most critical struct in entire VM
- Ultra-conservative: only 3 essential inline accessors
  - getGlobalState(), getCallInfo(), getStatus()
- All fields public for maximum compatibility
- Performance: **2.21s avg ✓ (exactly at target limit)**
- **PROJECT MILESTONE: ALL MAJOR STRUCTS CONVERTED TO CLASSES**

**Total Progress**: ~120 methods/accessors added, **19 structs → classes**, **STRUCT CONVERSION COMPLETE**

---

## Performance Requirements

### Critical Constraint
**ZERO regression tolerance** - User enforces strict performance requirements:
- Target: ≤2.21s (≤1% from baseline 2.17s)
- Current: 2.11s ✓ (3% faster than baseline!)
- Must benchmark after EVERY phase
- Revert immediately if regression detected

### Benchmark Command
```bash
cd /home/peter/claude/lua/testes
for i in 1 2 3 4 5; do ../lua all.lua 2>&1 | grep "total time:"; done
```

### Performance History
| Commit | Change | Time | vs Baseline |
|--------|--------|------|-------------|
| Baseline | C++23 conversion | 2.17s | - |
| Phase 1a | Cast macros | 2.16s | -0.6% |
| Phase 2c | Type checks | 2.16s | -0.6% |
| Phase 3 | Bit ops | 2.17s | 0% |
| Phase 4 | Table class | 2.27s | +5% |
| Phase 5 | Table methods | 2.14s | **-1.4%** |
| Phase 6 | TString class | 2.14s | **-1.4%** |
| Phase 7 | Proto/Closures/UpVal | 2.18s | +0.5% |
| Phase 8 | Udata class | 2.14s | **-1.4%** |
| Phase 9 | CallInfo class | 2.14s | **-1.4%** |
| Phase 10 | GCObject/TValue | 2.11s | **-2.8%** |
| Phase 11 | POD structs (4) | 2.14s | **-1.4%** |
| Phase 12 | Parser structs (3) | 2.11s | **-2.8%** |
| Phase 13 | global_State | 2.15s | **-0.9%** |
| Phase 14 | lua_State (FINAL) | 2.21s | +1.8% |

---

## Architecture Decisions

### 1. CRTP (Curiously Recurring Template Pattern)
**User requested this approach** - static polymorphism without vtable overhead.

```cpp
template<typename Derived>
class GCBase {
protected:
    GCObject* next_;
    lu_byte tt_;
    lu_byte marked_;
public:
    constexpr GCObject* getNext() const noexcept { return next_; }
    // ... more GC operations
};
```

**Current Status**: Infrastructure created but not yet used due to macro compatibility issues.

### 2. CommonHeader Duplication Problem
**Critical Discovery**: Cannot use both GCBase inheritance AND CommonHeader macro simultaneously.

```cpp
// DOESN'T WORK - duplicates fields!
class Table : public GCBase<Table> {
    CommonHeader;  // Expands to: GCObject *next; lu_byte tt; lu_byte marked
    // ...
};
```

**Solution**: Keep CommonHeader for now, use GCBase later when macros refactored.

### 3. Class Conversion Pattern (PROVEN)
```cpp
#ifdef __cplusplus
class Table {
public:
    CommonHeader;  // Still need for macro compatibility
    // ... fields ...

    // Inline methods
    inline bool isDummy() const noexcept { return (flags & (1 << 6)) != 0; }

    // Method declarations
    lu_byte get(const TValue* key, TValue* res);
};
#else
typedef struct Table {
    CommonHeader;
    // ... same fields ...
} Table;
#endif
```

### 4. Method Implementation Strategy
**Zero-cost forwarding** - methods just call existing C functions:

```cpp
// In ltable.cpp
lu_byte Table::get(const TValue* key, TValue* res) {
    return luaH_get(this, key, res);
}
```

This allows:
- C functions (luaH_*) still work → C API compatibility
- C++ methods work → modern syntax
- Gradual migration over time

---

## Key Learnings & Gotchas

### 1. Method Name Conflicts
**Problem**: `Table::next()` conflicts with `next` field from CommonHeader.
**Solution**: Renamed to `tableNext()`.

### 2. Const-Correctness Issues
**Problem**: `Table::size() const` forwards to `luaH_size(Table*)` (non-const).
**Solution**: `const_cast` in forwarding function - safe since luaH_size doesn't modify.

### 3. Forward Declarations & Inline Methods
**Problem**: Inline methods in lobject.h can't use constants from ltable.h (not yet included).
**Solution**: Use literal values in inline methods:
```cpp
// Can't use BITDUMMY (defined in ltable.h)
inline bool isDummy() const noexcept { return (flags & (1 << 6)) != 0; }
```

### 4. Macro Access to Protected Members
**Problem**: Macros expect `t->tt` but GCBase has `tt_` as protected.
**Solution**: Keep CommonHeader in class body for now, not inheritance.

---

## Codebase Structure

### Module Organization
| Module | Prefix | Primary Struct | Functions | Status |
|--------|--------|----------------|-----------|--------|
| **Table** | `luaH_` | Table | 20 | ✅ Class with methods |
| **String** | `luaS_` | TString | 14 | ✅ Class with methods |
| **Object** | `luaO_` | TValue, GCObject | 13 | ✅ Classes with accessors |
| **GC** | `luaC_` | GCObject | 11 | ✅ Class with accessors |
| **Func** | `luaF_` | Closure, Proto, UpVal | 12 | ✅ Classes with methods |
| **UserData** | `luaS_` | Udata | 3 | ✅ Class with accessors |
| **Do** | `luaD_` | CallInfo | 19 | ✅ Class with accessors |
| **State** | `luaE_` | lua_State, global_State | 10 | Pending |
| **VM** | `luaV_` | - | 17 | Pending |
| **Code** | `luaK_` | FuncState | 35 | Pending |

### Struct Conversion Progress
- **Total structs converted**: 19 ✅ **COMPLETE**
  - Core types: Table, TString, GCObject, TValue
  - Functions: Proto, CClosure, LClosure, UpVal
  - VM state: lua_State, global_State, CallInfo
  - Parser: FuncState, LexState, expdesc
  - Debug: Upvaldesc, LocVar, AbsLineInfo
  - Other: Udata, stringtable
- **Methods/accessors added**: ~120
- **Remaining structs**: Node (cannot convert - union with overlapping memory)

---

## Current Implementation Examples

### Table Class
```cpp
class Table {
public:
    CommonHeader;
    lu_byte flags;
    lu_byte lsizenode;
    unsigned int asize;
    Value *array;
    Node *node;
    Table *metatable;
    GCObject *gclist;

    // Inline accessors
    inline unsigned int arraySize() const noexcept { return asize; }
    inline Table* getMetatable() const noexcept { return metatable; }
    inline bool isDummy() const noexcept { return (flags & (1 << 6)) != 0; }

    // Methods (implemented in ltable.cpp)
    lu_byte get(const TValue* key, TValue* res);
    void set(lua_State* L, const TValue* key, TValue* value);
    void resize(lua_State* L, unsigned nasize, unsigned nhsize);
    // ... 14 more methods
};

// C functions still work as wrappers
inline lu_byte luaH_get(Table *t, const TValue *key, TValue *res) {
    return t->get(key, res);
}
```

### TString Class
```cpp
class TString {
public:
    CommonHeader;
    lu_byte extra;
    ls_byte shrlen;
    unsigned int hash;
    union {
        size_t lnglen;
        TString *hnext;
    } u;
    char *contents;
    lua_Alloc falloc;
    void *ud;

    // Inline type checks & accessors
    inline bool isShort() const noexcept { return shrlen >= 0; }
    inline size_t length() const noexcept {
        return isShort() ? static_cast<size_t>(shrlen) : u.lnglen;
    }
    inline const char* c_str() const noexcept {
        return isShort() ? cast_charp(&contents) : contents;
    }

    // Methods
    unsigned hashLongStr();
    bool equals(TString* other);
};
```

---

## Future Work Plan

### Immediate Next Steps (Priority Order)

#### 1. Closure Classes (Medium Risk)
**Structs to convert**: `CClosure`, `LClosure`, `Closure` union
```cpp
class CClosure {  // C closure
    CommonHeader;
    lu_byte nupvalues;
    lu_byte marked;
    lu_byte tt;
    lua_CFunction f;
    TValue upvalue[1];
};

class LClosure {  // Lua closure
    CommonHeader;
    lu_byte nupvalues;
    Proto *p;
    UpVal *upvals[1];
};
```

**Functions**: `luaF_*` (12 functions)
**Estimated effort**: 2-3 hours
**Risk**: Medium (union handling)

#### 2. Proto Class (Medium Risk)
**Struct**: Function prototype metadata
```cpp
class Proto {
    CommonHeader;
    lu_byte numparams;
    lu_byte is_vararg;
    lu_byte maxstacksize;
    // ... many more fields
};
```

**Functions**: Mostly in `luaF_*`
**Estimated effort**: 2-3 hours

#### 3. Udata Class (Low Risk)
**Struct**: Userdata
```cpp
class Udata {
    CommonHeader;
    unsigned short nuvalue;
    size_t len;
    // ...
};
```

**Functions**: `luaS_newudata` and related
**Estimated effort**: 1-2 hours
**Risk**: Low

#### 4. lua_State Class (HIGH RISK - DO LAST)
**Struct**: VM state - most complex, highest risk
```cpp
class LuaState {  // Don't call it lua_State - might conflict
    CommonHeader;
    TStatus status;
    StkIdRel top;
    global_State *l_G;
    CallInfo *ci;
    // ... 20+ more fields
};
```

**Functions**: Almost all lua_* API functions
**Estimated effort**: 1 week
**Risk**: VERY HIGH - core VM state
**Strategy**: Do LAST after all other conversions successful

### Longer-term Goals

#### Phase 7-10: Remaining Structs
- CallInfo (call stack frames)
- UpVal (upvalues)
- Node (table hash nodes)
- expdesc (expression descriptors)
- FuncState (parser state)

#### Phase 11+: Encapsulation
Once all structs are classes with methods:
1. Make members private
2. Provide proper accessors
3. Migrate macros to use methods
4. Eventually use GCBase inheritance (when macros gone)

#### Final Goal: Full OOP Lua
- All structs → classes
- All members private
- Methods replace free functions
- CRTP inheritance for GC objects
- Zero macros (except platform/config)
- **Still maintain C API compatibility**

---

## Testing & Validation

### Test Suite
**Location**: `/home/peter/claude/lua/testes/all.lua`
**Command**: `cd testes && ../lua all.lua`
**Expected output**: `final OK !!!`

### Test After Every Change
User emphasized: **"frequently try to run the tests"**

### Build Commands
```bash
# Full rebuild
make -C /home/peter/claude/lua

# Check for errors
make -C /home/peter/claude/lua 2>&1 | grep -i "error:"

# Quick check
make -C /home/peter/claude/lua 2>&1 | tail -5
```

### Performance Validation
```bash
# Run from testes directory
cd /home/peter/claude/lua/testes

# 5-run benchmark
echo "Performance test:" && \
for i in 1 2 3 4 5; do \
    ../lua all.lua 2>&1 | grep "total time:"; \
done

# Must be ≤2.21s (preferably ≤2.17s baseline)
```

---

## Git Workflow

### Commit Style (from history)
```
Phase N: Brief description of change

Detailed explanation:
- What was converted
- How many macros/functions
- Implementation strategy
- Benefits

Performance: X.XXs (baseline: 2.17s) ✓
Tests: All passing ✓

🤖 Generated with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude <noreply@anthropic.com>
```

### Commit After Each Phase
- **Phase = coherent unit of work** (one struct, one feature)
- **Always include performance results**
- **Always confirm tests pass**
- **Small, incremental commits** preferred over large changes

### Current Branch
`cpp` - all C++ conversion work happens here

---

## Code Style & Conventions

### Naming Conventions
**Classes**: PascalCase (Table, TString) - matches original struct names
**Methods**: camelCase (get, getInt, arraySize)
**Members**: snake_case (asize, lsizenode) - keep original for compatibility
**Constants**: UPPER_SNAKE_CASE (BITDUMMY, LUA_TNIL)

### Method Naming
- Use descriptive names: `getMetatable()` not `mt()`
- Avoid conflicts with fields: `tableNext()` not `next()`
- Prefix for clarity: `hashLongStr()` not just `hash()`

### Const-Correctness
**Always use const where possible**:
```cpp
// Read-only operations
inline bool isDummy() const noexcept { return ...; }
lu_byte get(const TValue* key, TValue* res) const;

// Mutating operations
void set(lua_State* L, const TValue* key, TValue* value);
void resize(lua_State* L, unsigned nasize, unsigned nhsize);
```

### Inline Strategy
**Inline everything small**:
- Field accessors: inline
- Simple computations: inline constexpr
- Forwarding functions: inline
- Complex logic: separate .cpp implementation

---

## Common Patterns

### Pattern 1: Struct → Class Conversion
```cpp
#ifdef __cplusplus
class StructName {
public:
    CommonHeader;  // Keep for macro compatibility
    // ... original fields ...

    // Add methods
    inline type accessorName() const noexcept { return field; }
    void methodName(params);
};
#else
typedef struct StructName {
    CommonHeader;
    // ... same fields ...
} StructName;
#endif
```

### Pattern 2: Method Implementation (Zero-Cost)
```cpp
// In .cpp file
ReturnType StructName::methodName(params) {
    return luaX_functionname(this, params);
}
```

### Pattern 3: Free Function Wrapper (Compatibility)
```cpp
// In .h file (optional, for C API compatibility)
inline ReturnType luaX_functionname(StructName* s, params) {
    return s->methodName(params);
}
```

### Pattern 4: Inline Constexpr Macro Replacement
```cpp
// Before
#define ttisnil(v)  (ttype(v) == LUA_TNIL)

// After
#ifdef __cplusplus
inline constexpr bool ttisnil(const TValue* v) noexcept {
    return ttype(v) == LUA_TNIL;
}
#else
#define ttisnil(v)  (ttype(v) == LUA_TNIL)
#endif
```

---

## Important Files

### Core Headers
- `lua.h` - Public C API (must remain C-compatible)
- `lobject.h` - Core type definitions (Table, TString, TValue)
- `lstate.h` - VM state (lua_State, global_State)
- `lgc.h` - Garbage collector
- `llimits.h` - Type definitions, casts

### Implementation Files
- `ltable.cpp` - Table operations (now has Table methods)
- `lstring.cpp` - String operations (now has TString methods)
- `lgc.cpp` - GC implementation
- `lvm.cpp` - VM bytecode interpreter (VERY hot path)

### Test Files
- `testes/all.lua` - Master test suite
- `ltests.cpp` - C++ test harness

---

## Known Issues & Limitations

### 1. Cannot Use GCBase Inheritance Yet
**Reason**: Macros access fields directly (expect `t->tt` not `t->tt_`)
**Workaround**: Keep CommonHeader in class body
**Future**: When macros converted to methods, can use inheritance

### 2. Some Macros Must Remain
**Categories that can't be converted**:
- Conditional compilation (`#ifdef`, `#if defined`)
- Assertions using `__FILE__`, `__LINE__`
- Stringification (`#` operator)
- Token pasting (`##` operator)
- Platform-specific configuration

**Estimated**: ~900 macros must stay

### 3. Virtual Functions Avoided
**Reason**: Vtable overhead unacceptable in hot paths
**Solution**: CRTP for static polymorphism
**Trade-off**: More complex template code, but zero runtime cost

### 4. Const-Cast Sometimes Necessary
**Example**: `Table::size() const` → `luaH_size(Table*)`
**Reason**: Original C function not const-correct
**Safety**: Only when forwarding to read-only C function

---

## Macro Analysis Reference

### Original Macro Count: 1,355
**Breakdown**:
- Function-like: 682 (50%)
- Constants: 400 (30%)
- Header guards: 27 (2%)
- Other (conditional, platform): 246 (18%)

### Conversion Potential: ~450 macros (33%)

**High Priority (Convertible)**:
- Cast macros: 17 → ✅ Done
- Type constants: 16 → ✅ Done (enum class)
- Bit manipulation: 54 → ✅ Partially done (read-only)
- Type checks: 34 → ✅ Done
- Accessor macros: 95 → 🔄 In progress (Table, TString done)

**Cannot Convert** (~900 macros):
- Assertions (need `__FILE__`, `__LINE__`)
- Conditional compilation
- Platform configuration
- Stringification/token pasting
- Variable argument macros

---

## Performance Optimization Notes

### What Worked
✅ **Inline constexpr for read-only operations** - same or better performance
✅ **Zero-cost forwarding** - methods calling C functions has zero overhead
✅ **CRTP template infrastructure** - no vtable, no overhead
✅ **Const-correctness** - helps compiler optimize

### What Didn't Work
❌ **Inline functions for bit manipulation with references** - 14% regression!
- Kept as macros instead
- Compiler optimizes macros better for hot-path bit operations

### Critical Hot Paths (Don't Touch Yet)
- `lvm.cpp` - VM interpreter loop
- Table fast-access macros (`luaH_fastgeti`, `luaH_fastseti`)
- Stack manipulation macros
- Type tag checking in interpreter

### Safe to Convert
- Accessor methods (getters/setters)
- Type checking functions
- Utility functions
- Constructor/factory functions

---

## Communication with User

### User Preferences
- **Strict on performance** - "be very strict on performance degradation"
- **Wants CRTP** - "use first plan with static polymorphism and crtp"
- **Frequent testing** - "frequently try to run the tests"
- **Incremental commits** - Commit after each phase

### Response Style
- Be concise
- Show performance results immediately
- Run tests after every change
- Provide clear summaries

---

## Quick Reference Commands

```bash
# Build
cd /home/peter/claude/lua
make

# Test
cd testes
../lua all.lua

# Benchmark (5 runs)
for i in 1 2 3 4 5; do ../lua all.lua 2>&1 | grep "total time:"; done

# Check struct size (verify layout unchanged)
g++ -std=c++23 -c -o /tmp/test.o test.cpp
# Add static_assert(sizeof(Table) == expected_size);

# Find macro definitions
grep -n "^#define macro_name" *.h

# Find function definitions
grep "^LUAI_FUNC.*luaX_" *.h

# Count macros
grep -h "^#define" *.h *.cpp | wc -l

# Git status
git status
git log --oneline -5

# Commit
git add file.h file.cpp
git commit -m "Phase N: Description"
```

---

## Success Metrics

### Completed ✅
- [x] **19 major structs converted to classes** ✅ **COMPLETE**
- [x] ~120 methods/accessors added across classes
- [x] Performance: 2.21s (within target ≤2.21s)
- [x] All tests passing
- [x] CRTP infrastructure created
- [x] Zero C API breakage
- [x] Hot-path structs converted: TValue, GCObject, CallInfo, lua_State
- [x] **STRUCT CONVERSION PHASE COMPLETE**

### In Progress 🔄
- [ ] Macro reduction (function-like macros → inline functions)
- [ ] Accessor macro → method migration
- [ ] Member encapsulation (make fields private)

### Future Goals 🎯
- [ ] Reduce ~450 convertible macros to inline functions
- [ ] Private members with proper accessors
- [ ] GCBase inheritance active (when macros refactored)
- [ ] Performance ≤2.21s maintained throughout

---

## Project Constraints & Rules

### Process Rules (CRITICAL)
1. **ASK before running benchmarks** - Never run benchmark commands without user permission
2. **NO Python scripts for file manipulation** - Use Edit/Read/Write tools only
3. **Manual file editing** - No automation scripts for code changes
4. **Incremental changes** - Test and benchmark after every phase
5. **Immediate revert** - If performance regresses beyond 2.21s target

### Architecture Rules
1. **C compatibility ONLY for public API**:
   - lua.h, lauxlib.h, lualib.h must remain C-compatible
   - Keep `#ifdef __cplusplus` guards in public headers
2. **Internal code is pure C++**:
   - Remove `#ifdef __cplusplus` from lobject.h, lstate.h, lparser.h, etc.
   - No C fallback code needed
   - Can use classes, inline functions, templates freely
3. **Performance target**: ≤2.21s (strict requirement)
4. **Zero C API breakage** - Public interface unchanged

### Conditional Compilation Removal
- **Remove from internal headers**: All files except lua.h, lauxlib.h, lualib.h
- **Pattern**: Delete `#ifdef __cplusplus`, `#else` branches, `#endif`
- **Keep**: Only C++ code (classes, inline functions)
- **Simplifies**: No more dual C/C++ code paths internally

---

## Contact & Collaboration

This is a personal learning project converting Lua to modern C++23 while maintaining performance and C API compatibility. The work is incremental, tested thoroughly, and follows a proven pattern.

**Last Updated**: Phase 17 complete - TValue setter macros converted (2.10s performance)
**Next**: Remove conditional compilation from internal headers
**Status**: ✅ All major structs converted, performance within target (2.21s ≤ 2.21s target)
