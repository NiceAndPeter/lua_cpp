# Encapsulation Continuation Plan

## IMPORTANT: Commit After Every Phase! ⚠️

After completing each encapsulation phase:
1. ✅ Build successfully
2. ✅ Run full test suite
3. ✅ **COMMIT immediately with descriptive message**
4. Move to next phase

This ensures clean history and easy rollback if needed.

---

## Current Status Summary

**Fully Encapsulated (13/19 classes - 68%):**
1. ✅ LocVar - All fields private
2. ✅ AbsLineInfo - All fields private
3. ✅ Upvaldesc - All fields private
4. ✅ stringtable - All fields private
5. ✅ GCObject - Protected fields (base class)
6. ✅ TString - All fields private
7. ✅ Table - All fields private
8. ✅ Proto - All fields private (Phase 32)
9. ✅ UpVal - All fields private (Phase 33)
10. ✅ CClosure - All fields private (Phase 34)
11. ✅ LClosure - All fields private (Phase 34)
12. ✅ expdesc - All fields private (Phase 35) ⬅️ JUST COMPLETED
13. ✅ CallInfo - All fields private (Phase 36) ⬅️ JUST COMPLETED

**Remaining Classes (6):**
- Udata (low risk)
- Udata0 (trivial)
- FuncState (medium risk)
- LexState (medium risk)
- global_State (high risk)
- lua_State (HIGHEST risk)

**Current Performance**: 2.11s (test suite, 3% better than 2.17s baseline!)
**Performance Target**: ≤2.21s (strict requirement)

---

## Phase 37: FuncState Encapsulation

**Risk Level**: MEDIUM
**Estimated Time**: 2-3 hours
**Estimated Call Sites**: ~50-100
**Files**: `src/compiler/lcode.cpp`, `src/compiler/lparser.cpp`

Currently has 6 inline accessors, need to make all fields private and add comprehensive accessors.

---

## Phase 38: LexState Encapsulation

**Risk Level**: MEDIUM
**Estimated Time**: 2-3 hours
**Estimated Call Sites**: ~50-100
**Files**: `src/compiler/llex.cpp`, `src/compiler/lparser.cpp`

Currently has 4 inline accessors, need to make all fields private and add comprehensive accessors.

---

## Phase 39: Udata Encapsulation

**Risk Level**: LOW
**Estimated Time**: 1-2 hours
**Estimated Call Sites**: 10-20

### Current State
```cpp
class Udata : public GCBase<Udata> {
public:  // ← NEED TO MAKE PRIVATE
  unsigned short nuvalue;
  size_t len;
  struct Table *metatable;
  GCObject *gclist;
  UValue uv[1];
```

### Target State
```cpp
class Udata : public GCBase<Udata> {
private:
  unsigned short nuvalue;
  size_t len;
  Table *metatable;
  GCObject *gclist;
  UValue uv[1];

public:
  // Existing accessors (keep)
  size_t getLen() const noexcept { return len; }
  unsigned short getNumUserValues() const noexcept { return nuvalue; }
  Table* getMetatable() const noexcept { return metatable; }
  void setMetatable(Table* mt) noexcept { metatable = mt; }
  UValue* getUserValue(int idx) noexcept { return &uv[idx]; }
  const UValue* getUserValue(int idx) const noexcept { return &uv[idx]; }
  void* getMemory() noexcept;
  const void* getMemory() const noexcept;

  // New accessors needed
  void setLen(size_t l) noexcept { len = l; }
  void setNumUserValues(unsigned short n) noexcept { nuvalue = n; }

  GCObject* getGclist() noexcept { return gclist; }
  void setGclist(GCObject* gc) noexcept { gclist = gc; }
  GCObject** getGclistPtr() noexcept { return &gclist; }

  // For initialization (luaS_newudata)
  Table** getMetatablePtr() noexcept { return &metatable; }
};
```

### Update Strategy
1. Add missing accessors to Udata class in lobject.h
2. Make fields private
3. Update call sites:
   - `src/objects/lstring.cpp` (luaS_newudata, udata2finalize)
   - `src/memory/lgc.cpp` (GC traversal)
   - `src/core/lapi.cpp` (API functions)
4. Build, test, benchmark
5. Commit if successful

---

## Phase 40: global_State Encapsulation

**Risk Level**: HIGH
**Estimated Time**: 4-6 hours
**Estimated Call Sites**: 100+

