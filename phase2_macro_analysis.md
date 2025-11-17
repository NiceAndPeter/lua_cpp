# Phase 2: VM Operation Macro Analysis

**Date:** 2025-11-16
**Status:** Analysis complete - Decision pending

---

## Macro Inventory

### Current Operation Macros (lvm.cpp lines 991-1117)

**Arithmetic Operations:**
- `op_arithI(L,iop,fop)` - Arithmetic with immediate operand (lines 991-1003)
- `op_arithf_aux(L,v1,v2,fop)` - Float arithmetic auxiliary (lines 1010-1015)
- `op_arithf(L,fop)` - Float arithmetic with register operands (lines 1021-1024)
- `op_arithfK(L,fop)` - Float arithmetic with constant operand (lines 1030-1033)
- `op_arith_aux(L,v1,v2,iop,fop)` - Integer/float arithmetic auxiliary (lines 1039-1045)
- `op_arith(L,iop,fop)` - Full arithmetic with register operands (lines 1051-1054)
- `op_arithK(L,iop,fop)` - Full arithmetic with constant operand (lines 1060-1063)

**Bitwise Operations:**
- `op_bitwiseK(L,op)` - Bitwise with constant operand (lines 1066-1074)
- `op_bitwise(L,op)` - Bitwise with register operands (lines 1077-1084)

**Comparison Operations:**
- `op_order(L,op,other)` - Order comparison with operators (lines 1086-1094)
- `op_orderI(L,opi,opf,inv,tm)` - Order comparison with immediate (lines 1101-1115)

**Total:** 11 operation macros used in 33+ locations in the VM loop

---

## Analysis: Why These Macros Are Challenging

### 1. Local Variable Access

All macros access local variables from `luaV_execute` scope:

```cpp
void luaV_execute(lua_State *L, CallInfo *ci) {
    LClosure *cl;
    TValue *k;          // ← Accessed by KC(i) macro
    StkId base;         // ← Accessed by RA(i), vRB(i), vRC(i) macros
    const Instruction *pc;  // ← Modified directly: pc++
    int trap;
    Instruction i;      // ← Current instruction, used by ALL macros
    // ...

    vmcase(OP_ADD) {
        op_arith(L, l_addi, luai_numadd);  // Uses i, pc, base implicitly
        vmbreak;
    }
}
```

### 2. Program Counter Mutation

Most arithmetic macros modify `pc` directly:

```cpp
#define op_arith_aux(L,v1,v2,iop,fop) {  \
    if (ttisinteger(v1) && ttisinteger(v2)) {  \
        StkId ra = RA(i); \
        lua_Integer i1 = ivalue(v1); lua_Integer i2 = ivalue(v2);  \
        pc++; setivalue(s2v(ra), iop(L, i1, i2));  /* <-- pc modified */  \
    }  \
    else op_arithf_aux(L, v1, v2, fop); }
```

Requires passing `pc` by reference if converted to function.

### 3. Nested Macro Dependencies

Macros call other macros that access local variables:

```cpp
#define op_arith(L,iop,fop) {  \
    TValue *v1 = vRB(i);       /* vRB uses i, base */  \
    TValue *v2 = vRC(i);       /* vRC uses i, base */  \
    op_arith_aux(L, v1, v2, iop, fop); }  /* calls another macro */
```

Chain: `op_arith` → `vRB/vRC` → `base, i` access

### 4. Template Parameters Are Operators

Some macros take operators as parameters:

```cpp
#define op_order(L,op,other) {  \
    // ...
    cond = (*ra op *rb);  /* 'op' is <, <=, etc. - not a value! */
    // ...
}

// Used as:
vmcase(OP_LT) {
    op_order(L, <, lessthanothers);  /* < is an operator token */
    vmbreak;
}
```

Cannot pass operators to template functions - only function objects.

---

## Conversion Strategies Considered

### Strategy A: Pass All Context (VERBOSE)

