/*
** $Id: lvm.c $
** Lua virtual machine
** See Copyright Notice in lua.h
*/

#define lvm_c
#define LUA_CORE

#include "lprefix.h"

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"


/*
** By default, use jump tables in the main interpreter loop on gcc
** and compatible compilers.
**
** PERFORMANCE NOTE: Jump tables (computed goto) provide faster dispatch
** in the VM's main interpreter loop compared to a switch statement. GCC
** and Clang can generate a single indirect jump instead of cascading
** comparisons, improving instruction cache utilization and branch prediction.
*/
#if !defined(LUA_USE_JUMPTABLE)
#if defined(__GNUC__)
#define LUA_USE_JUMPTABLE	1
#else
#define LUA_USE_JUMPTABLE	0
#endif
#endif



/*
** Limit for table tag-method (metamethod) chains to prevent infinite loops.
** When __index or __newindex metamethods redirect to other tables/objects,
** this limit ensures we don't loop forever if there's a cycle in the chain.
*/
inline constexpr int MAXTAGLOOP = 2000;


/*
** ===========================================================================
** Type conversion functions
** ===========================================================================
** Moved to lvm_conversion.cpp:
**   - l_strton()              - String to number conversion
**   - luaV_tonumber_()        - Value to float conversion
**   - luaV_flttointeger()     - Float to integer with rounding
**   - luaV_tointegerns()      - Value to integer (no string coercion)
**   - luaV_tointeger()        - Value to integer (with string coercion)
**   - TValue::toNumber()      - TValue conversion methods
**   - TValue::toInteger()
**   - TValue::toIntegerNoString()
** ===========================================================================
*/


/*
** ===========================================================================
** For-loop operations (lua_State methods)
** ===========================================================================
** Moved to lvm_loops.cpp:
**   - lua_State::forLimit()      - Convert for-loop limit to integer
**   - lua_State::forPrep()       - Prepare numerical for loop (OP_FORPREP)
**   - lua_State::floatForLoop()  - Execute float for-loop step
** ===========================================================================
*/


/*
** ===========================================================================
** Table access operations
** ===========================================================================
** Moved to lvm_table.cpp:
**   - luaV_finishget()  - Finish table access with __index metamethod
**   - luaV_finishset()  - Finish table assignment with __newindex metamethod
** ===========================================================================
*/


/*
** ===========================================================================
** Comparison operations
** ===========================================================================
** Moved to lvm_comparison.cpp:
**   - l_strcmp()                 - String comparison with locale support
**   - LTintfloat(), LEintfloat() - Integer vs float comparisons
**   - LTfloatint(), LEfloatint() - Float vs integer comparisons
**   - lua_State::lessThanOthers(), lua_State::lessEqualOthers()
**   - luaV_lessthan(), luaV_lessequal() - Main comparison operations
**   - luaV_equalobj()            - Equality comparison with metamethods
** ===========================================================================
*/


/*
** ===========================================================================
** String concatenation and length operations
** ===========================================================================
** Moved to lvm_string.cpp:
**   - tostring()     - Ensure value is a string (with coercion)
**   - isemptystr()   - Check if string is empty
**   - copy2buff()    - Copy strings from stack to buffer
**   - luaV_concat()  - Main concatenation operation
**   - luaV_objlen()  - Length operator (#) implementation
** ===========================================================================
*/



/*
** create a new Lua closure, push it in the stack, and initialize
** its upvalues.
*/
void lua_State::pushClosure(Proto *p, UpVal **encup, StkId base, StkId ra) {
  int nup = p->getUpvaluesSize();
  Upvaldesc *uv = p->getUpvalues();
  int i;
  LClosure *ncl = LClosure::create(this, nup);
  ncl->setProto(p);
  setclLvalue2s(this, ra, ncl);  /* anchor new closure in stack */
  for (i = 0; i < nup; i++) {  /* fill in its upvalues */
    if (uv[i].isInStack())  /* upvalue refers to local variable? */
      ncl->setUpval(i, luaF_findupval(this, base + uv[i].getIndex()));
    else  /* get upvalue from enclosing function */
      ncl->setUpval(i, encup[uv[i].getIndex()]);
    luaC_objbarrier(this, ncl, ncl->getUpval(i));
  }
}


/*
** finish execution of an opcode interrupted by a yield
*/
void luaV_finishOp (lua_State *L) {
  CallInfo *ci = L->getCI();
  StkId base = ci->funcRef().p + 1;
  Instruction inst = *(ci->getSavedPC() - 1);  /* interrupted instruction */
  OpCode op = static_cast<OpCode>(InstructionView(inst).opcode());
  switch (op) {  /* finish its execution */
    case OP_MMBIN: case OP_MMBINI: case OP_MMBINK: {
      *s2v(base + InstructionView(*(ci->getSavedPC() - 2)).a()) = *s2v(--L->getTop().p);
      break;
    }
    case OP_UNM: case OP_BNOT: case OP_LEN:
    case OP_GETTABUP: case OP_GETTABLE: case OP_GETI:
    case OP_GETFIELD: case OP_SELF: {
      *s2v(base + InstructionView(inst).a()) = *s2v(--L->getTop().p);
      break;
    }
    case OP_LT: case OP_LE:
    case OP_LTI: case OP_LEI:
    case OP_GTI: case OP_GEI:
    case OP_EQ: {  /* note that 'OP_EQI'/'OP_EQK' cannot yield */
      int res = !l_isfalse(s2v(L->getTop().p - 1));
      L->getTop().p--;
      lua_assert(InstructionView(*ci->getSavedPC()).opcode() == OP_JMP);
      if (res != InstructionView(inst).k())  /* condition failed? */
        ci->setSavedPC(ci->getSavedPC() + 1);  /* skip jump instruction */
      break;
    }
    case OP_CONCAT: {
      StkId top = L->getTop().p - 1;  /* top when 'luaT_tryconcatTM' was called */
      int a = InstructionView(inst).a();      /* first element to concatenate */
      int total = cast_int(top - 1 - (base + a));  /* yet to concatenate */
      *s2v(top - 2) = *s2v(top);  /* put TM result in proper position (operator=) */
      L->getTop().p = top - 1;  /* top is one after last element (at top-2) */
      luaV_concat(L, total);  /* concat them (may yield again) */
      break;
    }
    case OP_CLOSE: {  /* yielded closing variables */
      ci->setSavedPC(ci->getSavedPC() - 1);  /* repeat instruction to close other vars. */
      break;
    }
    case OP_RETURN: {  /* yielded closing variables */
      StkId ra = base + InstructionView(inst).a();
      /* adjust top to signal correct number of returns, in case the
         return is "up to top" ('isIT') */
      L->getTop().p = ra + ci->getNRes();
      /* repeat instruction to close other vars. and complete the return */
      ci->setSavedPC(ci->getSavedPC() - 1);
      break;
    }
    default: {
      /* only these other opcodes can yield */
      lua_assert(op == OP_TFORCALL || op == OP_CALL ||
           op == OP_TAILCALL || op == OP_SETTABUP || op == OP_SETTABLE ||
           op == OP_SETI || op == OP_SETFIELD);
      break;
    }
  }
}