### Current State
```cpp
class global_State {
public:  // ← 46+ FIELDS ALL PUBLIC
  lua_Alloc frealloc;
  void *ud;
  l_mem GCtotalbytes;
  l_mem GCdebt;
  // ... 42 more fields
```

### Target State
```cpp
class global_State {
private:  // ← ALL FIELDS PRIVATE
  lua_Alloc frealloc;
  void *ud;
  l_mem GCtotalbytes;
  l_mem GCdebt;
  stringtable strt;
  TValue l_registry;
  TValue nilvalue;
  unsigned int seed;
  lu_byte gcparams[LUA_GCPN];
  lu_byte currentwhite;
  lu_byte gcstate;
  lu_byte gckind;
  lu_byte gcstopem;
  lu_byte gcstp;
  lu_byte gcemergency;
  // GC object lists
  GCObject *allgc;
  GCObject **sweepgc;
  GCObject *finobj;
  GCObject *gray;
  GCObject *grayagain;
  GCObject *weak;
  GCObject *ephemeron;
  GCObject *allweak;
  GCObject *tobefnz;
  GCObject *fixedgc;
  GCObject *survival;
  GCObject *old1;
  GCObject *reallyold;
  GCObject *firstold1;
  GCObject *finobjsur;
  GCObject *finobjold1;
  GCObject *finobjrold;
  lua_State *twups;
  lua_CFunction panic;
  TString *memerrmsg;
  TString *tmname[TM_N];
  Table *mt[LUA_NUMTYPES];
  TString *strcache[STRCACHE_N][STRCACHE_M];
  lua_WarnFunction warnf;
  void *ud_warn;
  LX mainth;

public:
  // Memory allocator
  lua_Alloc getAllocator() const noexcept { return frealloc; }
  void setAllocator(lua_Alloc f) noexcept { frealloc = f; }
  void* getUserData() const noexcept { return ud; }
  void setUserData(void* data) noexcept { ud = data; }

  // GC memory counters (need reference accessors)
  l_mem getTotalBytes() const noexcept { return GCtotalbytes; }
  void setTotalBytes(l_mem bytes) noexcept { GCtotalbytes = bytes; }
  l_mem& getTotalBytesRef() noexcept { return GCtotalbytes; }

  l_mem getDebt() const noexcept { return GCdebt; }
  void setDebt(l_mem debt) noexcept { GCdebt = debt; }
  l_mem& getDebtRef() noexcept { return GCdebt; }

  l_mem getMarked() const noexcept { return GCmarked; }
  void setMarked(l_mem m) noexcept { GCmarked = m; }
  l_mem& getMarkedRef() noexcept { return GCmarked; }

  l_mem getMajorMinor() const noexcept { return GCmajorminor; }
  void setMajorMinor(l_mem mm) noexcept { GCmajorminor = mm; }
  l_mem& getMajorMinorRef() noexcept { return GCmajorminor; }

  // String table
  stringtable* getStringTable() noexcept { return &strt; }
  const stringtable* getStringTable() const noexcept { return &strt; }

  // Registry and special values
  TValue* getRegistry() noexcept { return &l_registry; }
  const TValue* getRegistry() const noexcept { return &l_registry; }
  TValue* getNilValue() noexcept { return &nilvalue; }
  const TValue* getNilValue() const noexcept { return &nilvalue; }

  // Random seed
  unsigned int getSeed() const noexcept { return seed; }
  void setSeed(unsigned int s) noexcept { seed = s; }
  unsigned int& getSeedRef() noexcept { return seed; }

  // GC parameters
  lu_byte getGCParam(int idx) const noexcept { return gcparams[idx]; }
  void setGCParam(int idx, lu_byte val) noexcept { gcparams[idx] = val; }
  lu_byte* getGCParams() noexcept { return gcparams; }

  // GC state
  lu_byte getCurrentWhite() const noexcept { return currentwhite; }
  void setCurrentWhite(lu_byte w) noexcept { currentwhite = w; }
  lu_byte& getCurrentWhiteRef() noexcept { return currentwhite; }

  lu_byte getGCState() const noexcept { return gcstate; }
  void setGCState(lu_byte state) noexcept { gcstate = state; }
  lu_byte& getGCStateRef() noexcept { return gcstate; }

  lu_byte getGCKind() const noexcept { return gckind; }
  void setGCKind(lu_byte kind) noexcept { gckind = kind; }

  lu_byte getGCStopEM() const noexcept { return gcstopem; }
  void setGCStopEM(lu_byte stop) noexcept { gcstopem = stop; }
  lu_byte& getGCStopEMRef() noexcept { return gcstopem; }

  lu_byte getGCStp() const noexcept { return gcstp; }
  void setGCStp(lu_byte stp) noexcept { gcstp = stp; }

  lu_byte getGCEmergency() const noexcept { return gcemergency; }
  void setGCEmergency(lu_byte em) noexcept { gcemergency = em; }

  // GC object lists (need both value and pointer accessors)
  GCObject* getAllGC() const noexcept { return allgc; }
  void setAllGC(GCObject* gc) noexcept { allgc = gc; }
  GCObject** getAllGCPtr() noexcept { return &allgc; }

  GCObject** getSweepGC() const noexcept { return sweepgc; }
  void setSweepGC(GCObject** sweep) noexcept { sweepgc = sweep; }
  GCObject*** getSweepGCPtr() noexcept { return &sweepgc; }

  GCObject* getFinObj() const noexcept { return finobj; }
  void setFinObj(GCObject* gc) noexcept { finobj = gc; }
  GCObject** getFinObjPtr() noexcept { return &finobj; }

  GCObject* getGray() const noexcept { return gray; }
  void setGray(GCObject* gc) noexcept { gray = gc; }
  GCObject** getGrayPtr() noexcept { return &gray; }

  GCObject* getGrayAgain() const noexcept { return grayagain; }
  void setGrayAgain(GCObject* gc) noexcept { grayagain = gc; }
  GCObject** getGrayAgainPtr() noexcept { return &grayagain; }

  GCObject* getWeak() const noexcept { return weak; }
  void setWeak(GCObject* gc) noexcept { weak = gc; }
  GCObject** getWeakPtr() noexcept { return &weak; }

  GCObject* getEphemeron() const noexcept { return ephemeron; }
  void setEphemeron(GCObject* gc) noexcept { ephemeron = gc; }
  GCObject** getEphemeronPtr() noexcept { return &ephemeron; }

  GCObject* getAllWeak() const noexcept { return allweak; }
  void setAllWeak(GCObject* gc) noexcept { allweak = gc; }
  GCObject** getAllWeakPtr() noexcept { return &allweak; }

  GCObject* getToBeFnz() const noexcept { return tobefnz; }
  void setToBeFnz(GCObject* gc) noexcept { tobefnz = gc; }
  GCObject** getToBeFnzPtr() noexcept { return &tobefnz; }

  GCObject* getFixedGC() const noexcept { return fixedgc; }
  void setFixedGC(GCObject* gc) noexcept { fixedgc = gc; }
  GCObject** getFixedGCPtr() noexcept { return &fixedgc; }

  // Generational GC lists
  GCObject* getSurvival() const noexcept { return survival; }
  void setSurvival(GCObject* gc) noexcept { survival = gc; }
  GCObject** getSurvivalPtr() noexcept { return &survival; }

  GCObject* getOld1() const noexcept { return old1; }
  void setOld1(GCObject* gc) noexcept { old1 = gc; }
  GCObject** getOld1Ptr() noexcept { return &old1; }

  GCObject* getReallyOld() const noexcept { return reallyold; }
  void setReallyOld(GCObject* gc) noexcept { reallyold = gc; }
  GCObject** getReallyOldPtr() noexcept { return &reallyold; }

  GCObject* getFirstOld1() const noexcept { return firstold1; }
  void setFirstOld1(GCObject* gc) noexcept { firstold1 = gc; }
  GCObject** getFirstOld1Ptr() noexcept { return &firstold1; }

  GCObject* getFinObjSur() const noexcept { return finobjsur; }
  void setFinObjSur(GCObject* gc) noexcept { finobjsur = gc; }
  GCObject** getFinObjSurPtr() noexcept { return &finobjsur; }

  GCObject* getFinObjOld1() const noexcept { return finobjold1; }
  void setFinObjOld1(GCObject* gc) noexcept { finobjold1 = gc; }
  GCObject** getFinObjOld1Ptr() noexcept { return &finobjold1; }

  GCObject* getFinObjROld() const noexcept { return finobjrold; }
  void setFinObjROld(GCObject* gc) noexcept { finobjrold = gc; }
  GCObject** getFinObjROldPtr() noexcept { return &finobjrold; }

  // Thread list
  lua_State* getTwups() const noexcept { return twups; }
  void setTwups(lua_State* th) noexcept { twups = th; }
  lua_State** getTwupsPtr() noexcept { return &twups; }

  // Panic handler
  lua_CFunction getPanic() const noexcept { return panic; }
  void setPanic(lua_CFunction p) noexcept { panic = p; }

  // Memory error message
  TString* getMemErrMsg() const noexcept { return memerrmsg; }
  void setMemErrMsg(TString* msg) noexcept { memerrmsg = msg; }
  TString** getMemErrMsgPtr() noexcept { return &memerrmsg; }

  // Tag method names
  TString* getTMName(int tm) const noexcept { return tmname[tm]; }
  void setTMName(int tm, TString* name) noexcept { tmname[tm] = name; }
  TString** getTMNamePtr(int tm) noexcept { return &tmname[tm]; }

  // Metatables
  Table* getMetatable(int type) const noexcept { return mt[type]; }
  void setMetatable(int type, Table* t) noexcept { mt[type] = t; }
  Table** getMetatablePtr(int type) noexcept { return &mt[type]; }

  // String cache
  TString* getStrCache(int n, int m) const noexcept { return strcache[n][m]; }
  void setStrCache(int n, int m, TString* s) noexcept { strcache[n][m] = s; }
  TString** getStrCachePtr(int n, int m) noexcept { return &strcache[n][m]; }

  // Warning function
  lua_WarnFunction getWarnF() const noexcept { return warnf; }
  void setWarnF(lua_WarnFunction w) noexcept { warnf = w; }
  void* getUDWarn() const noexcept { return ud_warn; }
  void setUDWarn(void* ud) noexcept { ud_warn = ud; }

  // Main thread
  LX* getMainThread() noexcept { return &mainth; }
  const LX* getMainThread() const noexcept { return &mainth; }
};
```

