# ⚠️ PARTIALLY COMPLETE - Aggressive Macro Elimination Plan

**Status**: ⚠️ **ONGOING** - lvm.cpp macros mostly done, ~75 remain in other files
**Last Updated**: November 2025
**Remaining**: lopcodes.h, llimits.h, lctype.h macros

---

# AGGRESSIVE MACRO ELIMINATION PLAN - lvm.cpp

**Date:** 2025-11-17
**Goal:** Convert ALL 36 remaining macros to modern C++
**Timeline:** 8-12 hours total  
**Risk Level:** MEDIUM to HIGH (performance-critical code)

---

## Executive Summary

After converting 11 VM operation macros to lambdas (with 2.1% performance GAIN), we're targeting the remaining 36 macros for elimination. This aggressive plan converts everything except true compile-time configuration.

**Target:** Convert 33/36 macros (92% conversion rate)  
**Keep:** 3 configuration macros only

---

## Phase-by-Phase Plan

### 🟢 Phase 2.1: CLEANUP - Remove Dead Code (HIGH PRIORITY)

**Target:** 11 macros (lines 991-1127)  
**Effort:** 15 minutes  
**Risk:** ZERO (dead code removal)  
**Performance Impact:** None (code not used)

**Action:** Delete original VM operation macro definitions

```cpp
// DELETE these (superseded by lambdas):
#define op_arithI(L,iop,fop) { ... }      // Line 991-1003
#define op_arithf_aux(L,v1,v2,fop) { ... } // Line 1010-1015
#define op_arithf(L,fop) { ... }          // Line 1021-1024
#define op_arithfK(L,fop) { ... }         // Line 1030-1033
#define op_arith_aux(L,v1,v2,iop,fop) { ... } // Line 1039-1045
#define op_arith(L,iop,fop) { ... }       // Line 1051-1054
#define op_arithK(L,iop,fop) { ... }      // Line 1060-1063
#define op_bitwiseK(L,op) { ... }         // Line 1069-1074
#define op_bitwise(L,op) { ... }          // Line 1083-1090
#define op_order(L,op,other) { ... }      // Line 1097-1105
#define op_orderI(L,opi,opf,inv,tm) { ... } // Line 1112-1127
```

**Reason:** These are #undef'd inside luaV_execute and replaced by lambdas. They serve no purpose.

**Benchmark:** Not required (no code changes, just deletion)

---

### 🟢 Phase 2.2: Math Constants → Constexpr (LOW RISK)

**Target:** 4 macros  
**Effort:** 30 minutes  
**Risk:** LOW (compile-time constants)  
**Performance Impact:** None expected

**Conversions:**

```cpp
// BEFORE (Line 69):
#define NBM  l_floatatt(MANT_DIG)

// AFTER:
inline constexpr int NBM = l_floatatt(MANT_DIG);

// BEFORE (Line 82):
#define MAXINTFITSF  ((lua_Unsigned)1 << NBM)

// AFTER:
inline constexpr lua_Unsigned MAXINTFITSF = (static_cast<lua_Unsigned>(1) << NBM);

// BEFORE (Line 85 or 89):
#define l_intfitsf(i)  ((MAXINTFITSF + l_castS2U(i)) <= (2 * MAXINTFITSF))
// or:
#define l_intfitsf(i)  1

// AFTER:
inline constexpr bool l_intfitsf(lua_Integer i) noexcept {
#if !defined(LUA_FLOORN2I)
    return ((MAXINTFITSF + l_castS2U(i)) <= (2 * MAXINTFITSF));
#else
    (void)i;
    return true;
#endif
}

// BEFORE (Line 832):
#define NBITS  l_numbits(lua_Integer)

// AFTER:
inline constexpr int NBITS = l_numbits(lua_Integer);
```

**Benchmark:** Quick 3-run test (expect identical performance)

**Dependencies:** None (standalone constants)

---

### 🟡 Phase 2.3: String Conversion → Inline Function (MEDIUM RISK)

**Target:** 1 macro (line 680)  
**Effort:** 1 hour  
**Risk:** MEDIUM (used in string operations)  
**Performance Impact:** Minimal expected

