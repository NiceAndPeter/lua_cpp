# Constructor Pattern Plan - GC Object Allocation

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
- Build after each phase
- Run full test suite
- Benchmark (must stay ≤2.21s)
- Commit each phase separately