### Update Strategy (Batched)
1. Add all ~100 accessors to global_State class
2. Make fields private
3. Update call sites in batches:
   - **Batch 1**: `src/memory/lgc.cpp` (GC list manipulation - most accesses)
   - **Batch 2**: `src/core/lstate.cpp` (initialization/cleanup)
   - **Batch 3**: `src/objects/lstring.cpp` (string table access)
   - **Batch 4**: `src/core/lapi.cpp` (API functions)
   - **Batch 5**: Remaining files
4. Build and test after EACH batch
5. Final benchmark after all batches
6. Commit if performance ≤2.21s

**Critical**: Use pointer accessors (e.g., `getAllGCPtr()`) in GC code to avoid copies.

---

## Phase 41: lua_State Encapsulation

**Risk Level**: EXTREME ⚠️
**Estimated Time**: 1 week
**Estimated Call Sites**: 200-300+

### Current State
```cpp
class lua_State : public GCBase<lua_State> {
public:  // ← 27 FIELDS ALL PUBLIC (MOST CRITICAL CLASS)
  lu_byte allowhook;
  TStatus status;
  StkIdRel top;
  global_State *l_G;
  CallInfo *ci;
  // ... 22 more fields
```

### Target State
```cpp
class lua_State : public GCBase<lua_State> {
private:  // ← ALL FIELDS PRIVATE
  lu_byte allowhook;
  TStatus status;
  StkIdRel top;
  global_State *l_G;
  CallInfo *ci;
  StkIdRel stack_last;
  StkIdRel stack;
  UpVal *openupval;
  StkIdRel tbclist;
  GCObject *gclist;
  lua_State *twups;
  lua_longjmp *errorJmp;
  CallInfo base_ci;
  volatile lua_Hook hook;
  ptrdiff_t errfunc;
  l_uint32 nCcalls;
  int oldpc;
  int nci;
  int basehookcount;
  int hookcount;
  volatile l_signalT hookmask;
  struct {
    int ftransfer;
    int ntransfer;
  } transferinfo;

public:
  // Keep existing 3 accessors
  global_State* getGlobalState() const noexcept { return l_G; }
  CallInfo* getCallInfo() const noexcept { return ci; }
  TStatus getStatus() const noexcept { return status; }

  // Stack accessors (reference for hot paths)
  StkIdRel& topRef() noexcept { return top; }
  const StkIdRel& topRef() const noexcept { return top; }

  StkIdRel& stackRef() noexcept { return stack; }
  const StkIdRel& stackRef() const noexcept { return stack; }

  StkIdRel& stackLastRef() noexcept { return stack_last; }
  const StkIdRel& stackLastRef() const noexcept { return stack_last; }

  // CallInfo (hot path)
  CallInfo*& ciRef() noexcept { return ci; }
  CallInfo* const& ciRef() const noexcept { return ci; }
  void setCallInfo(CallInfo* newci) noexcept { ci = newci; }

  // Allow hook (hot path)
  lu_byte getAllowHook() const noexcept { return allowhook; }
  void setAllowHook(lu_byte ah) noexcept { allowhook = ah; }
  lu_byte& allowHookRef() noexcept { return allowhook; }

  // Status
  void setStatus(TStatus st) noexcept { status = st; }

  // Open upvalues
  UpVal* getOpenUpval() const noexcept { return openupval; }
  void setOpenUpval(UpVal* uv) noexcept { openupval = uv; }
  UpVal** getOpenUpvalPtr() noexcept { return &openupval; }

  // TBC list
  StkIdRel& tbclistRef() noexcept { return tbclist; }
  const StkIdRel& tbclistRef() const noexcept { return tbclist; }

  // GC list
  GCObject* getGclist() const noexcept { return gclist; }
  void setGclist(GCObject* gc) noexcept { gclist = gc; }
  GCObject** getGclistPtr() noexcept { return &gclist; }

  // Thread list
  lua_State* getTwups() const noexcept { return twups; }
  void setTwups(lua_State* th) noexcept { twups = th; }
  lua_State** getTwupsPtr() noexcept { return &twups; }

  // Error jump
  lua_longjmp* getErrorJmp() const noexcept { return errorJmp; }
  void setErrorJmp(lua_longjmp* ej) noexcept { errorJmp = ej; }
  lua_longjmp** getErrorJmpPtr() noexcept { return &errorJmp; }

  // Base call info
  CallInfo* getBaseCI() noexcept { return &base_ci; }
  const CallInfo* getBaseCI() const noexcept { return &base_ci; }

  // Hook
  lua_Hook getHook() const noexcept { return hook; }
  void setHook(lua_Hook h) noexcept { hook = h; }
  volatile lua_Hook& getHookRef() noexcept { return hook; }

  // Error function
  ptrdiff_t getErrFunc() const noexcept { return errfunc; }
  void setErrFunc(ptrdiff_t ef) noexcept { errfunc = ef; }
  ptrdiff_t& getErrFuncRef() noexcept { return errfunc; }

  // nCcalls (hot path - may need reference)
  l_uint32 getNCcalls() const noexcept { return nCcalls; }
  void setNCcalls(l_uint32 n) noexcept { nCcalls = n; }
  l_uint32& nCcallsRef() noexcept { return nCcalls; }

  // Old PC
  int getOldPC() const noexcept { return oldpc; }
  void setOldPC(int pc) noexcept { oldpc = pc; }
  int& oldPCRef() noexcept { return oldpc; }

  // NCI (call info count)
  int getNCI() const noexcept { return nci; }
  void setNCI(int n) noexcept { nci = n; }
  int& nciRef() noexcept { return nci; }

  // Hook counts
  int getBaseHookCount() const noexcept { return basehookcount; }
  void setBaseHookCount(int c) noexcept { basehookcount = c; }
  int& baseHookCountRef() noexcept { return basehookcount; }

  int getHookCount() const noexcept { return hookcount; }
  void setHookCount(int c) noexcept { hookcount = c; }
  int& hookCountRef() noexcept { return hookcount; }

  // Hook mask
  l_signalT getHookMask() const noexcept { return hookmask; }
  void setHookMask(l_signalT m) noexcept { hookmask = m; }
  volatile l_signalT& getHookMaskRef() noexcept { return hookmask; }

  // Transfer info
  int getFTransfer() const noexcept { return transferinfo.ftransfer; }
  void setFTransfer(int ft) noexcept { transferinfo.ftransfer = ft; }
  int& fTransferRef() noexcept { return transferinfo.ftransfer; }

  int getNTransfer() const noexcept { return transferinfo.ntransfer; }
  void setNTransfer(int nt) noexcept { transferinfo.ntransfer = nt; }
  int& nTransferRef() noexcept { return transferinfo.ntransfer; }

  // Keep all existing methods (30+)
};
```

