# ✅ HISTORICAL - Constructor Pattern Plan (COMPLETED)

**Status**: ✅ **COMPLETE** - All GC objects use constructor pattern
**Completion Date**: November 2025
**Result**: Constructor pattern with placement new operators fully implemented

---

# Constructor Pattern Plan - GC Object Allocation

## Status: ✅ COMPLETED

All GC object types now use constructor pattern with placement new operators. Full test suite passes.

## Overview
Convert `luaF_*` and `luaS_*` allocation functions to constructor pattern with placement new operators for type-safe GC allocation.

## Pattern

```cpp
class CClosure : public GCBase<CClosure> {
public:
    // Member placement new operator for GC allocation
    static void* operator new(size_t size, lua_State* L, lu_byte tt) {
        return luaC_newobj(L, tt, size);
    }

    // For variable-size allocation (optional)
    static void* operator new(size_t size, lua_State* L, lu_byte tt, size_t extra) {
        return luaC_newobj(L, tt, size + extra);
    }

    // Constructor
    CClosure(int nupvals);

    // Factory method
    static CClosure* create(lua_State* L, int nupvals) {
        size_t extra = (nupvals - 1) * sizeof(TValue);
        CClosure* c = new (L, LUA_VCCL, extra) CClosure(nupvals);
        return c;
    }
};

// Old function becomes wrapper for compatibility
CClosure* luaF_newCclosure(lua_State* L, int nupvals) {
    return CClosure::create(L, nupvals);
}
```

## Classes to Convert

### Phase 34a: CClosure
```cpp
CClosure::CClosure(int nupvals) {
    this->nupvalues = cast_byte(nupvals);
    this->gclist = NULL;
    this->f = NULL;
}
```

### Phase 34b: LClosure
```cpp
LClosure::LClosure(int nupvals) {
    this->nupvalues = cast_byte(nupvals);
    this->p = NULL;
    this->gclist = NULL;
}
```

### Phase 34c: Proto
```cpp
Proto::Proto() {
    // Initialize all 14+ fields to NULL/0
}

Proto* Proto::create(lua_State* L) {
    return new (L, LUA_VPROTO) Proto();
}
```

### Phase 34d: Udata
```cpp
Udata::Udata(size_t len, unsigned short nuvalue) {
    this->len = len;
    this->nuvalue = nuvalue;
    this->metatable = NULL;
}

Udata* Udata::create(lua_State* L, size_t s, unsigned short nuvalue) {
    size_t extra = s + nuvalue * sizeof(TValue);
    return new (L, LUA_VUSERDATA, extra) Udata(s, nuvalue);
}
```

### Phase 34e: TString
```cpp
// Short string
TString::TString(unsigned int hash, ls_byte shrlen) {
    this->hash = hash;
    this->shrlen = shrlen;
    this->extra = 0;
}

// Long string
TString::TString(unsigned int hash, size_t lnglen, ls_byte kind) {
    this->hash = hash;
    this->shrlen = -1;
    this->u.lnglen = lnglen;
    this->extra = kind;
}
```

### Phase 34f: UpVal
```cpp
UpVal::UpVal(TValue* val) {
    this->v = val;
}

UpVal* UpVal::create(lua_State* L, StkId level) {
    UpVal* uv = new (L, LUA_VUPVAL) UpVal(level);
    uv->v = level;  // Open upvalue points to stack
    return uv;
}
```

## Benefits
- ✅ Type-safe GC allocation via `new (L, type) Class(args)`
- ✅ lua_State dependency explicit in operator signature
- ✅ RAII - Constructors guarantee initialization
- ✅ Encapsulation - GC allocation logic in class
- ✅ Zero runtime cost - Placement new has no overhead

## Testing
- ✅ Build after each phase
- ✅ Run full test suite (all.lua) - **PASSED**
- ✅ Benchmark (must stay ≤2.21s) - **2.12s**
- Commit each phase separately

## Implementation Notes - Variable-Size Objects

### Critical Issue: Field Initialization for Variable-Size Types

Both TString and Udata use variable-size allocation where `allocated_size < sizeof(Class)` for certain variants. This creates a critical constraint: **you cannot initialize fields that don't exist in the allocated memory**.

### TString Variants and Memory Layout

```cpp
// TString has 3 variants with different memory layouts:

1. Short strings (LUA_VSHRSTR):
   Size = contentsOffset() + length + 1
   Fields: GCObject, extra, shrlen, hash, u
   String data inline at offset 32 (where contents field would be)

2. LSTRFIX (fixed external):
   Size = 40 bytes (fallocOffset())
   Fields: GCObject, extra, shrlen, hash, u, contents
   NO falloc or ud fields!

3. LSTRMEM (managed external):
   Size = sizeof(TString) = 56 bytes
   Fields: GCObject, extra, shrlen, hash, u, contents, falloc, ud
   All fields exist
```

**Bug Fix**: Original code initialized falloc and ud for ALL long strings, but LSTRFIX only allocates 40 bytes. Writing to falloc (offset 40) wrote to guard bytes! Solution: Only initialize contents for long strings; falloc/ud are initialized by caller when needed.

### Udata Variants and Memory Layout

```cpp
// Udata has 2 variants:

1. Udata0 (nuvalue == 0):
   Size = offsetof(Udata0, bindata) + data_length
   Fields: GCObject, nuvalue, len, metatable, bindata
   NO gclist field!

2. Udata (nuvalue > 0):
   Size = uvOffset() + (sizeof(UValue) * nuvalue) + data_length
   Fields: GCObject, nuvalue, len, metatable, gclist, uv[]
   gclist field exists at offset 32
```

**Bug Fix**: Original code initialized gclist for ALL Udata, but Udata0 doesn't have this field. For Udata0 with len=0, size=32, writing to gclist (offset 32) wrote to guard bytes! Solution: Only initialize gclist when `nuvalue > 0`.

### General Rule for Variable-Size Objects

**Do not use constructors** for variable-size objects. Instead:
1. Call `luaC_newobj` directly with exact size
2. Manually initialize only the fields that exist in allocated memory
3. Use conditional initialization based on variant type
4. Let callers initialize optional fields when needed

### Memory Corruption Debugging Tips

1. Test allocator guard bytes are invaluable for detecting buffer overruns
2. Variable-size bugs manifest as writing to addresses beyond allocation
3. Pattern: Last N bytes of allocation + first M guard bytes zeroed
4. Check struct layouts with offsetof() to understand field positions
5. For 8-byte pointer fields, corruption often shows as 8 consecutive zeros
