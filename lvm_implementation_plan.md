# lvm.cpp Modernization - Detailed Implementation Plan

**Created:** 2025-11-16
**Status:** Ready for implementation
**Total Estimated Time:** 30-43 hours (excluding P7)

---

## Time Summary

| Phase | Description | Estimated Time | Risk Level |
|-------|-------------|----------------|------------|
| **Phase 1** | Foundation (Priorities 1 & 4) | **6-9 hours** | ✅ LOW |
| **Phase 2** | Macro Conversion (Priority 2) | **12-16 hours** | ⚠️ MEDIUM |
| **Phase 3** | Code Quality (Priorities 3 & 6) | **8-12 hours** | ✅ LOW |
| **Phase 4** | Polish (Priority 5, optional) | **4-6 hours** | ✅ LOW |
| **TOTAL** | All recommended work | **30-43 hours** | - |

**Conservative estimate:** 43 hours (~5-6 days of full-time work)
**Optimistic estimate:** 30 hours (~4 days of full-time work)
**Realistic with testing:** 38 hours (~5 days with thorough benchmarking)

---

## Phase 1: Foundation (6-9 hours total)

### Milestone 1.1: Convert Simple Macros to Constexpr (2-3 hours)
**Risk:** ✅ VERY LOW | **Dependencies:** None

#### Step 1.1.1: Convert MAXTAGLOOP constant (15 min)
**File:** `src/vm/lvm.cpp` line 60

```cpp
// Before:
#define MAXTAGLOOP	2000

// After:
inline constexpr int MAXTAGLOOP = 2000;
```

**Tasks:**
- [ ] Edit lvm.cpp line 60
- [ ] Build and verify no warnings
- [ ] Search for all usages, verify they compile
- [ ] Time: **15 minutes**

#### Step 1.1.2: Convert arithmetic operator macros (30 min)
**File:** `src/vm/lvm.cpp` lines 935-940

Currently macros: `l_addi`, `l_subi`, `l_muli`, `l_band`, `l_bor`, `l_bxor`

**Note:** `l_lti`, `l_lei`, `l_gti`, `l_gei` are already inline constexpr ✅

```cpp
// Before:
#define l_addi(L,a,b)	intop(+, a, b)
#define l_subi(L,a,b)	intop(-, a, b)
#define l_muli(L,a,b)	intop(*, a, b)
#define l_band(a,b)	intop(&, a, b)
#define l_bor(a,b)	intop(|, a, b)
#define l_bxor(a,b)	intop(^, a, b)

// After:
inline constexpr lua_Integer l_addi(lua_State*, lua_Integer a, lua_Integer b) noexcept {
    return intop(+, a, b);
}
inline constexpr lua_Integer l_subi(lua_State*, lua_Integer a, lua_Integer b) noexcept {
    return intop(-, a, b);
}
inline constexpr lua_Integer l_muli(lua_State*, lua_Integer a, lua_Integer b) noexcept {
    return intop(*, a, b);
}
inline constexpr lua_Integer l_band(lua_Integer a, lua_Integer b) noexcept {
    return intop(&, a, b);
}
inline constexpr lua_Integer l_bor(lua_Integer a, lua_Integer b) noexcept {
    return intop(|, a, b);
}
inline constexpr lua_Integer l_bxor(lua_Integer a, lua_Integer b) noexcept {
    return intop(^, a, b);
}
```

**Tasks:**
- [ ] Convert each macro to inline constexpr
- [ ] Build and verify no warnings
- [ ] Time: **30 minutes**

#### Step 1.1.3: First benchmark checkpoint (30 min)
**Critical:** Establish baseline before larger changes

```bash
cd /home/user/lua_cpp
cmake --build build --clean-first

cd testes
for i in 1 2 3 4 5; do
    ../build/lua all.lua 2>&1 | grep "total time:"
done
# Calculate average and verify ≤4.24s
```

**Tasks:**
- [ ] Clean build
- [ ] Run 5 benchmarks
- [ ] Calculate average
- [ ] Verify ≤4.24s (target: ~4.20s baseline)
- [ ] Document results
- [ ] Time: **30 minutes**

#### Step 1.1.4: Commit Phase 1.1 (15 min)

```bash
git add src/vm/lvm.cpp
git commit -m "Phase 1.1: Convert simple macros to constexpr

- Convert MAXTAGLOOP to inline constexpr int
- Convert arithmetic macros (l_addi, l_subi, etc.) to inline constexpr functions
- Benchmark: X.XXs avg (baseline: 4.20s) - no regression ✅"
git push -u origin claude/analyze-lv-018LEz1SVgM57AT2HW11UTsi
```

**Tasks:**
- [ ] Git add, commit, push
- [ ] Time: **15 minutes**

**Milestone 1.1 Total:** 1.5-2 hours

---