**Current macro:**
```cpp
#define tostring(L,o)  \
	(ttisstring(o) || (cvt2str(o) && (luaO_tostring(L, o), 1)))
```

**Conversion strategy:**

```cpp
// Option A: Inline function with short-circuit evaluation
inline bool tostring(lua_State* L, TValue* o) {
    if (ttisstring(o)) return true;
    if (!cvt2str(o)) return false;
    luaO_tostring(L, o);
    return true;
}

// Option B: Keep comma operator for exact semantics
inline bool tostring(lua_State* L, TValue* o) {
    return ttisstring(o) || (cvt2str(o) && (luaO_tostring(L, o), true));
}
```

**Analysis needed:** Check all call sites to ensure proper usage

**Benchmark:** 5-run test (string-heavy workload)

---

### 🟡 Phase 2.4: Register Access → Inline Functions (HIGHER RISK)

**Target:** 9 macros (lines 1185-1193)  
**Effort:** 2-3 hours  
**Risk:** MEDIUM-HIGH (ultra-hot path, billions of executions)  
**Performance Impact:** Critical to verify

**Current macros:**
```cpp
#define RA(i)    (base+InstructionView(i).a())
#define vRA(i)   s2v(RA(i))
#define RB(i)    (base+InstructionView(i).b())
#define vRB(i)   s2v(RB(i))
#define KB(i)    (k+InstructionView(i).b())
#define RC(i)    (base+InstructionView(i).c())
#define vRC(i)   s2v(RC(i))
#define KC(i)    (k+InstructionView(i).c())
#define RKC(i)   ((InstructionView(i).testk()) ? k + InstructionView(i).c() : s2v(base + InstructionView(i).c()))
```

**Conversion strategy - Option A: Lambda Capture (RECOMMENDED)**

These need access to `base` and `k` from luaV_execute scope. Convert to lambdas like we did with operations:

```cpp
void luaV_execute(lua_State *L, CallInfo *ci) {
    // ... existing setup ...
    
    // Register access lambdas (after base and k are initialized)
    auto RA = [&](Instruction i) -> StkId { 
        return base + InstructionView(i).a(); 
    };
    auto vRA = [&](Instruction i) -> TValue* { 
        return s2v(RA(i)); 
    };
    auto RB = [&](Instruction i) -> StkId { 
        return base + InstructionView(i).b(); 
    };
    auto vRB = [&](Instruction i) -> TValue* { 
        return s2v(RB(i)); 
    };
    auto KB = [&](Instruction i) -> const TValue* { 
        return k + InstructionView(i).b(); 
    };
    auto RC = [&](Instruction i) -> StkId { 
        return base + InstructionView(i).c(); 
    };
    auto vRC = [&](Instruction i) -> TValue* { 
        return s2v(RC(i)); 
    };
    auto KC = [&](Instruction i) -> const TValue* { 
        return k + InstructionView(i).c(); 
    };
    auto RKC = [&](Instruction i) -> const TValue* {
        return InstructionView(i).testk() 
            ? (k + InstructionView(i).c()) 
            : s2v(base + InstructionView(i).c());
    };
    
    // ... existing operation lambdas ...
    // ... main loop ...
}
```

**Why lambdas work:**
1. ✅ Same performance as macros (proven with op_arith* lambdas)
2. ✅ Type safety (return types explicit)
3. ✅ Automatic capture of base and k
4. ✅ Perfect inlining at -O3
5. ✅ No need to modify call sites (same syntax)

**Alternative - Option B: Pass base/k explicitly (NOT RECOMMENDED)**

Would require passing base and k to every lambda and modifying all call sites. Too invasive.

**Benchmark:** CRITICAL - 10-run side-by-side test
- Register access is executed billions of times
- Must verify zero performance regression
- If any regression > 1%, revert immediately

---

### 🟠 Phase 2.5: VM State Management → Inline Functions (MEDIUM RISK)

**Target:** 5 macros (lines 1197-1244)  
**Effort:** 1.5 hours  
**Risk:** MEDIUM (frequently used in error paths)  
**Performance Impact:** Moderate concern

