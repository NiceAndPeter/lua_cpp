# Phase 36.2: Encapsulate lua_State - Incremental Plan

## Overview
Encapsulate 23 public fields in lua_State by adding accessor methods and updating ~375 call sites across 20 files.

**Strategy**: Work on one field group at a time, compile after each group to catch errors early.

---

## Step 1: Stack Position Fields (Most Frequent)
**Fields**: `top`, `stack`, `stack_last`, `tbclist` (type: `StkIdRel`)

### Add to lstate.h (private section + public accessors):
```cpp
private:
  StkIdRel top;
  StkIdRel stack;
  StkIdRel stack_last;
  StkIdRel tbclist;

public:
  // Stack accessors - return references to allow `.p` access
  StkIdRel& getTop() noexcept { return top; }
  const StkIdRel& getTop() const noexcept { return top; }
  void setTop(StkIdRel t) noexcept { top = t; }

  StkIdRel& getStack() noexcept { return stack; }
  const StkIdRel& getStack() const noexcept { return stack; }
  void setStack(StkIdRel s) noexcept { stack = s; }

  StkIdRel& getStackLast() noexcept { return stack_last; }
  const StkIdRel& getStackLast() const noexcept { return stack_last; }
  void setStackLast(StkIdRel sl) noexcept { stack_last = sl; }

  StkIdRel& getTbclist() noexcept { return tbclist; }
  const StkIdRel& getTbclist() const noexcept { return tbclist; }
  void setTbclist(StkIdRel tbc) noexcept { tbclist = tbc; }
```

### Update macro in lstate.h:
```cpp
#define stacksize(th)  cast_int((th)->getStackLast().p - (th)->getStack().p)
```

### Update call sites:
- `L->top` → `L->getTop()` (~150 occurrences, mostly in lapi.cpp, lvm.cpp, ldo.cpp)
- `L->stack` → `L->getStack()` (~50 occurrences)
- `L->stack_last` → `L->getStackLast()` (~20 occurrences)
- `L->tbclist` → `L->getTbclist()` (~10 occurrences)

### Build and test:
```bash
cmake --build build && cd testes && ../build/lua all.lua
```

---

## Step 2: CallInfo and Base Fields
**Fields**: `ci`, `base_ci` (already has `getCallInfo()`, keep it)

### Add to lstate.h:
```cpp
private:
  CallInfo *ci;
  CallInfo base_ci;

public:
  CallInfo* getCI() noexcept { return ci; }
  const CallInfo* getCI() const noexcept { return ci; }
  void setCI(CallInfo* c) noexcept { ci = c; }
  CallInfo** getCIPtr() noexcept { return &ci; }

  CallInfo* getBaseCI() noexcept { return &base_ci; }
  const CallInfo* getBaseCI() const noexcept { return &base_ci; }
```

### Update call sites:
- `L->ci` → `L->getCI()` (~60 occurrences)
- `&L->base_ci` → `L->getBaseCI()` (~8 occurrences)

### Build and test

---

## Step 3: GC and State Management Fields
**Fields**: `l_G`, `openupval`, `gclist`, `twups`

### Add to lstate.h:
```cpp
private:
  global_State *l_G;
  UpVal *openupval;
  GCObject *gclist;
  lua_State *twups;

public:
  global_State* getGlobalState() noexcept { return l_G; }  // Already exists
  const global_State* getGlobalState() const noexcept { return l_G; }
  void setGlobalState(global_State* g) noexcept { l_G = g; }
  global_State*& getGlobalStateRef() noexcept { return l_G; }  // For G() macro

  UpVal* getOpenUpval() noexcept { return openupval; }
  void setOpenUpval(UpVal* uv) noexcept { openupval = uv; }
  UpVal** getOpenUpvalPtr() noexcept { return &openupval; }

  GCObject* getGclist() noexcept { return gclist; }
  void setGclist(GCObject* gc) noexcept { gclist = gc; }
  GCObject** getGclistPtr() noexcept { return &gclist; }

  lua_State* getTwups() noexcept { return twups; }
  void setTwups(lua_State* tw) noexcept { twups = tw; }
  lua_State** getTwupsPtr() noexcept { return &twups; }
```

### Update G() macro:
```cpp
constexpr global_State*& G(lua_State* L) noexcept { return L->getGlobalStateRef(); }
```

### Update call sites:
- `L->openupval` → `L->getOpenUpval()` (~15 occurrences)
- `L->gclist` → `L->getGclist()` (~8 occurrences)
- `L->twups` → `L->getTwups()` (~6 occurrences)

### Build and test

---

## Step 4: Status and Error Handling Fields
**Fields**: `status`, `errorJmp`, `errfunc`

### Add to lstate.h:
```cpp
private:
  TStatus status;
  lua_longjmp *errorJmp;
  ptrdiff_t errfunc;

public:
  TStatus getStatus() const noexcept { return status; }  // Already exists
  void setStatus(TStatus s) noexcept { status = s; }

  lua_longjmp* getErrorJmp() noexcept { return errorJmp; }
  void setErrorJmp(lua_longjmp* ej) noexcept { errorJmp = ej; }
  lua_longjmp** getErrorJmpPtr() noexcept { return &errorJmp; }

  ptrdiff_t getErrFunc() const noexcept { return errfunc; }
  void setErrFunc(ptrdiff_t ef) noexcept { errfunc = ef; }
```