### Milestone 1.2: Move Static Functions to lua_State Methods (4-6 hours)
**Risk:** ✅ LOW | **Dependencies:** None

#### Step 1.2.1: Move for-loop helpers (2 hours)
**Files:** `src/vm/lvm.cpp` lines 207-311, `src/core/lstate.h`

**Functions to convert:**
- `forlimit()` → `lua_State::forLimit()`
- `forprep()` → `lua_State::forPrep()`
- `floatforloop()` → `lua_State::floatForLoop()`

**Tasks:**

**A. Add method declarations to lstate.h (20 min)**

```cpp
// In lua_State class, private section:
private:
    // For-loop operation helpers (VM-internal)
    inline int forLimit(lua_Integer init, const TValue *lim,
                        lua_Integer *p, lua_Integer step) noexcept;
    inline int forPrep(StkId ra) noexcept;
    inline int floatForLoop(StkId ra) noexcept;
```

- [ ] Edit lstate.h
- [ ] Add declarations to private section
- [ ] Build to verify syntax
- [ ] Time: **20 minutes**

**B. Convert implementations in lvm.cpp (40 min)**

```cpp
// Before:
static int forlimit(lua_State *L, lua_Integer init, const TValue *lim,
                    lua_Integer *p, lua_Integer step) {
    // ... implementation
}

// After:
int lua_State::forLimit(lua_Integer init, const TValue *lim,
                        lua_Integer *p, lua_Integer step) noexcept {
    // Same implementation, but 'this' replaces 'L'
    // Change: luaV_tointeger(lim, p, ...) stays same (uses this implicitly)
    // Change: luaG_forerror(L, lim, "limit") → luaG_forerror(this, lim, "limit")
}
```

- [ ] Remove `static` keyword
- [ ] Change function signature to `lua_State::methodName`
- [ ] Replace `L` with `this` in function bodies
- [ ] Time: **40 minutes** (3 functions × ~13 min each)

**C. Update call sites in luaV_execute (40 min)**

Find all calls to these functions and update:

```cpp
// Before (line 250):
if (forlimit(L, init, plimit, &limit, step))

// After:
if (L->forLimit(init, plimit, &limit, step))

// Before (line 240):
if (forprep(L, ra))

// After:
if (L->forPrep(ra))

// Before (line 299 - floatforloop):
else if (floatforloop(L, ra))

// After:
else if (L->floatForLoop(ra))
```

**Search strategy:**
```bash
grep -n "forlimit(" src/vm/lvm.cpp
grep -n "forprep(" src/vm/lvm.cpp
grep -n "floatforloop(" src/vm/lvm.cpp
```

- [ ] Find all call sites (3 locations expected)
- [ ] Update to method call syntax
- [ ] Time: **40 minutes**

**D. Build and test (20 min)**

- [ ] cmake --build build
- [ ] cd testes && ../build/lua all.lua
- [ ] Verify "final OK !!!"
- [ ] Time: **20 minutes**

**Step 1.2.1 Total:** 2 hours

#### Step 1.2.2: Move comparison helpers (1 hour)
**Files:** `src/vm/lvm.cpp` lines 548-570, `src/core/lstate.h`

**Functions to convert:**
- `lessthanothers()` → `lua_State::lessThanOthers()`
- `lessequalothers()` → `lua_State::lessEqualOthers()`

**Tasks:**

**A. Add method declarations (10 min)**

```cpp
// In lua_State class, private section:
private:
    inline int lessThanOthers(const TValue *l, const TValue *r);
    inline int lessEqualOthers(const TValue *l, const TValue *r);
```

**B. Convert implementations (20 min)**
- [ ] Remove `static` keyword
- [ ] Change to member functions
- [ ] Replace `L` with `this`

**C. Update call sites (20 min)**

```bash
grep -n "lessthanothers" src/vm/lvm.cpp
grep -n "lessequalothers" src/vm/lvm.cpp
```

Expected locations: lines 1788, 1792 in `op_order` macro calls

**D. Build and test (10 min)**

**Step 1.2.2 Total:** 1 hour

#### Step 1.2.3: Move pushclosure helper (1 hour)
**Files:** `src/vm/lvm.cpp` lines 842-857, `src/core/lstate.h`

**Function:** `pushclosure()` → `lua_State::pushClosure()`

**Tasks:**

**A. Add method declaration (10 min)**

```cpp
// In lua_State class, private section:
private:
    inline void pushClosure(Proto *p, UpVal **encup, StkId base, StkId ra);
```

**B. Convert implementation (20 min)**
- [ ] Remove `static` keyword
- [ ] Change to member function
- [ ] Replace `L` with `this`

**C. Update call site (20 min)**

```bash
grep -n "pushclosure(" src/vm/lvm.cpp
```

Expected location: line 2061 in OP_CLOSURE handler