**Current macros:**
```cpp
#define updatetrap(ci)  (trap = ci->getTrap())
#define updatebase(ci)  (base = ci->funcRef().p + 1)
#define updatestack(ci) { if (l_unlikely(trap)) { updatebase(ci); ra = RA(i); } }
#define savepc(ci)      ci->setSavedPC(pc)
#define savestate(L,ci) (savepc(ci), L->getTop().p = ci->topRef().p)
```

**Conversion strategy:**

These access outer scope variables (trap, base, pc, ra) - use lambdas:

```cpp
void luaV_execute(lua_State *L, CallInfo *ci) {
    // ... existing setup ...
    
    auto updatetrap = [&]() { trap = ci->getTrap(); };
    auto updatebase = [&]() { base = ci->funcRef().p + 1; };
    auto updatestack = [&]() { 
        if (l_unlikely(trap)) { 
            updatebase(); 
            ra = RA(i); 
        } 
    };
    auto savepc = [&]() { ci->setSavedPC(pc); };
    auto savestate = [&]() { 
        savepc(); 
        L->getTop().p = ci->topRef().p; 
    };
    
    // ... main loop ...
}
```

**Note:** These lambdas capture `trap`, `base`, `pc`, `ra`, `ci`, `L`, `i` by reference

**Benchmark:** 5-run test (focus on error handling paths)

---

### 🔴 Phase 2.6: Control Flow → Lambdas (HIGHER RISK)

**Target:** 3 macros (lines 1210-1221)  
**Effort:** 1 hour  
**Risk:** MEDIUM-HIGH (used in every branch/jump)  
**Performance Impact:** Must verify carefully

**Current macros:**
```cpp
#define dojump(ci,i,e)  { pc += InstructionView(i).sj() + e; updatetrap(ci); }
#define donextjump(ci)  { Instruction ni = *pc; dojump(ci, ni, 1); }
#define docondjump()    if (cond != InstructionView(i).k()) pc++; else donextjump(ci);
```

**Conversion strategy:**

```cpp
void luaV_execute(lua_State *L, CallInfo *ci) {
    // ... existing setup ...
    
    auto dojump = [&](Instruction instr, int e) {
        pc += InstructionView(instr).sj() + e;
        updatetrap();
    };
    
    auto donextjump = [&]() {
        Instruction ni = *pc;
        dojump(ni, 1);
    };
    
    auto docondjump = [&](int cond) {
        if (cond != InstructionView(i).k()) 
            pc++; 
        else 
            donextjump();
    };
    
    // ... main loop ...
}
```

**Call site changes needed:**
```cpp
// OLD:
docondjump();  // Uses implicit 'cond' from outer scope

// NEW:
docondjump(cond);  // Pass cond explicitly
```

**Note:** `docondjump` is used inside `op_order` and `op_orderI` lambdas! Must update those too.

**Benchmark:** 10-run side-by-side test (branching is critical path)

---

### 🔴 Phase 2.7: Exception/Error Handling → Lambdas (CRITICAL)

**Target:** 4 macros (lines 1263-1300)  
**Effort:** 2 hours  
**Risk:** HIGH (exception safety critical)  
**Performance Impact:** Error paths - moderate concern

**Current macros:**
```cpp
#define Protect(exp)      (savestate(L,ci), (exp), updatetrap(ci))
#define ProtectNT(exp)    (savepc(ci), (exp), updatetrap(ci))
#define halfProtect(exp)  (savestate(L,ci), (exp))
#define checkGC(L,c)      { luaC_condGC(L, (savepc(ci), L->getTop().p = (c)), updatetrap(ci)); luai_threadyield(L); }
```

**Challenge:** These are expression-like macros that wrap arbitrary code

**Conversion strategy - Template Lambdas:**

```cpp
void luaV_execute(lua_State *L, CallInfo *ci) {
    // ... existing setup ...
    
    // Protect: Save state, execute, update trap
    auto Protect = [&](auto&& expr) {
        savestate();
        expr();
        updatetrap();
    };
    
    // ProtectNT: Save PC only, execute, update trap  
    auto ProtectNT = [&](auto&& expr) {
        savepc();
        expr();
        updatetrap();
    };
    
    // halfProtect: Save state, execute (no trap update)
    auto halfProtect = [&](auto&& expr) {
        savestate();
        expr();
    };
    
    // checkGC: Conditional GC with state save
    auto checkGC = [&](StkId limit) {
        luaC_condGC(L, [&]() {
            savepc();
            L->getTop().p = limit;
        }, [&]() {
            updatetrap();
        });
        luai_threadyield(L);
    };
}
```