### Update Strategy (ULTRA CONSERVATIVE)

**CRITICAL**: Must benchmark after EVERY small batch (10-20 call sites)

1. Add ~40 accessors to lua_State class
2. Make fields private
3. Update call sites in VERY SMALL batches:

   **Phase 37a**: Non-hot paths (50-70 call sites)
   - `src/core/lapi.cpp` (non-critical API functions)
   - `src/libraries/*.cpp` (standard libraries)
   - Build, test, **benchmark** ✓

   **Phase 37b**: Medium-hot paths (50-70 call sites)
   - `src/core/lapi.cpp` (critical API functions)
   - `src/auxiliary/lauxlib.cpp`
   - Build, test, **benchmark** ✓

   **Phase 37c**: Hot path - ldo.cpp (50-70 call sites)
   - `src/core/ldo.cpp` (call/return/error handling)
   - **BENCHMARK AFTER EVERY 10-20 CHANGES** ⚠️
   - Build, test, **benchmark** ✓

   **Phase 37d**: ULTRA HOT - lvm.cpp (30-50 call sites)
   - `src/vm/lvm.cpp` (VM interpreter loop)
   - **THIS IS THE MOST CRITICAL FILE**
   - **BENCHMARK AFTER EVERY 5-10 CHANGES** ⚠️⚠️⚠️
   - Use reference accessors exclusively
   - Zero-cost abstraction is MANDATORY
   - Build, test, **benchmark** ✓