```cpp
// Before:
halfProtect(pushclosure(L, p, cl->getUpvalPtr(0), base, ra));

// After:
halfProtect(L->pushClosure(p, cl->getUpvalPtr(0), base, ra));
```

**D. Build and test (10 min)**

**Step 1.2.3 Total:** 1 hour

#### Step 1.2.4: Consider l_strton → TValue method (30 min - OPTIONAL)
**Files:** `src/vm/lvm.cpp` line 101, `src/objects/ltvalue.h`

**Current:**
```cpp
static int l_strton(const TValue *obj, TValue *result);
```

**Could become:**
```cpp
// In TValue class:
inline int tryConvertFromString(TValue *result) const noexcept;
```

**Decision:** ⚠️ Skip for now - this function is only called from `luaV_tonumber_` and `luaV_tointeger`, which are already wrapped. Not worth the effort.

**Time saved:** 30 minutes (reallocate to testing)

#### Step 1.2.5: Benchmark checkpoint (30 min)

Run full benchmark suite:

```bash
cd /home/user/lua_cpp
cmake --build build --clean-first

cd testes
for i in 1 2 3 4 5; do
    ../build/lua all.lua 2>&1 | grep "total time:"
done
```

**Tasks:**
- [ ] Clean build
- [ ] Run 5 benchmarks
- [ ] Calculate average
- [ ] Verify ≤4.24s
- [ ] Document results
- [ ] Time: **30 minutes**

#### Step 1.2.6: Commit Phase 1.2 (15 min)

```bash
git add src/vm/lvm.cpp src/core/lstate.h
git commit -m "Phase 1.2: Convert static functions to lua_State methods

- forLimit(), forPrep(), floatForLoop() → lua_State methods
- lessThanOthers(), lessEqualOthers() → lua_State methods
- pushClosure() → lua_State method

Improves encapsulation, aligns with project's 100% encapsulation goal.
All functions remain inline, zero performance impact.

Benchmark: X.XXs avg (baseline: 4.20s) - no regression ✅"
git push
```

**Step 1.2.6 Total:** 15 minutes

**Milestone 1.2 Total:** 4.5-5 hours

---

**Phase 1 Total:** 6-9 hours

---

## Phase 2: Macro Conversion (12-16 hours total)

### ⚠️ CRITICAL: Incremental approach required - benchmark after each batch

**Strategy:** Convert macros in small batches, benchmark after each, revert if regression

### Milestone 2.1: Register/Constant Access Macros (2-3 hours)
**Risk:** ✅ LOW | **Files:** `src/vm/lvm.cpp` lines 1157-1165

**Current macros:**
```cpp
#define RA(i)   (base+InstructionView(i).a())
#define vRA(i)  s2v(RA(i))
#define RB(i)   (base+InstructionView(i).b())
#define vRB(i)  s2v(RB(i))
#define KB(i)   (k+InstructionView(i).b())
#define RC(i)   (base+InstructionView(i).c())
#define vRC(i)  s2v(RC(i))
#define KC(i)   (k+InstructionView(i).c())
#define RKC(i)  ((InstructionView(i).testk()) ? k + InstructionView(i).c() : s2v(base + InstructionView(i).c()))
```

#### Step 2.1.1: Convert to inline functions (1.5 hours)

**Decision:** These macros access local variables (base, k) from luaV_execute scope. We have two options:

**Option A:** Keep as macros (RECOMMENDED)
- These are used 300+ times in hot loop
- Access local variables from outer scope
- Converting would require passing base, k to every call
- **Verdict:** Keep as macros for performance ✅

**Option B:** Convert to lambda (experimental)
```cpp
void luaV_execute(lua_State *L, CallInfo *ci) {
    // ... setup base, k, pc, trap

    auto RA = [&](Instruction i) { return base + InstructionView(i).a(); };
    auto vRA = [&](Instruction i) { return s2v(RA(i)); };
    // ... etc
}
```

**Decision:** Skip this milestone - keep register access macros as-is
**Time saved:** 2-3 hours (reallocate to testing)

**Milestone 2.1:** SKIPPED

---

### Milestone 2.2: VM State Macros → Inline Functions (2 hours)
**Risk:** ✅ LOW | **Files:** `src/vm/lvm.cpp` lines 1169-1247

#### Step 2.2.1: Convert simple state macros (1 hour)

**Current:**
```cpp
#define updatetrap(ci)  (trap = ci->getTrap())
#define updatebase(ci)  (base = ci->funcRef().p + 1)
#define savepc(ci)      ci->setSavedPC(pc)
```

**Decision:** These are actually fine as macros - they modify local variables and are extremely simple. Converting would make code more verbose without benefit.

**Example of what it would look like:**
```cpp
// Before:
updatetrap(ci);

// After:
trap = ci->getTrap();  // Just inline it manually if needed
```

**Verdict:** Keep as macros for clarity ✅

**Milestone 2.2:** SKIPPED

---

