# Phases 138-139: [[nodiscard]] & Const Correctness

**Date**: 2025-12-11
**Status**: Phase 138 ✅ Complete | Phase 139 ⏭️ Skipped (already complete)
**Performance**: ~2.25s avg (47% faster than baseline)

---

## Phase 138: Additional [[nodiscard]] Annotations ✅

### Overview
Added `[[nodiscard]]` to 6 core functions where ignoring the return value is likely a bug.

### Changes Made

| Function | Location | Rationale |
|----------|----------|-----------|
| `luaG_findlocal()` | ldebug.h | Returns variable name - ignoring defeats lookup purpose |
| `luaG_addinfo()` | ldebug.h | Returns formatted error message - always used |
| `luaT_objtypename()` | ltm.h | Returns type name - pointless to ignore |
| `luaT_gettm()` | ltm.h | Returns metamethod - lookup pointless if ignored |
| `luaT_gettmbyobj()` | ltm.h | Returns metamethod - same rationale |
| `luaE_extendCI()` | lstate.h | Returns new CallInfo* - must use result |

**Total**: 96 → 102 [[nodiscard]] annotations

### Key Decision: NOT Added to pushfstring

`luaO_pushfstring()` and `luaO_pushvfstring()` were intentionally **NOT** annotated because they have legitimate dual use:

**Pattern 1 - Return value used:**
```cpp
const char* msg = luaO_pushfstring(L, "error: %s", details);
```

**Pattern 2 - Side effect only (pushing on stack):**
```cpp
luaO_pushfstring(L, "error in %s", name);
L->doThrow(LUA_ERRSYNTAX);  // Value pushed on stack, pointer not needed
```

### Results
- ✅ All tests pass
- ✅ Performance: ~2.25s avg (no regression)
- ✅ Build clean, zero warnings
- ✅ 3 files modified (headers only)

**Commit**: `571d6b8e`

---

## Phase 139: Const Correctness Survey ⏭️

### Overview
Comprehensive survey of the codebase revealed **excellent const-correctness already in place** from previous incremental work. No changes needed.

### Survey Results

#### VirtualMachine ✅ Excellent
```cpp
// All query methods already const:
[[nodiscard]] int tonumber(const TValue *obj, lua_Number *n) const;
[[nodiscard]] int tointeger(const TValue *obj, lua_Integer *p, F2Imod mode) const;
[[nodiscard]] int lessThan(const TValue *l, const TValue *r) const;
[[nodiscard]] int equalObj(const TValue *t1, const TValue *t2) const;
// ... all conversion/comparison/arithmetic methods const
```

#### Table ✅ Excellent
```cpp
// All getters have const/non-const overloads:
Value* getArray() noexcept;
const Value* getArray() const noexcept;

std::span<Value> getArraySpan() noexcept;
std::span<const Value> getArraySpan() const noexcept;

// All query methods const:
[[nodiscard]] LuaT get(const TValue* key, TValue* res) const;
[[nodiscard]] LuaT getInt(lua_Integer key, TValue* res) const;
```

#### Proto ✅ Excellent
```cpp
// All accessors const:
lu_byte getNumParams() const noexcept;
int getCodeSize() const noexcept;
Instruction* getCode() const noexcept;

// All span accessors have const overloads:
std::span<Instruction> getCodeSpan() noexcept;
std::span<const Instruction> getCodeSpan() const noexcept;
```

#### UpVal, CClosure, LClosure ✅ Excellent
```cpp
// All getters have const/non-const pairs:
TValue* getValue() noexcept;
const TValue* getValue() const noexcept;

const TValue* getUpvalue(int idx) const noexcept;
TValue* getUpvalue(int idx) noexcept;
```

### What's NOT Const (Correctly)

Methods that genuinely modify state:
- **Setters**: `setFlags()`, `setArraySize()`, etc.
- **VM execution**: `execute()`, `finishOp()`
- **Table modification**: `pset*()`, `set*()`, `resize()`
- **GC operations**: `unlink()`, `remove()`, `normalize()`
- **Stack ops**: `concat()`, `objlen()`

### Conclusion

**No work needed** - const-correctness is already excellent! Previous incremental improvements (Phases 47, 126, and earlier) already achieved comprehensive const-correctness.

This demonstrates the value of **incremental const-correctness work** - by adding const as we went, we avoided the need for a large dedicated phase later.

---

## Key Learnings

1. **[[nodiscard]] decision-making**: Not all functions with return values need it - functions with legitimate dual-use (return value + side effects) should NOT be annotated

2. **Incremental const work pays off**: Previous phases adding const incrementally meant Phase 139 found no work needed - a validation of the incremental approach

3. **Survey value**: Even when no changes are made, documenting that the codebase is already correct has value

---

## Files Modified

**Phase 138**:
- `src/core/ldebug.h` (2 functions)
- `src/core/ltm.h` (3 functions)
- `src/core/lstate.h` (1 function)

**Phase 139**: None (survey only)

---

**Phases**: 138 ✅, 139 ⏭️ | **Next**: Phase 140 (GC Loop Iterators)