/*
** {==================================================================
** Macros for arithmetic/bitwise/comparison opcodes in 'luaV_execute'
**
** All these macros are to be used exclusively inside the main
** iterpreter loop (function luaV_execute) and may access directly
** the local variables of that function (L, i, pc, ci, etc.).
** ===================================================================
*/

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

inline constexpr bool l_lti(lua_Integer a, lua_Integer b) noexcept {
	return a < b;
}

inline constexpr bool l_lei(lua_Integer a, lua_Integer b) noexcept {
	return a <= b;
}

inline constexpr bool l_gti(lua_Integer a, lua_Integer b) noexcept {
	return a > b;
}

inline constexpr bool l_gei(lua_Integer a, lua_Integer b) noexcept {
	return a >= b;
}

/*
** NOTE: The VM operation macros (op_arithI, op_arith, op_arithK, op_bitwise, etc.)
** have been converted to lambdas defined inside luaV_execute() for better type safety
** and debuggability. See lines 1378-1514 for the lambda implementations.
*/

/* }================================================================== */


/*
** {==================================================================
** Function 'luaV_execute': main interpreter loop
** ===================================================================
**
** ARCHITECTURE OVERVIEW:
** This is the heart of the Lua VM - a register-based bytecode interpreter.
** Unlike stack-based VMs (like the JVM or Python's CPython), Lua uses
** registers for local variables and intermediate values, reducing stack
** manipulation overhead.
**
** KEY DESIGN DECISIONS:
** 1. Register-based: Instructions reference register indices (A, B, C fields)
**    rather than implicitly using a stack. This reduces instruction count
**    and improves cache locality.
**
** 2. Inline dispatch: The main loop uses either computed goto (jump tables)
**    or a large switch statement to dispatch instructions. Computed goto is
**    ~10-30% faster on modern CPUs due to better branch prediction.
**
** 3. Hot-path optimization: Common operations (table access, arithmetic on
**    integers) have fast paths inlined directly in the VM loop to avoid
**    function call overhead.
**
** 4. Protect macros: Operations that can raise errors or trigger GC use
**    Protect() macros to save VM state (pc, top) before the operation.
**    This enables proper stack unwinding via C++ exceptions.
**
** 5. Trap mechanism: The 'trap' variable tracks whether hooks are enabled
**    or stack reallocation is needed. Checked before each instruction fetch
**    to handle debugger breakpoints and step-through.
**
** PERFORMANCE CRITICAL: This function processes billions of instructions
** per second. Every cycle counts. Changes here should be benchmarked.
*/

/*
** some macros for common tasks in 'luaV_execute'
*/


/*
** Register and constant access functions (converted from macros to lambdas)
**
** RA, RB, RC: Convert instruction field to stack index (StkId pointer)
** vRA, vRB, vRC: Get TValue* from stack index (s2v = stack-to-value)
** KB, KC: Get constant from constant table using instruction field
** RKC: Get either register or constant depending on 'k' bit in instruction
**
** Example instruction format (iABC):
**   OP_ADD A B C  means: R(A) := R(B) + R(C)
**   OP_ADDK A B C means: R(A) := R(B) + K(C)  [if k bit set]
**
** NOTE: These have been converted to lambdas defined inside luaV_execute()
** for better type safety and debuggability. See lines 1274-1301 for implementations.
*/



/*
** State management functions (converted from macros to lambdas)
**
** updatetrap(ci): Update local trap variable from CallInfo
** updatebase(ci): Update local base pointer from CallInfo
** updatestack(ra,ci,i): Conditionally update base and ra if trap is set
**
** NOTE: These have been converted to lambdas defined inside luaV_execute()
** for better type safety. See lines ~1304-1323 for implementations.
*/


/*
** Control flow functions (converted from macros to lambdas)
**
** dojump(ci,i,e): Execute a jump instruction. The 'updatetrap' allows signals
**                 to stop tight loops. (Without it, the local copy of 'trap'
**                 could never change.)
** donextjump(ci): For test instructions, execute the jump instruction that follows it
** docondjump(cond,ci,i): Conditional jump - skip next instruction if 'cond' is not
**                        what was expected (parameter 'k'), else do next instruction,
**                        which must be a jump.
**
** NOTE: These have been converted to lambdas defined inside luaV_execute()
** for better type safety. See lines ~1331-1345 for implementations.
*/


/*
** Correct global 'pc' (program counter).
** The local 'pc' variable is kept in a register for performance. Before any
** operation that might throw an exception, we must save it to the CallInfo
** so stack unwinding can report the correct error location.
**
** savepc(ci): Save local pc to CallInfo
** savestate(L,ci): Save both pc and top to CallInfo and lua_State
**
** NOTE: These have been converted to lambdas defined inside luaV_execute()
** for better type safety. See lines ~1317-1323 for implementations.
**
** EXCEPTION HANDLING: This implementation uses C++ exceptions instead of
** setjmp/longjmp. When an error is thrown, the exception handler needs
** accurate pc and top values to:
** 1. Report the correct line number in error messages
** 2. Properly unwind the stack to the correct depth
** 3. Close any to-be-closed variables at the right stack level
*/


/*
** function executed during Lua functions at points where the
** function can yield.
*/
#if !defined(luai_threadyield)
inline void luai_threadyield(lua_State* L) noexcept {
  lua_unlock(L);
  lua_lock(L);
}
#endif

/*
** Check if garbage collection is needed and yield thread if necessary.
**
** 'c' is the limit of live values in the stack (typically L->top or ci->top)
**
** PERFORMANCE vs CORRECTNESS: GC is expensive, so we only check conditionally
** (luaC_condGC) rather than forcing collection. The GC uses a debt-based system
** to determine when collection is needed.
**
** The macro saves state before GC (because GC can trigger __gc metamethods that
** might throw errors), then updates trap after (because GC might have changed hooks).
**
** luai_threadyield allows the OS to schedule other threads. Without it, tight
** loops could starve other threads on single-core systems.
*/
#define checkGC(L,c)  \
	{ luaC_condGC(L, (savepc(ci), L->getTop().p = (c)), \
                         updatetrap(ci)); \
           luai_threadyield(L); }


#define vmdispatch(o)	switch(o)
#define vmcase(l)	case l:
#define vmbreak		break