### Milestone 2.3: Arithmetic Operation Macros (8-10 hours)
**Risk:** ⚠️ MEDIUM | **Files:** `src/vm/lvm.cpp` lines 963-1100

**This is the MAIN work of Phase 2**

#### Step 2.3.1: Analysis and design (1 hour)

**Current macro hierarchy:**
```
op_arithI    - Arithmetic with immediate operand
op_arithf    - Float-only arithmetic
op_arithfK   - Float arithmetic with constant
op_arith_aux - Helper for integer/float arithmetic
op_arith     - Full arithmetic (register operands)
op_arithK    - Full arithmetic (constant operand)
op_bitwiseK  - Bitwise with constant
op_bitwise   - Bitwise with registers
op_order     - Comparison operations
op_orderI    - Comparison with immediate
```

**Challenge:** These macros:
1. Access local variables from luaV_execute (i, pc, base, k, ci, L)
2. Modify `pc` inline (pc++)
3. Are used in 60+ locations in the VM loop

**Proposed solution:** Create helper methods on lua_State that take all needed context

```cpp
// In lua_State class (private):
template<typename IntOp, typename FloatOp>
inline void doArithmetic(Instruction i, StkId base, const TValue *k,
                         const Instruction*& pc, IntOp&& iop, FloatOp&& fop) noexcept {
    TValue *v1 = s2v(base + InstructionView(i).b());
    TValue *v2 = s2v(base + InstructionView(i).c());
    if (ttisinteger(v1) && ttisinteger(v2)) {
        StkId ra = base + InstructionView(i).a();
        lua_Integer i1 = ivalue(v1);
        lua_Integer i2 = ivalue(v2);
        pc++;
        setivalue(s2v(ra), iop(this, i1, i2));
    }
    else {
        lua_Number n1, n2;
        if (tonumberns(v1, n1) && tonumberns(v2, n2)) {
            StkId ra = base + InstructionView(i).a();
            pc++;
            setfltvalue(s2v(ra), fop(this, n1, n2));
        }
    }
}
```

**Tasks:**
- [ ] Design helper method signatures
- [ ] Create test implementation for one macro
- [ ] Verify it compiles
- [ ] Time: **1 hour**

#### Step 2.3.2: Convert op_arith family (Batch A) (2 hours)

**Convert:**
- `op_arith_aux` → `lua_State::doArithmeticAux()`
- `op_arith` → `lua_State::doArithmetic()`
- `op_arithK` → `lua_State::doArithmeticK()`

**Tasks:**
- [ ] Implement methods in lstate.h (inline)
- [ ] Update call sites in lvm.cpp
- [ ] Build and verify compilation
- [ ] Time: **2 hours**

#### Step 2.3.3: Benchmark Batch A (30 min)

**CRITICAL DECISION POINT**

```bash
cd /home/user/lua_cpp
cmake --build build --clean-first

cd testes
for i in 1 2 3 4 5; do
    ../build/lua all.lua 2>&1 | grep "total time:"
done
```

**Decision criteria:**
- ✅ If ≤4.24s: Continue to next batch
- ⚠️ If 4.24-4.30s: Investigate (may be noise, re-run 10 times)
- ❌ If >4.30s: **REVERT IMMEDIATELY** and keep macros

#### Step 2.3.4: Convert op_arithI and op_arithf families (Batch B) (2 hours)
**Conditional on Batch A success**

**Convert:**
- `op_arithI` → method
- `op_arithf` → method
- `op_arithfK` → method

**Tasks:**
- [ ] Implement methods
- [ ] Update call sites
- [ ] Build
- [ ] Time: **2 hours**

#### Step 2.3.5: Benchmark Batch B (30 min)

Same decision criteria as Batch A

#### Step 2.3.6: Convert op_bitwise family (Batch C) (1.5 hours)
**Conditional on Batch B success**

**Convert:**
- `op_bitwise` → method
- `op_bitwiseK` → method

#### Step 2.3.7: Benchmark Batch C (30 min)

#### Step 2.3.8: Convert op_order family (Batch D) (1.5 hours)
**Conditional on Batch C success**

**Convert:**
- `op_order` → method
- `op_orderI` → method

#### Step 2.3.9: Final benchmark and commit (1 hour)

**If all batches succeeded:**

```bash
git add src/vm/lvm.cpp src/core/lstate.h
git commit -m "Phase 2.3: Convert arithmetic/bitwise/comparison macros to template methods

Converted 10 macro families to type-safe template methods:
- op_arith, op_arithK, op_arith_aux, op_arithI, op_arithf, op_arithfK
- op_bitwise, op_bitwiseK
- op_order, op_orderI

Benefits:
- Type safety (compile-time errors vs runtime bugs)
- Debuggable (can step into functions)
- Better error messages

Benchmark: X.XXs avg (baseline: 4.20s) - regression: +X.XX% ✅"
git push
```