4. **If ANY batch shows regression > 2.21s: REVERT IMMEDIATELY**

5. Final comprehensive benchmark (10 runs minimum)

6. Commit only if performance ≤2.21s

---

## Success Criteria

**Phase 37 (FuncState):**
- ✅ All fields private
- ✅ Tests pass
- ✅ Performance ≤2.21s
- ✅ Commit immediately

**Phase 38 (LexState):**
- ✅ All fields private
- ✅ Tests pass
- ✅ Performance ≤2.21s
- ✅ Commit immediately

**Phase 39 (Udata):**
- ✅ All fields private
- ✅ Tests pass
- ✅ Performance ≤2.21s
- ✅ Commit immediately

**Phase 40 (global_State):**
- ✅ All 46+ fields private
- ✅ Tests pass
- ✅ Performance ≤2.21s
- ✅ Commit immediately

**Phase 41 (lua_State):**
- ✅ All 27 fields private
- ✅ Tests pass
- ✅ Performance ≤2.21s
- ✅ **100% ENCAPSULATION COMPLETE** 🎉
- ✅ Commit immediately

---

## Performance Monitoring

**Current**: 2.14s (3% better than 2.17s baseline)
**Target**: ≤2.21s (≤1% regression from baseline)

**Benchmark command:**
```bash
cd /home/peter/claude/lua/testes
for i in 1 2 3 4 5; do ../build/lua all.lua 2>&1 | grep "total time:"; done
```