```cpp
// Convert macro to method
class lua_State {
public:
    template<typename IntOp, typename FloatOp>
    inline void doArithmetic(Instruction i, StkId base, const TValue *k,
                             const Instruction*& pc, IntOp&& iop, FloatOp&& fop) {
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
};

// Call site becomes:
vmcase(OP_ADD) {
    L->doArithmetic(i, base, k, pc, l_addi, luai_numadd);  // VERBOSE!
    vmbreak;
}
```

**Pros:**
- Type-safe
- Debuggable
- Can step through code

**Cons:**
- ❌ Very verbose call sites (7 parameters!)
- ❌ Must pass `pc` by reference (mutation)
- ❌ Repeated parameter passing 33+ times
- ⚠️ Potential register pressure (compiler must pass many values)
- ⚠️ May not inline as well as macros

### Strategy B: Lambda Closures (EXPERIMENTAL)

```cpp
void luaV_execute(lua_State *L, CallInfo *ci) {
    // ... setup base, k, pc, i

    // Define lambda that captures locals
    auto op_arith = [&](auto&& iop, auto&& fop) {
        TValue *v1 = vRB(i);
        TValue *v2 = vRC(i);
        if (ttisinteger(v1) && ttisinteger(v2)) {
            StkId ra = RA(i);
            lua_Integer i1 = ivalue(v1);
            lua_Integer i2 = ivalue(v2);
            pc++;
            setivalue(s2v(ra), iop(L, i1, i2));
        }
        // ... rest of logic
    };

    // Use:
    vmcase(OP_ADD) {
        op_arith(l_addi, luai_numadd);  // Clean call site!
        vmbreak;
    }
}
```

**Pros:**
- ✅ Clean call sites (captures locals automatically)
- ✅ Type-safe
- ✅ Same inline potential as macros

**Cons:**
- ⚠️ Unconventional (lambdas in 2000+ line function)
- ⚠️ Each lambda increases function size
- ⚠️ Unknown compiler optimization behavior
- ⚠️ Harder to understand for maintainers

### Strategy C: Keep as Macros (CURRENT APPROACH)

```cpp
// No changes - keep existing macros
#define op_arith(L,iop,fop) {  \
    TValue *v1 = vRB(i);  \
    TValue *v2 = vRC(i);  \
    op_arith_aux(L, v1, v2, iop, fop); }

vmcase(OP_ADD) {
    op_arith(L, l_addi, luai_numadd);  // CLEAN!
    vmbreak;
}
```

**Pros:**
- ✅ Clean, concise call sites
- ✅ Zero performance risk (proven approach)
- ✅ Compiler can fully inline and optimize
- ✅ Established pattern (5+ years in production)
- ✅ Easy to understand (conventional C/C++ idiom)