**If any batch failed:**
```bash
git reset --hard HEAD  # Revert to last good commit
# Document why it failed in lvm_analysis_suggestions.md
```

**Milestone 2.3 Total:** 8-10 hours (conditional on success)

---

**Phase 2 Total:** 8-10 hours (some milestones skipped, one major milestone conditional)

**Realistic outcome:**
- Best case: 10 hours (all conversions successful)
- Likely case: 2-4 hours (convert what we can, keep some macros)
- Worst case: 1 hour (analysis only, keep all macros)

---

## Phase 3: Code Quality (8-12 hours total)

### Milestone 3.1: Split lvm.cpp into Focused Files (6-8 hours)
**Risk:** ✅ LOW | **Dependencies:** Phases 1-2 complete

#### Step 3.1.1: Create lvm_helpers.cpp (2 hours)

**Extract from lvm.cpp:**
- Conversion functions: `luaV_tonumber_`, `luaV_tointeger`, `luaV_tointegerns`, `luaV_flttointeger` (lines 118-172)
- TValue conversion methods: `TValue::toNumber()`, etc. (lines 178-189)
- Comparison helpers: `l_strcmp`, `LTintfloat`, `LEintfloat`, `LTfloatint`, `LEfloatint`, `LTnum`, `LEnum` (lines 434-545)
- Arithmetic operations: `luaV_idiv`, `luaV_mod`, `luaV_modf`, `luaV_shiftl` (lines 749-835)

**Tasks:**

**A. Create new file (30 min)**
```bash
touch src/vm/lvm_helpers.cpp
```

Add to CMakeLists.txt:
```cmake
# In lua_internal_sources:
src/vm/lvm.cpp
src/vm/lvm_helpers.cpp  # NEW
```

**B. Move function implementations (1 hour)**
- [ ] Copy functions to lvm_helpers.cpp
- [ ] Remove from lvm.cpp
- [ ] Add necessary includes to lvm_helpers.cpp
- [ ] Add forward declarations to lvm.h if needed

**C. Build and test (30 min)**
- [ ] cmake --build build
- [ ] Verify no linker errors
- [ ] Run test suite

**Step 3.1.1 Total:** 2 hours

#### Step 3.1.2: Create lvm_table.cpp for table operations (2 hours)

**Extract from lvm.cpp:**
- Table access finishers: `luaV_finishget`, `luaV_finishset` (lines 330-423)

**Similar tasks as 3.1.1**

#### Step 3.1.3: Create lvm_string.cpp for string operations (1.5 hours)

**Extract from lvm.cpp:**
- String concatenation: `copy2buff`, `luaV_concat` (lines 676-746)
- Object length: `luaV_objlen` (if present)

#### Step 3.1.4: Update lvm.cpp to focused interpreter loop (30 min)

**lvm.cpp should now contain only:**
- luaV_execute() - main VM loop
- luaV_finishOp() - opcode continuation
- Macro definitions needed by VM loop
- lua_State method wrappers

#### Step 3.1.5: Verify and benchmark (30 min)

```bash
cmake --build build --clean-first
cd testes && ../build/lua all.lua

# Benchmark
for i in 1 2 3 4 5; do
    ../build/lua all.lua 2>&1 | grep "total time:"
done
```

#### Step 3.1.6: Commit (15 min)

```bash
git add CMakeLists.txt src/vm/lvm*.cpp src/vm/lvm.h
git commit -m "Phase 3.1: Split lvm.cpp into focused compilation units

Created:
- lvm_helpers.cpp: Conversion and arithmetic helpers
- lvm_table.cpp: Table access finishers
- lvm_string.cpp: String operations

lvm.cpp now contains only the core VM interpreter loop (luaV_execute).

Benefits:
- Faster parallel compilation
- Better code organization
- Smaller primary hot-path file

Before: 2,133 lines
After: lvm.cpp ~800 lines, helpers ~1,333 lines

Benchmark: X.XXs avg (baseline: 4.20s) - no regression ✅"
git push
```

**Milestone 3.1 Total:** 6-8 hours

---

### Milestone 3.2: Documentation Improvements (2-4 hours)
**Risk:** ✅ NONE | **Dependencies:** None (can be done anytime)

#### Step 3.2.1: Add complexity annotations to luaV_execute (1 hour)

**File:** `src/vm/lvm.cpp` line 1335