**Call site changes:**
```cpp
// OLD:
Protect(cond = other(L, ra, rb));

// NEW:
Protect([&]() { cond = other(L, ra, rb); });

// OR (if expression result needed):
cond = Protect([&]() { return other(L, ra, rb); });
```

**Major refactoring:** All Protect/halfProtect call sites must be updated (40+ locations)

**Benchmark:** EXTENSIVE - 10-run test (error handling critical)

---

### 🔴 Phase 2.8: VM Dispatch → Keep or Replace (HIGHEST RISK)

**Target:** 4 macros (lines 1282-1336)  
**Effort:** 3-4 hours (if attempted)  
**Risk:** VERY HIGH (core VM dispatch)  
**Performance Impact:** CRITICAL

**Current macros:**
```cpp
#define luai_threadyield(L)  {lua_unlock(L); lua_lock(L);}
#define vmfetch()     { if (l_unlikely(trap)) { trap = luaG_traceexec(L, pc); updatebase(ci); } i = *(pc++); }
#define vmdispatch(o) switch(o)
#define vmcase(l)     case l:
#define vmbreak       break
```

**Analysis:**

**vmfetch:**
```cpp
// Could become lambda:
auto vmfetch = [&]() {
    if (l_unlikely(trap)) {
        trap = luaG_traceexec(L, pc);
        updatebase();
    }
    i = *(pc++);
};
```

**vmdispatch/vmcase/vmbreak:**

These define the dispatch mechanism. Options:

**Option A: Keep as macros (RECOMMENDED)**
- These are fundamental VM structure
- No benefit from conversion
- Risk too high for minimal gain

**Option B: Remove macros, use direct syntax**
```cpp
// OLD:
vmdispatch (InstructionView(i).opcode()) {
    vmcase(OP_MOVE) {
        // ...
        vmbreak;
    }
}

// NEW:
switch (InstructionView(i).opcode()) {
    case OP_MOVE: {
        // ...
        break;
    }
}
```

This is trivial but changes 200+ lines of code for no real benefit.

**luai_threadyield:**

```cpp
// Could become:
inline void luai_threadyield(lua_State* L) {
    lua_unlock(L);
    lua_lock(L);
}
```

**Recommendation:** 
- Convert `luai_threadyield` → inline function (5 min, ZERO risk)
- Convert `vmfetch` → lambda (30 min, LOW risk)
- **KEEP** vmdispatch/vmcase/vmbreak (no benefit, high effort)

**Benchmark:** If vmfetch converted, 10-run test

---

## Summary Table

| Phase | Target | Macros | Effort | Risk | Benchmark |
|-------|--------|--------|--------|------|-----------|
| 2.1 | Dead code removal | 11 | 15 min | ZERO | No |
| 2.2 | Math constants | 4 | 30 min | LOW | Quick |
| 2.3 | String conversion | 1 | 1 hr | MED | 5-run |
| 2.4 | Register access | 9 | 2-3 hr | MED-HIGH | 10-run |
| 2.5 | State management | 5 | 1.5 hr | MED | 5-run |
| 2.6 | Control flow | 3 | 1 hr | MED-HIGH | 10-run |
| 2.7 | Exception handling | 4 | 2 hr | HIGH | 10-run |
| 2.8 | VM dispatch | 2 | 30 min | MED | 10-run |
| **TOTAL** | **Convertible** | **33** | **8-12 hr** | **MIXED** | **Required** |

**Keep as-is:**
- Configuration macros (3): `lvm_c`, `LUA_CORE`, `LUA_USE_JUMPTABLE`
- Dispatch macros (2): `vmdispatch`, `vmcase`, `vmbreak` (optional - could remove but low value)

---

## Execution Strategy

### Aggressive Approach (RECOMMENDED)

Execute phases in order, with mandatory benchmarking between phases:

1. ✅ **Phase 2.1** - Immediate (dead code cleanup)
2. ✅ **Phase 2.2** - Low risk (math constants)
3. ⚠️ **Phase 2.3** - Medium risk (string conversion)
4. 🛑 **CHECKPOINT** - Benchmark, verify zero regression
5. ⚠️ **Phase 2.4** - HIGH RISK (register access)
6. 🛑 **CRITICAL CHECKPOINT** - Extensive benchmark
7. ⚠️ **Phase 2.5** - Medium risk (state management)
8. ⚠️ **Phase 2.6** - Medium risk (control flow)
9. 🛑 **CHECKPOINT** - Benchmark
10. ⚠️ **Phase 2.7** - HIGH RISK (exception handling)
11. 🛑 **FINAL CHECKPOINT** - Comprehensive benchmark
12. ⚠️ **Phase 2.8** - Optional (dispatch macros)

### Rollback Strategy

**At each checkpoint:**
1. Run side-by-side benchmark (macro vs lambda)
2. If regression > 1%, **IMMEDIATELY REVERT**
3. Document why conversion failed
4. Keep previous phase changes

### Success Criteria

**Must achieve:**
- ✅ Zero compiler warnings
- ✅ All tests pass
- ✅ Performance within 1% of macro version
- ✅ No increase in variance

**Nice to have:**
- ✅ Performance improvement (like we got with op_arith* lambdas)
- ✅ Lower variance
- ✅ Cleaner code

---

## Expected Outcomes

### Best Case (60% probability)

- ✅ All 33 macros converted
- ✅ Performance neutral or slightly better
- ✅ Dramatically improved code quality
- ✅ Complete modernization of lvm.cpp

### Likely Case (30% probability)

- ⚠️ 25-30 macros converted (76-91%)
- ⚠️ Register access or exception handling causes issues
- ⚠️ Some macros kept for performance
- ✅ Still significant improvement

### Worst Case (10% probability)

- ❌ Critical performance regression in Phase 2.4 or 2.7
- ❌ Only 15-20 macros converted (45-61%)
- ❌ Must keep hot-path macros
- ⚠️ Still better than original macro version

---

## Risk Mitigation

### High-Risk Phases (2.4, 2.7)

1. **Prototype first** - Test with minimal changes
2. **Incremental conversion** - Convert 1-2 macros at a time
3. **Extensive benchmarking** - 20+ runs if needed
4. **Assembly analysis** - Check generated code
5. **Profiling** - Use perf to identify hot spots

### Performance Validation

Each phase must pass:
- ✅ Side-by-side benchmark (interleaved execution)
- ✅ Statistical significance (10+ runs)
- ✅ Variance check (similar or lower)
- ✅ Test suite pass (all tests)

### Code Review Points

Before each phase:
- ✅ Verify capture semantics correct
- ✅ Check for unintended copies
- ✅ Confirm noexcept where appropriate
- ✅ Review generated assembly (for critical paths)

---

## Tools and Techniques

### Benchmarking Script

Reuse `testes/bench_compare.sh` for side-by-side comparisons

### Assembly Analysis

```bash
# Generate assembly for hot functions
g++ -S -O3 -std=c++23 -o lvm_macro.s src/vm/lvm.cpp
# Compare with lambda version
diff -u lvm_macro.s lvm_lambda.s
```

### Profiling

```bash
# Profile hot paths
perf record -g ./lua all.lua
perf report
```

### Static Analysis

```bash
# Check for unintended copies
clang-tidy --checks='performance-*' src/vm/lvm.cpp
```

---

## Conclusion

This aggressive plan targets **33/36 macros (92%)** for conversion, leaving only true configuration macros.

**Key insights from op_arith* lambda conversion:**
1. ✅ Modern compilers optimize lambdas excellently
2. ✅ Type safety helps (not hurts) performance
3. ✅ Side-by-side benchmarking is CRITICAL
4. ✅ Zero-cost abstractions are REAL

**Timeline:** 8-12 hours with proper benchmarking  
**Expected result:** 25-33 macros converted (76-100%)  
**Performance target:** Within ±1% of macro version

**This would make lvm.cpp one of the most modern VM implementations in any language runtime!**

---

**Plan created by:** Claude (AI Assistant)  
**Date:** 2025-11-17  
**Branch:** claude/analyze-lv-018LEz1SVgM57AT2HW11UTsi  
**Status:** Ready for execution