**Frequency:**
- Udata: After completion
- Parser classes: After each class
- global_State: After each batch
- lua_State: After EVERY 5-20 call sites (depending on hotness)

---

## Risks and Mitigations

**Risk 1**: Performance regression in VM hot paths
- **Mitigation**: Use reference accessors, inline everything, benchmark frequently

**Risk 2**: Many call sites to update (300+)
- **Mitigation**: Batch updates, test after each batch, ready to revert

**Risk 3**: Complex field access patterns (pointer-to-pointer)
- **Mitigation**: Provide both value and pointer accessors

**Risk 4**: Union field access in CallInfo, UpVal
- **Mitigation**: Already handled, use existing pattern

---

## Completion Timeline

- **Phase 37 (FuncState)**: 2-3 hours
- **Phase 38 (LexState)**: 2-3 hours
- **Phase 39 (Udata)**: 1-2 hours
- **Phase 40 (global_State)**: 4-6 hours
- **Phase 41 (lua_State)**: 1 week (very careful, incremental)

**Total Estimated**: 2-3 weeks for complete encapsulation

**Current Progress**: 68% (13/19 classes)
**Remaining**: 32% (6 classes)

---

**Last Updated**: After Phase 36 (CallInfo encapsulation)
**Next Step**: Phase 37 (FuncState encapsulation)