```cpp
/**
 * Main VM interpreter loop - executes Lua bytecode instructions.
 *
 * PERFORMANCE CRITICAL: This function processes billions of instructions.
 * Any changes MUST be benchmarked (target: ≤4.24s on all.lua test suite).
 *
 * Architecture:
 * - Register-based VM (not stack-based)
 * - Computed goto dispatch (10-30% faster than switch on GCC/Clang)
 * - Hot-path inlining for common operations
 * - Exception-based error handling
 *
 * Metrics:
 * - Cyclomatic complexity: ~250 (83 opcodes × ~3 paths average)
 * - Stack frame size: ~64-128 bytes (cl, k, base, pc, trap, i)
 * - Typical instruction rate: 1-3 billion/second on modern CPUs
 *
 * Performance characteristics:
 * - L1 instruction cache: Critical (keep loop < 32KB)
 * - Branch prediction: Critical (computed goto helps)
 * - Register pressure: High (keep base, pc, k in registers)
 *
 * @param L     Lua state (contains stack, CallInfo chain, global state)
 * @param ci    CallInfo for the function being executed
 *
 * @complexity O(n) where n = number of instructions executed
 * @memory Stack frame: 6-8 local variables (kept in registers)
 *
 * @see lvm.h for opcode definitions
 * @see lopcodes.h for instruction format
 */
void luaV_execute(lua_State *L, CallInfo *ci) {
    // ...
}
```

**Tasks:**
- [ ] Add comprehensive documentation
- [ ] Time: **1 hour**

#### Step 3.2.2: Document hot vs cold paths (1-2 hours)

Add comments before opcode groups:

```cpp
/**
 * ==============================================================================
 * HOT PATH OPCODES (>10% of total execution time)
 * ==============================================================================
 * These opcodes are executed most frequently in typical Lua code.
 * Performance critical - any changes here must be benchmarked carefully.
 */

vmcase(OP_MOVE) { ... }      // ~15% - variable assignment
vmcase(OP_LOADI) { ... }     // ~8% - integer constants
vmcase(OP_LOADK) { ... }     // ~5% - constant loading
vmcase(OP_GETTABLE) { ... }  // ~12% - table reads
vmcase(OP_SETTABLE) { ... }  // ~8% - table writes
vmcase(OP_ADD) { ... }       // ~6% - arithmetic
vmcase(OP_CALL) { ... }      // ~10% - function calls
// ... etc

/**
 * ==============================================================================
 * WARM PATH OPCODES (1-10% of total execution time)
 * ==============================================================================
 */

vmcase(OP_GETUPVAL) { ... }
// ... etc

/**
 * ==============================================================================
 * COLD PATH OPCODES (<1% of total execution time)
 * ==============================================================================
 * Rarely executed - performance less critical.
 */

vmcase(OP_EXTRAARG) { ... }
```

**Note:** Actual percentages would need profiling data. Use estimates for now.

**Tasks:**
- [ ] Group opcodes by frequency (estimate)
- [ ] Add section comments
- [ ] Time: **1-2 hours**

#### Step 3.2.3: Add performance tips section (1 hour)

Add at top of file after includes:

```cpp
/**
 * ==============================================================================
 * PERFORMANCE MAINTENANCE GUIDELINES
 * ==============================================================================
 *
 * This file contains the Lua VM's main interpreter loop - the most performance-
 * critical code in the entire project. Follow these guidelines when making changes:
 *
 * 1. ALWAYS BENCHMARK CHANGES
 *    cd /home/user/lua_cpp && cmake --build build --clean-first
 *    cd testes && for i in 1 2 3 4 5; do ../build/lua all.lua 2>&1 | grep "total time:"; done
 *    Target: ≤4.24s (≤1% regression from 4.20s baseline)
 *    REVERT IMMEDIATELY if >4.24s
 *
 * 2. KEEP LOCALS IN REGISTERS
 *    The main loop keeps these in CPU registers:
 *    - pc: Program counter (read every instruction)
 *    - base: Stack frame base (read every instruction)
 *    - k: Constants table (read for most opcodes)
 *    - trap: Hook flag (checked every instruction)
 *    - cl: Current closure (read occasionally)
 *    Adding more locals may cause register spilling = performance loss
 *
 * 3. INLINE FAST PATHS, CALL SLOW PATHS
 *    - Table array access: inline (common case)
 *    - Table hash access: call function (less common)
 *    - Integer arithmetic: inline (common case)
 *    - Metamethod calls: call function (rare)
 *
 * 4. MINIMIZE PC SAVES
 *    savepc(ci) writes back the program counter (needed for error handling)
 *    Only call before operations that might throw:
 *    - Use Protect() for operations that can error or GC
 *    - Use ProtectNT() for operations that only change trap
 *    - Use halfProtect() for operations that only error (no GC)
 *    - Don't save pc in hot paths that can't error
 *
 * 5. COMPUTED GOTO IS CRITICAL
 *    #if LUA_USE_JUMPTABLE enables computed goto (10-30% faster dispatch)
 *    GCC/Clang generate direct jump tables vs cascading if/else
 *    NEVER add code between vmcase labels and vmbreak
 *
 * 6. WATCH INSTRUCTION CACHE
 *    The main loop should stay under 32KB to fit in L1 instruction cache
 *    Current size: ~25KB (good)
 *    If adding complex opcodes, consider extracting to helper functions
 *
 * 7. PROFILE-GUIDED OPTIMIZATION
 *    For maximum performance, use PGO:
 *    cmake -DLUA_ENABLE_PGO=ON ...
 *    This optimizes code layout based on actual branch frequencies
 *
 * 8. TESTING REQUIREMENTS
 *    All changes must:
 *    - Build with zero warnings (-Werror)
 *    - Pass full test suite (cd testes && ../build/lua all.lua)
 *    - Meet performance target (≤4.24s)
 *    - Be benchmarked 5+ times (check for variance)
 *
 * See CLAUDE.md for complete development guidelines.
 */
```