### Update call sites:
- `L->status` → `L->getStatus()` or `L->setStatus()` (~15 occurrences)
- `L->errorJmp` → `L->getErrorJmp()` (~5 occurrences)
- `L->errfunc` → `L->getErrFunc()` or `L->setErrFunc()` (~10 occurrences)

### Build and test

---

## Step 5: Hook and Debug Fields
**Fields**: `hook`, `hookmask`, `allowhook`, `oldpc`, `basehookcount`, `hookcount`, `transferinfo`

### Add to lstate.h:
```cpp
private:
  volatile lua_Hook hook;
  volatile l_signalT hookmask;
  lu_byte allowhook;
  int oldpc;
  int basehookcount;
  int hookcount;
  struct {
    int ftransfer;
    int ntransfer;
  } transferinfo;

public:
  lua_Hook getHook() const noexcept { return hook; }
  void setHook(lua_Hook h) noexcept { hook = h; }

  l_signalT getHookMask() const noexcept { return hookmask; }
  void setHookMask(l_signalT hm) noexcept { hookmask = hm; }

  lu_byte getAllowHook() const noexcept { return allowhook; }
  void setAllowHook(lu_byte ah) noexcept { allowhook = ah; }

  int getOldPC() const noexcept { return oldpc; }
  void setOldPC(int pc) noexcept { oldpc = pc; }

  int getBaseHookCount() const noexcept { return basehookcount; }
  void setBaseHookCount(int bhc) noexcept { basehookcount = bhc; }

  int getHookCount() const noexcept { return hookcount; }
  void setHookCount(int hc) noexcept { hookcount = hc; }
  int& getHookCountRef() noexcept { return hookcount; }  // For decrement

  // TransferInfo accessors - return reference to allow field access
  auto& getTransferInfo() noexcept { return transferinfo; }
  const auto& getTransferInfo() const noexcept { return transferinfo; }
```

### Update call sites:
- `L->hook` → `L->getHook()` or `L->setHook()` (~6 occurrences)
- `L->hookmask` → `L->getHookMask()` or `L->setHookMask()` (~10 occurrences)
- `L->allowhook` → `L->getAllowHook()` or `L->setAllowHook()` (~8 occurrences)
- `L->transferinfo.ftransfer` → `L->getTransferInfo().ftransfer` (~2 occurrences)
- etc.

### Build and test

---

## Step 6: Call Counter Fields
**Fields**: `nCcalls`, `nci`

### Add to lstate.h:
```cpp
private:
  l_uint32 nCcalls;
  int nci;

public:
  l_uint32 getNCcalls() const noexcept { return nCcalls; }
  void setNCcalls(l_uint32 nc) noexcept { nCcalls = nc; }
  l_uint32& getNCcallsRef() noexcept { return nCcalls; }  // For increment/decrement

  int getNCI() const noexcept { return nci; }
  void setNCI(int n) noexcept { nci = n; }
  int& getNCIRef() noexcept { return nci; }  // For increment/decrement

  // Non-yieldable call management (better names for incnny/decnny)
  void incrementNonYieldable() noexcept { nCcalls += 0x10000; }
  void decrementNonYieldable() noexcept { nCcalls -= 0x10000; }
```

### Update macros in lstate.h:
```cpp
#define yieldable(L)    (((L)->getNCcalls() & 0xffff0000) == 0)
#define getCcalls(L)    ((L)->getNCcalls() & 0xffff)

// Replace with method calls
#define incnny(L)       ((L)->incrementNonYieldable())
#define decnny(L)       ((L)->decrementNonYieldable())
```

### Update call sites:
- `L->nCcalls` → `L->getNCcalls()` or `L->getNCcallsRef()` (~12 occurrences)
- `L->nci` → `L->getNCI()` or `L->setNCI()` (~5 occurrences)
- `incnny(L)` / `decnny(L)` already work via macros

### Build and test

---

## Step 7: Final Cleanup and Validation

### Remove old macros (convert to inline functions):
```cpp
// Replace macros with inline functions
inline bool yieldable(const lua_State* L) noexcept {
  return ((L->getNCcalls() & 0xffff0000) == 0);
}
inline l_uint32 getCcalls(const lua_State* L) noexcept {
  return L->getNCcalls() & 0xffff;
}
inline void incnny(lua_State* L) noexcept {
  L->incrementNonYieldable();
}
inline void decnny(lua_State* L) noexcept {
  L->decrementNonYieldable();
}
```

### Final build and full test:
```bash
cmake --build build
cd testes && ../build/lua all.lua
```

### Performance check:
- Target: ≤2.50s (currently 2.46s)
- Should have zero overhead (all inline accessors)

---

## Summary
**Total changes**: ~375 call sites across 20 files
**Approach**: Incremental field-by-field with compile check after each step
**Expected result**: 100% lua_State encapsulation with zero performance impact

**Files with most changes** (tackle first):
1. lapi.cpp (~120 uses, mostly `L->top`)
2. lstate.cpp (~47 uses)
3. lvm.cpp (~48 uses)
4. ldo.cpp (~40 uses)
5. ldebug.cpp (~28 uses)