**Cons:**
- ❌ Not type-safe (macro expansion errors)
- ❌ Harder to debug (can't step into macros)
- ❌ IDE tools don't understand them well

---

## Performance Risk Assessment

### Critical Factors

1. **Execution Frequency:** Billions of operations per second
2. **Instruction Cache:** VM loop must stay hot in L1 cache
3. **Register Pressure:** Local variables (base, pc, k) must stay in registers
4. **Inlining:** All fast paths must inline completely

### Risk Analysis by Strategy

| Strategy | Performance Risk | Likelihood of Regression |
|----------|------------------|-------------------------|
| **Strategy A** (Pass context) | 🔴 HIGH | 60-80% |
| **Strategy B** (Lambdas) | 🟡 MEDIUM | 30-50% |
| **Strategy C** (Keep macros) | 🟢 NONE | 0% |

**Why Strategy A is high risk:**
- Passing 6-7 parameters per call
- Potential register spilling
- May prevent inlining
- Unknown optimization behavior with `pc&` reference parameter

**Why Strategy B is medium risk:**
- Lambdas usually inline well
- Captures should be optimized away
- But: unconventional, unclear if GCC/Clang optimize this pattern well in 2000+ line function

---

## Recommendation

### Option 1: Conservative Approach ✅ (RECOMMENDED)

**Keep all VM operation macros as-is**

**Rationale:**
1. These macros are **appropriate macro usage** - they're code generation, not simple expressions
2. Performance risk is too high for minimal benefit
3. The macros are well-documented and maintainable
4. We've already converted the **appropriate** macros in Phase 1 (simple expressions like l_addi)
5. Project has strict ≤1% regression tolerance - not worth the risk

**Benefits:**
- ✅ Zero performance risk
- ✅ Proven stable code
- ✅ Focus effort on higher-value improvements (Phase 3)

**Time saved:** 10-15 hours that can be used for Phase 3 (code organization)

### Option 2: Experimental Approach ⚠️ (HIGH RISK)

**Try Strategy B (lambdas) for ONE macro as proof-of-concept**

**Approach:**
1. Convert `op_arithI` to lambda (simplest macro)
2. Benchmark extensively (10+ runs)
3. If ≤4.24s: Continue with more macros
4. If >4.24s: **REVERT IMMEDIATELY** and keep all macros

**Estimated time:**
- Success case: 10-12 hours
- Failure case: 2-3 hours (revert and document)

**Probability of success:** ~40-50%

---

## Decision Matrix

| Factor | Keep Macros | Convert to Lambdas |
|--------|-------------|-------------------|
| **Performance risk** | 🟢 None | 🟡 Medium |
| **Type safety improvement** | ❌ None | ✅ Yes |
| **Debuggability improvement** | ❌ None | ✅ Yes |
| **Time required** | ⏱️ 0 hours | ⏱️ 10-15 hours |
| **Code readability** | 🟢 Good (familiar) | 🟡 Mixed (unconventional) |
| **Maintenance burden** | 🟢 Low (established) | 🟡 Medium (new pattern) |
| **Aligns with project goals** | ⚠️ Partial | ✅ Full (C++23) |

---

## Conclusion

After detailed analysis, **I recommend Option 1: Keep macros as-is**.

These VM operation macros are:
- ✅ Appropriate macro usage (code generation, not simple expressions)
- ✅ Performance-critical (billions of executions)
- ✅ Well-documented and maintainable
- ✅ Proven stable over years

The type safety and debuggability benefits **do not justify** the performance risk and time investment for this particular use case.

**Alternative path forward:**
- ✅ Move to Phase 3 (code organization) - 8-12 hours, low risk, high value
- ✅ Split lvm.cpp into focused files
- ✅ Add comprehensive documentation
- ✅ Faster compilation, better organization

This achieves project goals (modernization, maintainability) **without risking performance regression**.

---

## If We Proceed with Conversion Anyway

If the decision is made to attempt conversion despite risks:

1. **Start with op_arithI** (simplest macro, immediate operands)
2. **Use Strategy B** (lambda with captures)
3. **Benchmark after each macro family** (not each individual macro)
4. **Strict revert policy:** Any regression >4.24s → immediate revert
5. **Document results** regardless of outcome

**First test conversion:**

```cpp
void luaV_execute(lua_State *L, CallInfo *ci) {
    // ... setup

    // Define op_arithI as lambda
    auto op_arithI_impl = [&](auto&& iop, auto&& fop) {
        TValue *ra = vRA(i);
        TValue *v1 = vRB(i);
        int imm = InstructionView(i).sc();
        if (ttisinteger(v1)) {
            lua_Integer iv1 = ivalue(v1);
            pc++;
            setivalue(ra, iop(L, iv1, imm));
        }
        else if (ttisfloat(v1)) {
            lua_Number nb = fltvalue(v1);
            lua_Number fimm = cast_num(imm);
            pc++;
            setfltvalue(ra, fop(L, nb, fimm));
        }
    };

    // Main loop
    for (;;) {
        // ...
        vmcase(OP_ADDI) {
            op_arithI_impl(l_addi, luai_numadd);
            vmbreak;
        }
    }
}
```

**Test plan:**
1. Convert only OP_ADDI initially
2. Build and verify zero warnings
3. Run test suite - must pass
4. Benchmark 10 times - average must be ≤4.24s
5. If successful, continue with OP_SUBI, OP_MULI, etc.
6. If any failure, revert immediately

---

**Next Action:** Awaiting decision - Option 1 (keep macros) or Option 2 (attempt conversion)?