**Tasks:**
- [ ] Add performance guidelines
- [ ] Time: **1 hour**

#### Step 3.2.4: Commit documentation (15 min)

```bash
git add src/vm/lvm.cpp
git commit -m "Phase 3.2: Add comprehensive VM documentation

- Function-level complexity annotations
- Hot/warm/cold path categorization
- Performance maintenance guidelines
- Detailed comments on critical optimizations

Helps future maintainers understand performance-critical code."
git push
```

**Milestone 3.2 Total:** 2-4 hours

---

**Phase 3 Total:** 8-12 hours

---

## Phase 4: Modern C++ Polish (Optional, 4-6 hours)

### Milestone 4.1: Use std::span for Buffer Operations (2-3 hours)
**Risk:** ✅ LOW | **Dependencies:** None

#### Step 4.1.1: Convert copy2buff to use std::span (1.5 hours)

**Current (line 676):**
```cpp
static void copy2buff(StkId top, int n, char *buff) {
    size_t tl = 0;
    do {
        size_t l = strlen(svalue(s2v(top - n)));
        memcpy(buff + tl, svalue(s2v(top - n)), l * sizeof(char));
        tl += l;
    } while (--n > 0);
}
```

**After:**
```cpp
static void copy2buff(StkId top, int n, std::span<char> buff) noexcept {
    size_t tl = 0;
    do {
        size_t l = strlen(svalue(s2v(top - n)));
        lua_assert(tl + l <= buff.size());  // Debug-mode bounds check
        memcpy(buff.data() + tl, svalue(s2v(top - n)), l * sizeof(char));
        tl += l;
    } while (--n > 0);
}
```

**Update call sites:**
```bash
grep -n "copy2buff" src/vm/lvm.cpp
```

**Tasks:**
- [ ] Add `#include <span>` to lvm.cpp
- [ ] Convert function signature
- [ ] Update call sites to pass std::span
- [ ] Build and test
- [ ] Time: **1.5 hours**

#### Step 4.1.2: Consider other span opportunities (30 min)

Search for other `char*` buffer operations that could benefit.

#### Step 4.1.3: Benchmark and commit (1 hour)

```bash
git add src/vm/lvm.cpp
git commit -m "Phase 4.1: Use std::span for buffer operations

- copy2buff() now takes std::span<char> for better type safety
- Debug-mode bounds checking via assertions
- Zero runtime overhead (span is zero-cost abstraction)

Benchmark: X.XXs avg (baseline: 4.20s) - no regression ✅"
git push
```

**Milestone 4.1 Total:** 2-3 hours

---

### Milestone 4.2: Other Modern C++ Opportunities (2-3 hours)
**Risk:** ✅ LOW

#### Step 4.2.1: Use designated initializers where appropriate (1 hour)

Look for struct initialization that could be clearer with C++20 designated initializers.

**Example:**
```cpp
// If we find code like:
TValue v;
v.value_.i = 42;
v.tt_ = LUA_VNUMINT;

// Could become:
TValue v{
    .value_ = {.i = 42},
    .tt_ = LUA_VNUMINT
};
```

**Tasks:**
- [ ] Search for opportunities
- [ ] Convert if found
- [ ] Time: **1 hour**

#### Step 4.2.2: Use [[likely]]/[[unlikely]] attributes (1-2 hours)

**Current:**
```cpp
if (l_unlikely(trap)) { ... }
if (l_likely(ttisinteger(o))) { ... }
```

**C++20 alternative:**
```cpp
if (trap) [[unlikely]] { ... }
if (ttisinteger(o)) [[likely]] { ... }
```

**Decision:** Keep current approach - `l_likely`/`l_unlikely` are more portable and work pre-C++20. Not worth changing.

**Tasks:**
- [ ] Document decision not to change
- [ ] Time: **0 hours**

#### Step 4.2.3: Commit if any changes made (15 min)

**Milestone 4.2 Total:** 1-2 hours

---

**Phase 4 Total:** 3-5 hours (mostly optional)

---

## Final Verification and Documentation (2 hours)

### Step F.1: Full benchmark suite (1 hour)

Run comprehensive benchmarks:

```bash
cd /home/user/lua_cpp
cmake --build build --clean-first

cd testes
echo "Running 10 benchmark iterations..."
for i in {1..10}; do
    ../build/lua all.lua 2>&1 | grep "total time:"
done

# Calculate statistics:
# - Average
# - Min/Max
# - Standard deviation
```

### Step F.2: Update project documentation (30 min)

**Update CLAUDE.md:**
```markdown
## Recent Major Achievements

**lvm.cpp Modernization** - Completed Nov 16, 2025:

- **Static Functions → Methods** ✅
  - Converted 5 static helpers to lua_State methods
  - Better encapsulation, zero performance impact
  - Performance: X.XXs avg (baseline: 4.20s)

- **Macro Conversion** ✅/⚠️
  - Converted 6 simple macros to inline constexpr
  - [If succeeded] Converted 10 operation macros to templates
  - [If failed] Kept operation macros for performance
  - Performance: X.XXs avg

- **Code Organization** ✅
  - Split 2,133-line lvm.cpp into focused files
  - lvm.cpp now ~800 lines (core interpreter only)
  - Faster compilation, better maintainability

- **Documentation** ✅
  - Added comprehensive performance guidelines
  - Hot/warm/cold path categorization
  - Complexity annotations
```

### Step F.3: Create summary report (30 min)

Create `lvm_modernization_report.md` with:
- What was accomplished
- What was skipped and why
- Performance results
- Lessons learned
- Future opportunities

### Step F.4: Final commit (15 min)

```bash
git add CLAUDE.md lvm_modernization_report.md
git commit -m "Complete lvm.cpp modernization project

Summary of changes:
- Converted 5 static functions to lua_State methods
- Converted 6 simple macros to inline constexpr
- [If done] Converted 10 operation macros to templates
- Split lvm.cpp into 4 focused compilation units
- Added comprehensive documentation

Total effort: X hours over Y days
Performance: X.XXs avg (baseline: 4.20s) - X.XX% change

See lvm_modernization_report.md for detailed results."
git push
```

---

## Total Time Summary

| Phase | Hours (Best) | Hours (Realistic) | Hours (Conservative) |
|-------|--------------|-------------------|---------------------|
| **Phase 1: Foundation** | 6 | 7.5 | 9 |
| **Phase 2: Macros** | 2 | 6 | 10 |
| **Phase 3: Code Quality** | 8 | 10 | 12 |
| **Phase 4: Polish** | 3 | 4 | 5 |
| **Final Verification** | 2 | 2 | 2 |
| **TOTAL** | **21** | **29.5** | **38** |

**Realistic estimate considering:**
- Testing time between phases
- Debugging unexpected issues
- Time spent understanding code
- Decision-making time
- Benchmark variance investigation

**Expected timeline:**
- **Full-time work:** 4-5 days (8 hours/day)
- **Part-time work:** 7-10 days (4 hours/day)
- **Relaxed pace:** 2-3 weeks (2 hours/day)

---

## Risk Mitigation Strategies

### If benchmarks show regression:

**Phase 1 issues (unlikely):**
- Revert to baseline
- Check compiler optimization flags
- Verify inlining is happening

**Phase 2 issues (possible):**
- Revert batch that caused regression
- Keep macros for those operations
- Document why conversion wasn't viable

**Phase 3 issues (very unlikely):**
- Check that functions weren't accidentally de-inlined
- Verify linker optimization settings
- Consider LTO (Link Time Optimization)

### If compilation issues:

- Check header include order
- Verify forward declarations
- Check for circular dependencies
- Ensure all template instantiations are available

### If test failures:

- Check for incorrect `this` vs `L` conversions
- Verify all call sites were updated
- Check for macro expansion changes

---

## Success Criteria

✅ **Must Have:**
- [ ] All tests pass (cd testes && ../build/lua all.lua → "final OK !!!")
- [ ] Performance ≤4.24s (≤1% regression from 4.20s baseline)
- [ ] Zero compiler warnings with -Werror
- [ ] All changes committed and pushed

✅ **Should Have:**
- [ ] At least Phase 1 complete (static functions → methods)
- [ ] Code organization improved (split files)
- [ ] Documentation added

🎯 **Nice to Have:**
- [ ] Operation macros converted (Phase 2)
- [ ] Modern C++ features added (Phase 4)
- [ ] Performance improved vs baseline

---

## Next Steps

Ready to start? Recommended approach:

1. **Read this plan thoroughly** (30 min)
2. **Start with Phase 1.1** (simple macro conversion)
3. **Benchmark after Phase 1** to establish process
4. **Continue incrementally** with testing at each step
5. **Stop if any phase shows regression** - document and move on

**First command to run:**
```bash
cd /home/user/lua_cpp
# Back up current state
git branch backup-before-lvm-modernization

# Start Phase 1.1.1
# Edit src/vm/lvm.cpp line 60...
```

Good luck! 🚀