/*
** Execute a Lua function (LClosure) starting at the given CallInfo.
**
** PARAMETERS:
** - L: Lua state (contains stack, current CI, and global state)
** - ci: CallInfo for the function being executed
**
** LOCAL VARIABLES (kept in registers for performance):
** - cl: Current LClosure (Lua function) being executed
** - k: Constant table for current function (cl->proto->k)
** - base: Base of current stack frame (points to function's first register)
** - pc: Program counter (points to next instruction to execute)
** - trap: Cached copy of hook mask (0 if no hooks, non-zero if hooks enabled)
**
** EXECUTION FLOW:
** startfunc: Initialize for a new function call
** returning: Return from a nested call, continue in current function
** ret: Common return point for all return opcodes
**
** The function continues executing until:
** 1. A return instruction is executed and ci has CIST_FRESH flag (new C frame)
** 2. An error is thrown (C++ exception)
** 3. The function yields (coroutine suspend)
*/
void luaV_execute (lua_State *L, CallInfo *ci) {
  LClosure *cl;
  TValue *k;
  StkId base;
  const Instruction *pc;
  int trap;
#if LUA_USE_JUMPTABLE
#include "ljumptab.h"
#endif

  /* Convert operation macros to lambdas for better type safety and debuggability.
   * These lambdas capture local variables (L, pc, base, k, etc.) automatically.
   * Note: User has explicitly allowed performance regression for this conversion.
   */

  // Undefine operation macros to avoid naming conflicts
  #undef op_arithI
  #undef op_arithf_aux
  #undef op_arithf
  #undef op_arithfK
  #undef op_arith_aux
  #undef op_arith
  #undef op_arithK
  #undef op_bitwiseK
  #undef op_bitwise
  #undef op_order
  #undef op_orderI

  // Undefine register access macros to avoid naming conflicts
  #undef RA
  #undef vRA
  #undef RB
  #undef vRB
  #undef KB
  #undef RC
  #undef vRC
  #undef KC
  #undef RKC

  // Undefine state management macros to avoid naming conflicts
  #undef updatetrap
  #undef updatebase
  #undef updatestack
  #undef savepc
  #undef savestate

  // Undefine control flow macros to avoid naming conflicts
  #undef dojump
  #undef donextjump
  #undef docondjump

  // Undefine exception handling macros to avoid naming conflicts
  #undef Protect
  #undef ProtectNT
  #undef halfProtect
  #undef checkGC

  // Undefine VM dispatch macro to avoid naming conflict
  #undef vmfetch

  // Register access lambdas (defined before operation lambdas that use them)
  auto RA = [&](Instruction i) -> StkId {
    return base + InstructionView(i).a();
  };
  auto vRA = [&](Instruction i) -> TValue* {
    return s2v(base + InstructionView(i).a());
  };
  [[maybe_unused]] auto RB = [&](Instruction i) -> StkId {
    return base + InstructionView(i).b();
  };
  auto vRB = [&](Instruction i) -> TValue* {
    return s2v(base + InstructionView(i).b());
  };
  auto KB = [&](Instruction i) -> TValue* {
    return k + InstructionView(i).b();
  };
  [[maybe_unused]] auto RC = [&](Instruction i) -> StkId {
    return base + InstructionView(i).c();
  };
  auto vRC = [&](Instruction i) -> TValue* {
    return s2v(base + InstructionView(i).c());
  };
  auto KC = [&](Instruction i) -> TValue* {
    return k + InstructionView(i).c();
  };
  auto RKC = [&](Instruction i) -> TValue* {
    return InstructionView(i).testk() ? (k + InstructionView(i).c()) : s2v(base + InstructionView(i).c());
  };

  // State management lambdas
  auto updatetrap = [&](CallInfo* ci_arg) {
    trap = ci_arg->getTrap();
  };
  auto updatebase = [&](CallInfo* ci_arg) {
    base = ci_arg->funcRef().p + 1;
  };
  auto updatestack = [&](StkId& ra_arg, CallInfo* ci_arg, Instruction inst) {
    if (l_unlikely(trap)) {
      updatebase(ci_arg);
      ra_arg = RA(inst);
    }
  };
  auto savepc = [&](CallInfo* ci_arg) {
    ci_arg->setSavedPC(pc);
  };
  auto savestate = [&](lua_State* L_arg, CallInfo* ci_arg) {
    savepc(ci_arg);
    L_arg->getTop().p = ci_arg->topRef().p;
  };

  // Control flow lambdas
  auto dojump = [&](CallInfo* ci_arg, Instruction inst, int e) {
    pc += InstructionView(inst).sj() + e;
    updatetrap(ci_arg);
  };
  auto donextjump = [&](CallInfo* ci_arg) {
    Instruction ni = *pc;
    dojump(ci_arg, ni, 1);
  };
  auto docondjump = [&](int cond, CallInfo* ci_arg, Instruction inst) {
    if (cond != InstructionView(inst).k())
      pc++;
    else
      donextjump(ci_arg);
  };

  // Exception handling lambdas
  auto Protect = [&](auto&& expr) {
    savestate(L, ci);
    expr();
    updatetrap(ci);
  };
  auto ProtectNT = [&](auto&& expr) {
    savepc(ci);
    expr();
    updatetrap(ci);
  };
  auto halfProtect = [&](auto&& expr) {
    savestate(L, ci);
    expr();
  };
  auto checkGC = [&](lua_State* L_arg, StkId c_val) {
    luaC_condGC(L_arg,
                (savepc(ci), L_arg->getTop().p = c_val),
                updatetrap(ci));
    luai_threadyield(L_arg);
  };

  // Lambda: Arithmetic with immediate operand
  auto op_arithI = [&](auto iop, auto fop, Instruction i) {
    TValue *ra = vRA(i);
    TValue *v1 = vRB(i);
    int imm = InstructionView(i).sc();
    if (ttisinteger(v1)) {
      lua_Integer iv1 = ivalue(v1);
      pc++; setivalue(ra, iop(L, iv1, imm));
    }
    else if (ttisfloat(v1)) {
      lua_Number nb = fltvalue(v1);
      lua_Number fimm = cast_num(imm);
      pc++; setfltvalue(ra, fop(L, nb, fimm));
    }
  };

  // Lambda: Auxiliary function for arithmetic operations over floats
  auto op_arithf_aux = [&](const TValue *v1, const TValue *v2, auto fop, Instruction i) {
    lua_Number n1, n2;
    if (tonumberns(v1, n1) && tonumberns(v2, n2)) {
      StkId ra = RA(i);
      pc++; setfltvalue(s2v(ra), fop(L, n1, n2));
    }
  };

  // Lambda: Arithmetic operations over floats with register operands
  auto op_arithf = [&](auto fop, Instruction i) {
    TValue *v1 = vRB(i);
    TValue *v2 = vRC(i);
    op_arithf_aux(v1, v2, fop, i);
  };

  // Lambda: Arithmetic operations with K operands for floats
  auto op_arithfK = [&](auto fop, Instruction i) {
    TValue *v1 = vRB(i);
    TValue *v2 = KC(i);
    lua_assert(ttisnumber(v2));
    op_arithf_aux(v1, v2, fop, i);
  };

  // Lambda: Auxiliary for arithmetic operations over integers and floats
  auto op_arith_aux = [&](const TValue *v1, const TValue *v2, auto iop, auto fop, Instruction i) {
    if (ttisinteger(v1) && ttisinteger(v2)) {
      StkId ra = RA(i);
      lua_Integer i1 = ivalue(v1);
      lua_Integer i2 = ivalue(v2);
      pc++; setivalue(s2v(ra), iop(L, i1, i2));
    }
    else {
      op_arithf_aux(v1, v2, fop, i);
    }
  };

  // Lambda: Arithmetic operations with register operands
  auto op_arith = [&](auto iop, auto fop, Instruction i) {
    TValue *v1 = vRB(i);
    TValue *v2 = vRC(i);
    op_arith_aux(v1, v2, iop, fop, i);
  };

  // Lambda: Arithmetic operations with K operands
  auto op_arithK = [&](auto iop, auto fop, Instruction i) {
    TValue *v1 = vRB(i);
    TValue *v2 = KC(i);
    lua_assert(ttisnumber(v2));
    op_arith_aux(v1, v2, iop, fop, i);
  };

  // Lambda: Bitwise operations with constant operand
  auto op_bitwiseK = [&](auto op, Instruction i) {
    TValue *v1 = vRB(i);
    TValue *v2 = KC(i);
    lua_Integer i1;
    lua_Integer i2 = ivalue(v2);
    if (tointegerns(v1, &i1)) {
      StkId ra = RA(i);
      pc++; setivalue(s2v(ra), op(i1, i2));
    }
  };

  // Lambda: Bitwise operations with register operands
  auto op_bitwise = [&](auto op, Instruction i) {
    TValue *v1 = vRB(i);
    TValue *v2 = vRC(i);
    lua_Integer i1, i2;
    if (tointegerns(v1, &i1) && tointegerns(v2, &i2)) {
      StkId ra = RA(i);
      pc++; setivalue(s2v(ra), op(i1, i2));
    }
  };

  // Lambda: Order operations with register operands
  // Note: Cannot use operators as template parameters, so we pass comparator function objects
  auto op_order = [&](auto cmp, auto other, Instruction i) {
    TValue *ra = vRA(i);
    int cond;
    TValue *rb = vRB(i);
    if (ttisnumber(ra) && ttisnumber(rb))
      cond = cmp(ra, rb);  // Use comparator function object
    else
      Protect([&]() { cond = other(L, ra, rb); });
    docondjump(cond, ci, i);
  };

  // Lambda: Order operations with immediate operand
  auto op_orderI = [&](auto opi, auto opf, int inv, TMS tm, Instruction i) {
    TValue *ra = vRA(i);
    int cond;
    int im = InstructionView(i).sb();
    if (ttisinteger(ra))
      cond = opi(ivalue(ra), im);
    else if (ttisfloat(ra)) {
      lua_Number fa = fltvalue(ra);
      lua_Number fim = cast_num(im);
      cond = opf(fa, fim);
    }
    else {
      int isf = InstructionView(i).c();
      Protect([&]() { cond = luaT_callorderiTM(L, ra, im, inv, isf, tm); });
    }
    docondjump(cond, ci, i);
  };

  // Comparator function objects for op_order (operators cannot be passed as template params)
  auto cmp_lt = [](const TValue* a, const TValue* b) { return *a < *b; };
  auto cmp_le = [](const TValue* a, const TValue* b) { return *a <= *b; };

  // "Other" comparison lambdas for op_order (non-numeric comparisons)
  auto other_lt = [&](lua_State* L_arg, const TValue* l, const TValue* r) {
    return L_arg->lessThanOthers(l, r);
  };
  auto other_le = [&](lua_State* L_arg, const TValue* l, const TValue* r) {
    return L_arg->lessEqualOthers(l, r);
  };

 startfunc:
  trap = L->getHookMask();
 returning:  /* trap already set */
  cl = ci->getFunc();
  k = cl->getProto()->getConstants();
  pc = ci->getSavedPC();
  if (l_unlikely(trap))
    trap = luaG_tracecall(L);
  base = ci->funcRef().p + 1;

  Instruction i;  /* instruction being executed (moved outside loop for lambda capture) */

  // VM instruction fetch lambda
  auto vmfetch = [&]() {
    if (l_unlikely(trap)) {  /* stack reallocation or hooks? */
      trap = luaG_traceexec(L, pc);  /* handle hooks */
      updatebase(ci);  /* correct stack */
    }
    i = *(pc++);
  };

  /* main loop of interpreter */
  for (;;) {
    vmfetch();
    lua_assert(base == ci->funcRef().p + 1);
    lua_assert(base <= L->getTop().p && L->getTop().p <= L->getStackLast().p);
    /* for tests, invalidate top for instructions not expecting it */
    lua_assert(luaP_isIT(i) || (cast_void(L->getTop().p = base), 1));
    vmdispatch (InstructionView(i).opcode()) {
      vmcase(OP_MOVE) {
        StkId ra = RA(i);
        *s2v(ra) = *s2v(RB(i));  /* Use operator= for move */
        vmbreak;
      }
      vmcase(OP_LOADI) {
        StkId ra = RA(i);
        lua_Integer b = InstructionView(i).sbx();
        setivalue(s2v(ra), b);
        vmbreak;
      }
      vmcase(OP_LOADF) {
        StkId ra = RA(i);
        int b = InstructionView(i).sbx();
        setfltvalue(s2v(ra), cast_num(b));
        vmbreak;
      }
      vmcase(OP_LOADK) {
        StkId ra = RA(i);
        TValue *rb = k + InstructionView(i).bx();
        setobj2s(L, ra, rb);
        vmbreak;
      }
      vmcase(OP_LOADKX) {
        StkId ra = RA(i);
        TValue *rb;
        rb = k + InstructionView(*pc).ax(); pc++;
        setobj2s(L, ra, rb);
        vmbreak;
      }
      vmcase(OP_LOADFALSE) {
        StkId ra = RA(i);
        setbfvalue(s2v(ra));
        vmbreak;
      }
      vmcase(OP_LFALSESKIP) {
        StkId ra = RA(i);
        setbfvalue(s2v(ra));
        pc++;  /* skip next instruction */
        vmbreak;
      }
      vmcase(OP_LOADTRUE) {
        StkId ra = RA(i);
        setbtvalue(s2v(ra));
        vmbreak;
      }
      vmcase(OP_LOADNIL) {
        StkId ra = RA(i);
        int b = InstructionView(i).b();
        do {
          setnilvalue(s2v(ra++));
        } while (b--);
        vmbreak;
      }
      vmcase(OP_GETUPVAL) {
        StkId ra = RA(i);
        int b = InstructionView(i).b();
        setobj2s(L, ra, cl->getUpval(b)->getVP());
        vmbreak;
      }
      vmcase(OP_SETUPVAL) {
        StkId ra = RA(i);
        UpVal *uv = cl->getUpval(InstructionView(i).b());
        setobj(L, uv->getVP(), s2v(ra));
        luaC_barrier(L, uv, s2v(ra));
        vmbreak;
      }
      vmcase(OP_GETTABUP) {
        StkId ra = RA(i);
        TValue *upval = cl->getUpval(InstructionView(i).b())->getVP();
        TValue *rc = KC(i);
        TString *key = tsvalue(rc);  /* key must be a short string */
        lu_byte tag;
        tag = luaV_fastget(upval, key, s2v(ra), luaH_getshortstr);
        if (tagisempty(tag))
          Protect([&]() { luaV_finishget(L, upval, rc, ra, tag); });
        vmbreak;
      }
      vmcase(OP_GETTABLE) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        TValue *rc = vRC(i);
        lu_byte tag;
        if (ttisinteger(rc)) {  /* fast track for integers? */
          luaV_fastgeti(rb, ivalue(rc), s2v(ra), tag);
        }
        else
          tag = luaV_fastget(rb, rc, s2v(ra), luaH_get);
        if (tagisempty(tag))
          Protect([&]() { luaV_finishget(L, rb, rc, ra, tag); });
        vmbreak;
      }
      vmcase(OP_GETI) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        int c = InstructionView(i).c();
        lu_byte tag;
        luaV_fastgeti(rb, c, s2v(ra), tag);
        if (tagisempty(tag)) {
          TValue key;
          setivalue(&key, c);
          Protect([&]() { luaV_finishget(L, rb, &key, ra, tag); });
        }
        vmbreak;
      }
      vmcase(OP_GETFIELD) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        TValue *rc = KC(i);
        TString *key = tsvalue(rc);  /* key must be a short string */
        lu_byte tag;
        tag = luaV_fastget(rb, key, s2v(ra), luaH_getshortstr);
        if (tagisempty(tag))
          Protect([&]() { luaV_finishget(L, rb, rc, ra, tag); });
        vmbreak;
      }
      vmcase(OP_SETTABUP) {
        int hres;
        TValue *upval = cl->getUpval(InstructionView(i).a())->getVP();
        TValue *rb = KB(i);
        TValue *rc = RKC(i);
        TString *key = tsvalue(rb);  /* key must be a short string */
        hres = luaV_fastset(upval, key, rc, luaH_psetshortstr);
        if (hres == HOK)
          luaV_finishfastset(L, upval, rc);
        else
          Protect([&]() { luaV_finishset(L, upval, rb, rc, hres); });
        vmbreak;
      }
      vmcase(OP_SETTABLE) {
        StkId ra = RA(i);
        int hres;
        TValue *rb = vRB(i);  /* key (table is in 'ra') */
        TValue *rc = RKC(i);  /* value */
        if (ttisinteger(rb)) {  /* fast track for integers? */
          luaV_fastseti(s2v(ra), ivalue(rb), rc, hres);
        }
        else {
          hres = luaV_fastset(s2v(ra), rb, rc, luaH_pset);
        }
        if (hres == HOK)
          luaV_finishfastset(L, s2v(ra), rc);
        else
          Protect([&]() { luaV_finishset(L, s2v(ra), rb, rc, hres); });
        vmbreak;
      }
      vmcase(OP_SETI) {
        StkId ra = RA(i);
        int hres;
        int b = InstructionView(i).b();
        TValue *rc = RKC(i);
        luaV_fastseti(s2v(ra), b, rc, hres);
        if (hres == HOK)
          luaV_finishfastset(L, s2v(ra), rc);
        else {
          TValue key;
          setivalue(&key, b);
          Protect([&]() { luaV_finishset(L, s2v(ra), &key, rc, hres); });
        }
        vmbreak;
      }
      vmcase(OP_SETFIELD) {
        StkId ra = RA(i);
        int hres;
        TValue *rb = KB(i);
        TValue *rc = RKC(i);
        TString *key = tsvalue(rb);  /* key must be a short string */
        hres = luaV_fastset(s2v(ra), key, rc, luaH_psetshortstr);
        if (hres == HOK)
          luaV_finishfastset(L, s2v(ra), rc);
        else
          Protect([&]() { luaV_finishset(L, s2v(ra), rb, rc, hres); });
        vmbreak;
      }
      vmcase(OP_NEWTABLE) {
        StkId ra = RA(i);
        unsigned b = cast_uint(InstructionView(i).vb());  /* log2(hash size) + 1 */
        unsigned c = cast_uint(InstructionView(i).vc());  /* array size */
        Table *t;
        if (b > 0)
          b = 1u << (b - 1);  /* hash size is 2^(b - 1) */
        if (InstructionView(i).testk()) {  /* non-zero extra argument? */
          lua_assert(InstructionView(*pc).ax() != 0);
          /* add it to array size */
          c += cast_uint(InstructionView(*pc).ax()) * (MAXARG_vC + 1);
        }
        pc++;  /* skip extra argument */
        L->getTop().p = ra + 1;  /* correct top in case of emergency GC */
        t = luaH_new(L);  /* memory allocation */
        sethvalue2s(L, ra, t);
        if (b != 0 || c != 0)
          luaH_resize(L, t, c, b);  /* idem */
        checkGC(L, ra + 1);
        vmbreak;
      }
      vmcase(OP_SELF) {
        StkId ra = RA(i);
        lu_byte tag;
        TValue *rb = vRB(i);
        TValue *rc = KC(i);
        TString *key = tsvalue(rc);  /* key must be a short string */
        setobj2s(L, ra + 1, rb);
        tag = luaV_fastget(rb, key, s2v(ra), luaH_getshortstr);
        if (tagisempty(tag))
          Protect([&]() { luaV_finishget(L, rb, rc, ra, tag); });
        vmbreak;
      }
      vmcase(OP_ADDI) {
        op_arithI(l_addi, luai_numadd, i);
        vmbreak;
      }
      vmcase(OP_ADDK) {
        op_arithK(l_addi, luai_numadd, i);
        vmbreak;
      }
      vmcase(OP_SUBK) {
        op_arithK(l_subi, luai_numsub, i);
        vmbreak;
      }
      vmcase(OP_MULK) {
        op_arithK(l_muli, luai_nummul, i);
        vmbreak;
      }
      vmcase(OP_MODK) {
        savestate(L, ci);  /* in case of division by 0 */
        op_arithK(luaV_mod, luaV_modf, i);
        vmbreak;
      }
      vmcase(OP_POWK) {
        op_arithfK(luai_numpow, i);
        vmbreak;
      }
      vmcase(OP_DIVK) {
        op_arithfK(luai_numdiv, i);
        vmbreak;
      }
      vmcase(OP_IDIVK) {
        savestate(L, ci);  /* in case of division by 0 */
        op_arithK(luaV_idiv, luai_numidiv, i);
        vmbreak;
      }
      vmcase(OP_BANDK) {
        op_bitwiseK(l_band, i);
        vmbreak;
      }
      vmcase(OP_BORK) {
        op_bitwiseK(l_bor, i);
        vmbreak;
      }
      vmcase(OP_BXORK) {
        op_bitwiseK(l_bxor, i);
        vmbreak;
      }
      vmcase(OP_SHLI) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        int ic = InstructionView(i).sc();
        lua_Integer ib;
        if (tointegerns(rb, &ib)) {
          pc++; setivalue(s2v(ra), luaV_shiftl(ic, ib));
        }
        vmbreak;
      }
      vmcase(OP_SHRI) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        int ic = InstructionView(i).sc();
        lua_Integer ib;
        if (tointegerns(rb, &ib)) {
          pc++; setivalue(s2v(ra), luaV_shiftl(ib, -ic));
        }
        vmbreak;
      }
      vmcase(OP_ADD) {
        op_arith(l_addi, luai_numadd, i);
        vmbreak;
      }
      vmcase(OP_SUB) {
        op_arith(l_subi, luai_numsub, i);
        vmbreak;
      }
      vmcase(OP_MUL) {
        op_arith(l_muli, luai_nummul, i);
        vmbreak;
      }
      vmcase(OP_MOD) {
        savestate(L, ci);  /* in case of division by 0 */
        op_arith(luaV_mod, luaV_modf, i);
        vmbreak;
      }
      vmcase(OP_POW) {
        op_arithf(luai_numpow, i);
        vmbreak;
      }
      vmcase(OP_DIV) {  /* float division (always with floats) */
        op_arithf(luai_numdiv, i);
        vmbreak;
      }
      vmcase(OP_IDIV) {  /* floor division */
        savestate(L, ci);  /* in case of division by 0 */
        op_arith(luaV_idiv, luai_numidiv, i);
        vmbreak;
      }
      vmcase(OP_BAND) {
        op_bitwise(l_band, i);
        vmbreak;
      }
      vmcase(OP_BOR) {
        op_bitwise(l_bor, i);
        vmbreak;
      }
      vmcase(OP_BXOR) {
        op_bitwise(l_bxor, i);
        vmbreak;
      }
      vmcase(OP_SHL) {
        op_bitwise(luaV_shiftl, i);
        vmbreak;
      }
      vmcase(OP_SHR) {
        op_bitwise(luaV_shiftr, i);
        vmbreak;
      }
      vmcase(OP_MMBIN) {
        StkId ra = RA(i);
        Instruction pi = *(pc - 2);  /* original arith. expression */
        TValue *rb = vRB(i);
        TMS tm = (TMS)InstructionView(i).c();
        StkId result = RA(pi);
        lua_assert(OP_ADD <= InstructionView(pi).opcode() && InstructionView(pi).opcode() <= OP_SHR);
        Protect([&]() { luaT_trybinTM(L, s2v(ra), rb, result, tm); });
        vmbreak;
      }
      vmcase(OP_MMBINI) {
        StkId ra = RA(i);
        Instruction pi = *(pc - 2);  /* original arith. expression */
        int imm = InstructionView(i).sb();
        TMS tm = (TMS)InstructionView(i).c();
        int flip = InstructionView(i).k();
        StkId result = RA(pi);
        Protect([&]() { luaT_trybiniTM(L, s2v(ra), imm, flip, result, tm); });
        vmbreak;
      }
      vmcase(OP_MMBINK) {
        StkId ra = RA(i);
        Instruction pi = *(pc - 2);  /* original arith. expression */
        TValue *imm = KB(i);
        TMS tm = (TMS)InstructionView(i).c();
        int flip = InstructionView(i).k();
        StkId result = RA(pi);
        Protect([&]() { luaT_trybinassocTM(L, s2v(ra), imm, flip, result, tm); });
        vmbreak;
      }
      vmcase(OP_UNM) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        lua_Number nb;
        if (ttisinteger(rb)) {
          lua_Integer ib = ivalue(rb);
          setivalue(s2v(ra), intop(-, 0, ib));
        }
        else if (tonumberns(rb, nb)) {
          setfltvalue(s2v(ra), luai_numunm(L, nb));
        }
        else
          Protect([&]() { luaT_trybinTM(L, rb, rb, ra, TM_UNM); });
        vmbreak;
      }
      vmcase(OP_BNOT) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        lua_Integer ib;
        if (tointegerns(rb, &ib)) {
          setivalue(s2v(ra), intop(^, ~l_castS2U(0), ib));
        }
        else
          Protect([&]() { luaT_trybinTM(L, rb, rb, ra, TM_BNOT); });
        vmbreak;
      }
      vmcase(OP_NOT) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        if (l_isfalse(rb))
          setbtvalue(s2v(ra));
        else
          setbfvalue(s2v(ra));
        vmbreak;
      }
      vmcase(OP_LEN) {
        StkId ra = RA(i);
        Protect([&]() { luaV_objlen(L, ra, vRB(i)); });
        vmbreak;
      }
      vmcase(OP_CONCAT) {
        StkId ra = RA(i);
        int n = InstructionView(i).b();  /* number of elements to concatenate */
        L->getTop().p = ra + n;  /* mark the end of concat operands */
        ProtectNT([&]() { luaV_concat(L, n); });
        checkGC(L, L->getTop().p); /* 'luaV_concat' ensures correct top */
        vmbreak;
      }
      vmcase(OP_CLOSE) {
        StkId ra = RA(i);
        lua_assert(!InstructionView(i).b());  /* 'close must be alive */
        Protect([&]() { luaF_close(L, ra, LUA_OK, 1); });
        vmbreak;
      }
      vmcase(OP_TBC) {
        StkId ra = RA(i);
        /* create new to-be-closed upvalue */
        halfProtect([&]() { luaF_newtbcupval(L, ra); });
        vmbreak;
      }
      vmcase(OP_JMP) {
        dojump(ci, i, 0);
        vmbreak;
      }
      vmcase(OP_EQ) {
        StkId ra = RA(i);
        int cond;
        TValue *rb = vRB(i);
        Protect([&]() { cond = luaV_equalobj(L, s2v(ra), rb); });
        docondjump(cond, ci, i);
        vmbreak;
      }
      vmcase(OP_LT) {
        op_order(cmp_lt, other_lt, i);
        vmbreak;
      }
      vmcase(OP_LE) {
        op_order(cmp_le, other_le, i);
        vmbreak;
      }
      vmcase(OP_EQK) {
        StkId ra = RA(i);
        TValue *rb = KB(i);
        /* basic types do not use '__eq'; we can use raw equality */
        int cond = (*s2v(ra) == *rb);  /* Use operator== for cleaner syntax */
        docondjump(cond, ci, i);
        vmbreak;
      }
      vmcase(OP_EQI) {
        StkId ra = RA(i);
        int cond;
        int im = InstructionView(i).sb();
        if (ttisinteger(s2v(ra)))
          cond = (ivalue(s2v(ra)) == im);
        else if (ttisfloat(s2v(ra)))
          cond = luai_numeq(fltvalue(s2v(ra)), cast_num(im));
        else
          cond = 0;  /* other types cannot be equal to a number */
        docondjump(cond, ci, i);
        vmbreak;
      }
      vmcase(OP_LTI) {
        op_orderI(l_lti, luai_numlt, 0, TM_LT, i);
        vmbreak;
      }
      vmcase(OP_LEI) {
        op_orderI(l_lei, luai_numle, 0, TM_LE, i);
        vmbreak;
      }
      vmcase(OP_GTI) {
        op_orderI(l_gti, luai_numgt, 1, TM_LT, i);
        vmbreak;
      }
      vmcase(OP_GEI) {
        op_orderI(l_gei, luai_numge, 1, TM_LE, i);
        vmbreak;
      }
      vmcase(OP_TEST) {
        StkId ra = RA(i);
        int cond = !l_isfalse(s2v(ra));
        docondjump(cond, ci, i);
        vmbreak;
      }
      vmcase(OP_TESTSET) {
        StkId ra = RA(i);
        TValue *rb = vRB(i);
        if (l_isfalse(rb) == InstructionView(i).k())
          pc++;
        else {
          setobj2s(L, ra, rb);
          donextjump(ci);
        }
        vmbreak;
      }
      vmcase(OP_CALL) {
        StkId ra = RA(i);
        CallInfo *newci;
        int b = InstructionView(i).b();
        int nresults = InstructionView(i).c() - 1;
        if (b != 0)  /* fixed number of arguments? */
          L->getTop().p = ra + b;  /* top signals number of arguments */
        /* else previous instruction set top */
        savepc(ci);  /* in case of errors */
        if ((newci = L->preCall( ra, nresults)) == NULL)
          updatetrap(ci);  /* C call; nothing else to be done */
        else {  /* Lua call: run function in this same C frame */
          ci = newci;
          goto startfunc;
        }
        vmbreak;
      }
      vmcase(OP_TAILCALL) {
        StkId ra = RA(i);
        int b = InstructionView(i).b();  /* number of arguments + 1 (function) */
        int n;  /* number of results when calling a C function */
        int nparams1 = InstructionView(i).c();
        /* delta is virtual 'func' - real 'func' (vararg functions) */
        int delta = (nparams1) ? ci->getExtraArgs() + nparams1 : 0;
        if (b != 0)
          L->getTop().p = ra + b;
        else  /* previous instruction set top */
          b = cast_int(L->getTop().p - ra);
        savepc(ci);  /* several calls here can raise errors */
        if (InstructionView(i).testk()) {
          luaF_closeupval(L, base);  /* close upvalues from current call */
          lua_assert(L->getTbclist().p < base);  /* no pending tbc variables */
          lua_assert(base == ci->funcRef().p + 1);
        }
        if ((n = L->preTailCall( ci, ra, b, delta)) < 0)  /* Lua function? */
          goto startfunc;  /* execute the callee */
        else {  /* C function? */
          ci->funcRef().p -= delta;  /* restore 'func' (if vararg) */
          L->postCall( ci, n);  /* finish caller */
          updatetrap(ci);  /* 'luaD_poscall' can change hooks */
          goto ret;  /* caller returns after the tail call */
        }
      }
      vmcase(OP_RETURN) {
        StkId ra = RA(i);
        int n = InstructionView(i).b() - 1;  /* number of results */
        int nparams1 = InstructionView(i).c();
        if (n < 0)  /* not fixed? */
          n = cast_int(L->getTop().p - ra);  /* get what is available */
        savepc(ci);
        if (InstructionView(i).testk()) {  /* may there be open upvalues? */
          ci->setNRes(n);  /* save number of returns */
          if (L->getTop().p < ci->topRef().p)
            L->getTop().p = ci->topRef().p;
          luaF_close(L, base, CLOSEKTOP, 1);
          updatetrap(ci);
          updatestack(ra, ci, i);
        }
        if (nparams1)  /* vararg function? */
          ci->funcRef().p -= ci->getExtraArgs() + nparams1;
        L->getTop().p = ra + n;  /* set call for 'luaD_poscall' */
        L->postCall( ci, n);
        updatetrap(ci);  /* 'luaD_poscall' can change hooks */
        goto ret;
      }
      vmcase(OP_RETURN0) {
        if (l_unlikely(L->getHookMask())) {
          StkId ra = RA(i);
          L->getTop().p = ra;
          savepc(ci);
          L->postCall( ci, 0);  /* no hurry... */
          trap = 1;
        }
        else {  /* do the 'poscall' here */
          int nres = CallInfo::getNResults(ci->getCallStatus());
          L->setCI(ci->getPrevious());  /* back to caller */
          L->getTop().p = base - 1;
          for (; l_unlikely(nres > 0); nres--)
            setnilvalue(s2v(L->getTop().p++));  /* all results are nil */
        }
        goto ret;
      }
      vmcase(OP_RETURN1) {
        if (l_unlikely(L->getHookMask())) {
          StkId ra = RA(i);
          L->getTop().p = ra + 1;
          savepc(ci);
          L->postCall( ci, 1);  /* no hurry... */
          trap = 1;
        }
        else {  /* do the 'poscall' here */
          int nres = CallInfo::getNResults(ci->getCallStatus());
          L->setCI(ci->getPrevious());  /* back to caller */
          if (nres == 0)
            L->getTop().p = base - 1;  /* asked for no results */
          else {
            StkId ra = RA(i);
            *s2v(base - 1) = *s2v(ra);  /* at least this result (operator=) */
            L->getTop().p = base;
            for (; l_unlikely(nres > 1); nres--)
              setnilvalue(s2v(L->getTop().p++));  /* complete missing results */
          }
        }
       ret:  /* return from a Lua function */
        if (ci->getCallStatus() & CIST_FRESH)
          return;  /* end this frame */
        else {
          ci = ci->getPrevious();
          goto returning;  /* continue running caller in this frame */
        }
      }
      vmcase(OP_FORLOOP) {
        StkId ra = RA(i);
        if (ttisinteger(s2v(ra + 1))) {  /* integer loop? */
          lua_Unsigned count = l_castS2U(ivalue(s2v(ra)));
          if (count > 0) {  /* still more iterations? */
            lua_Integer step = ivalue(s2v(ra + 1));
            lua_Integer idx = ivalue(s2v(ra + 2));  /* control variable */
            chgivalue(s2v(ra), l_castU2S(count - 1));  /* update counter */
            idx = intop(+, idx, step);  /* add step to index */
            chgivalue(s2v(ra + 2), idx);  /* update control variable */
            pc -= InstructionView(i).bx();  /* jump back */
          }
        }
        else if (L->floatForLoop(ra))  /* float loop */
          pc -= InstructionView(i).bx();  /* jump back */
        updatetrap(ci);  /* allows a signal to break the loop */
        vmbreak;
      }
      vmcase(OP_FORPREP) {
        StkId ra = RA(i);
        savestate(L, ci);  /* in case of errors */
        if (L->forPrep(ra))
          pc += InstructionView(i).bx() + 1;  /* skip the loop */
        vmbreak;
      }
      vmcase(OP_TFORPREP) {
       /* before: 'ra' has the iterator function, 'ra + 1' has the state,
          'ra + 2' has the initial value for the control variable, and
          'ra + 3' has the closing variable. This opcode then swaps the
          control and the closing variables and marks the closing variable
          as to-be-closed.
       */
       StkId ra = RA(i);
       TValue temp;  /* to swap control and closing variables */
       temp = *s2v(ra + 3);  /* Use operator= for temp assignment */
       *s2v(ra + 3) = *s2v(ra + 2);  /* Use operator= */
       *s2v(ra + 2) = temp;  /* Use operator= */
        /* create to-be-closed upvalue (if closing var. is not nil) */
        halfProtect([&]() { luaF_newtbcupval(L, ra + 2); });
        pc += InstructionView(i).bx();  /* go to end of the loop */
        i = *(pc++);  /* fetch next instruction */
        lua_assert(InstructionView(i).opcode() == OP_TFORCALL && ra == RA(i));
        goto l_tforcall;
      }
      vmcase(OP_TFORCALL) {
       l_tforcall: {
        /* 'ra' has the iterator function, 'ra + 1' has the state,
           'ra + 2' has the closing variable, and 'ra + 3' has the control
           variable. The call will use the stack starting at 'ra + 3',
           so that it preserves the first three values, and the first
           return will be the new value for the control variable.
        */
        StkId ra = RA(i);
        *s2v(ra + 5) = *s2v(ra + 3);  /* copy the control variable (operator=) */
        *s2v(ra + 4) = *s2v(ra + 1);  /* copy state (operator=) */
        *s2v(ra + 3) = *s2v(ra);  /* copy function (operator=) */
        L->getTop().p = ra + 3 + 3;
        ProtectNT([&]() { L->call( ra + 3, InstructionView(i).c()); });  /* do the call */
        updatestack(ra, ci, i);  /* stack may have changed */
        i = *(pc++);  /* go to next instruction */
        lua_assert(InstructionView(i).opcode() == OP_TFORLOOP && ra == RA(i));
        goto l_tforloop;
      }}
      vmcase(OP_TFORLOOP) {
       l_tforloop: {
        StkId ra = RA(i);
        if (!ttisnil(s2v(ra + 3)))  /* continue loop? */
          pc -= InstructionView(i).bx();  /* jump back */
        vmbreak;
      }}
      vmcase(OP_SETLIST) {
        StkId ra = RA(i);
        unsigned n = cast_uint(InstructionView(i).vb());
        unsigned last = cast_uint(InstructionView(i).vc());
        Table *h = hvalue(s2v(ra));
        if (n == 0)
          n = cast_uint(L->getTop().p - ra) - 1;  /* get up to the top */
        else
          L->getTop().p = ci->topRef().p;  /* correct top in case of emergency GC */
        last += n;
        if (InstructionView(i).testk()) {
          last += cast_uint(InstructionView(*pc).ax()) * (MAXARG_vC + 1);
          pc++;
        }
        /* when 'n' is known, table should have proper size */
        if (last > h->arraySize()) {  /* needs more space? */
          /* fixed-size sets should have space preallocated */
          lua_assert(InstructionView(i).vb() == 0);
          luaH_resizearray(L, h, last);  /* preallocate it at once */
        }
        for (; n > 0; n--) {
          TValue *val = s2v(ra + n);
          obj2arr(h, last - 1, val);
          last--;
          luaC_barrierback(L, obj2gco(h), val);
        }
        vmbreak;
      }
      vmcase(OP_CLOSURE) {
        StkId ra = RA(i);
        Proto *p = cl->getProto()->getProtos()[InstructionView(i).bx()];
        halfProtect([&]() { L->pushClosure(p, cl->getUpvalPtr(0), base, ra); });
        checkGC(L, ra + 1);
        vmbreak;
      }
      vmcase(OP_VARARG) {
        StkId ra = RA(i);
        int n = InstructionView(i).c() - 1;  /* required results */
        Protect([&]() { luaT_getvarargs(L, ci, ra, n); });
        vmbreak;
      }
      vmcase(OP_VARARGPREP) {
        ProtectNT([&]() { luaT_adjustvarargs(L, InstructionView(i).a(), ci, cl->getProto()); });
        if (l_unlikely(trap)) {  /* previous "Protect" updated trap */
          L->hookCall( ci);
          L->setOldPC(1);  /* next opcode will be seen as a "new" line */
        }
        updatebase(ci);  /* function has new base after adjustment */
        vmbreak;
      }
      vmcase(OP_EXTRAARG) {
        lua_assert(0);
        vmbreak;
      }
    }
  }
}

/* }================================================================== */


/*
** lua_State VM operation methods (wrappers for compatibility)
*/

void lua_State::execute(CallInfo *callinfo) {
  luaV_execute(this, callinfo);
}

void lua_State::finishOp() {
  luaV_finishOp(this);
}

void lua_State::concat(int total) {
  luaV_concat(this, total);
}

void lua_State::objlen(StkId ra, const TValue *rb) {
  luaV_objlen(this, ra, rb);
}

lu_byte lua_State::finishGet(const TValue *t, TValue *key, StkId val, lu_byte tag) {
  return luaV_finishget(this, t, key, val, tag);
}

void lua_State::finishSet(const TValue *t, TValue *key, TValue *val, int aux) {
  luaV_finishset(this, t, key, val, aux);
}

/*
** lua_State arithmetic operation methods (wrappers for compatibility)
*/

lua_Integer lua_State::idiv(lua_Integer m, lua_Integer n) {
  return luaV_idiv(this, m, n);
}

lua_Integer lua_State::mod(lua_Integer m, lua_Integer n) {
  return luaV_mod(this, m, n);
}

lua_Number lua_State::modf(lua_Number m, lua_Number n) {
  return luaV_modf(this, m, n);
}
