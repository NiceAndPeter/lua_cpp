/*
** $Id: lstate.h $
** Global State
** See Copyright Notice in lua.h
*/

#ifndef lstate_h
#define lstate_h

#include "lua.h"


/* Some header files included here need this definition */
typedef struct CallInfo CallInfo;
class global_State;  /* forward declaration */

/* Type of protected functions, to be run by 'runprotected' */
typedef void (*Pfunc) (lua_State *L, void *ud);


#include "lobject.h"
#include "ltm.h"
#include "lzio.h"


/*
** Some notes about garbage-collected objects: All objects in Lua must
** be kept somehow accessible until being freed, so all objects always
** belong to one (and only one) of these lists, using field 'next' of
** the 'CommonHeader' for the link:
**
** 'allgc': all objects not marked for finalization;
** 'finobj': all objects marked for finalization;
** 'tobefnz': all objects ready to be finalized;
** 'fixedgc': all objects that are not to be collected (currently
** only small strings, such as reserved words).
**
** For the generational collector, some of these lists have marks for
** generations. Each mark points to the first element in the list for
** that particular generation; that generation goes until the next mark.
**
** 'allgc' -> 'survival': new objects;
** 'survival' -> 'old': objects that survived one collection;
** 'old1' -> 'reallyold': objects that became old in last collection;
** 'reallyold' -> NULL: objects old for more than one cycle.
**
** 'finobj' -> 'finobjsur': new objects marked for finalization;
** 'finobjsur' -> 'finobjold1': survived   """";
** 'finobjold1' -> 'finobjrold': just old  """";
** 'finobjrold' -> NULL: really old       """".
**
** All lists can contain elements older than their main ages, due
** to 'luaC_checkfinalizer' and 'udata2finalize', which move
** objects between the normal lists and the "marked for finalization"
** lists. Moreover, barriers can age young objects in young lists as
** OLD0, which then become OLD1. However, a list never contains
** elements younger than their main ages.
**
** The generational collector also uses a pointer 'firstold1', which
** points to the first OLD1 object in the list. It is used to optimize
** 'markold'. (Potentially OLD1 objects can be anywhere between 'allgc'
** and 'reallyold', but often the list has no OLD1 objects or they are
** after 'old1'.) Note the difference between it and 'old1':
** 'firstold1': no OLD1 objects before this point; there can be all
**   ages after it.
** 'old1': no objects younger than OLD1 after this point.
*/

/*
** Moreover, there is another set of lists that control gray objects.
** These lists are linked by fields 'gclist'. (All objects that
** can become gray have such a field. The field is not the same
** in all objects, but it always has this name.)  Any gray object
** must belong to one of these lists, and all objects in these lists
** must be gray (with two exceptions explained below):
**
** 'gray': regular gray objects, still waiting to be visited.
** 'grayagain': objects that must be revisited at the atomic phase.
**   That includes
**   - black objects got in a write barrier;
**   - all kinds of weak tables during propagation phase;
**   - all threads.
** 'weak': tables with weak values to be cleared;
** 'ephemeron': ephemeron tables with white->white entries;
** 'allweak': tables with weak keys and/or weak values to be cleared.
**
** The exceptions to that "gray rule" are:
** - TOUCHED2 objects in generational mode stay in a gray list (because
** they must be visited again at the end of the cycle), but they are
** marked black because assignments to them must activate barriers (to
** move them back to TOUCHED1).
** - Open upvalues are kept gray to avoid barriers, but they stay out
** of gray lists. (They don't even have a 'gclist' field.)
*/



/*
** About 'nCcalls':  This count has two parts: the lower 16 bits counts
** the number of recursive invocations in the C stack; the higher
** 16 bits counts the number of non-yieldable calls in the stack.
** (They are together so that we can change and save both with one
** instruction.)
*/

/* Non-yieldable call increment */
inline constexpr unsigned int nyci = (0x10000 | 1);


/*
** lua_longjmp now defined in ldo.cpp (no longer uses jmp_buf)
** Forward declaration for error handler chain
*/
struct lua_longjmp;


/*
** Atomic type (relative to signals) to better ensure that 'lua_sethook'
** is thread safe
*/
#if !defined(l_signalT)
#include <signal.h>
#define l_signalT	sig_atomic_t
#endif


/*
** Extra stack space to handle TM calls and some other extras. This
** space is not included in 'stack_last'. It is used only to avoid stack
** checks, either because the element will be promptly popped or because
** there will be a stack check soon after the push. Function frames
** never use this extra space, so it does not need to be kept clean.
*/
inline constexpr int EXTRA_STACK = 5;


/*
** Size of cache for strings in the API. 'N' is the number of
** sets (better be a prime) and "M" is the size of each set.
** (M == 1 makes a direct cache.)
*/
#if !defined(STRCACHE_N)
inline constexpr int STRCACHE_N = 53;
inline constexpr int STRCACHE_M = 2;
#endif


inline constexpr int BASIC_STACK_SIZE = (2*LUA_MINSTACK);


/*
** Possible states of the Garbage Collector
*/
enum class GCState : lu_byte {
	Propagate    = 0,
	EnterAtomic  = 1,
	Atomic       = 2,
	SweepAllGC   = 3,
	SweepFinObj  = 4,
	SweepToBeFnz = 5,
	SweepEnd     = 6,
	CallFin      = 7,
	Pause        = 8
};

/*
** Kinds of Garbage Collection
*/
enum class GCKind : lu_byte {
	Incremental       = 0,  /* incremental gc */
	GenerationalMinor = 1,  /* generational gc in minor (regular) mode */
	GenerationalMajor = 2   /* generational in major mode */
};


class stringtable {
private:
  TString **hash;  /* array of buckets (linked lists of strings) */
  int nuse;  /* number of elements */
  int size;  /* number of buckets */

public:
  // Inline accessors
  TString** getHash() const noexcept { return hash; }
  TString*** getHashPtr() noexcept { return &hash; }  // For reallocation
  int getNumElements() const noexcept { return nuse; }
  int getSize() const noexcept { return size; }

  // Inline setters
  void setHash(TString** h) noexcept { hash = h; }
  void setNumElements(int n) noexcept { nuse = n; }
  void setSize(int s) noexcept { size = s; }
  void incrementNumElements() noexcept { nuse++; }
  void decrementNumElements() noexcept { nuse--; }
};


/*
** Maximum expected number of results from a function
** (must fit in CIST_NRESULTS).
*/
inline constexpr int MAXRESULTS = 250;


/*
** Bits in CallInfo status
*/
/* bits 0-7 are the expected number of results from this function + 1 */
inline constexpr l_uint32 CIST_NRESULTS = 0xffu;

/* bits 8-11 count call metamethods (and their extra arguments) */
inline constexpr int CIST_CCMT = 8;  /* the offset, not the mask */
inline constexpr l_uint32 MAX_CCMT = (0xfu << CIST_CCMT);

/* Bits 12-14 are used for CIST_RECST (see below) */
inline constexpr int CIST_RECST = 12;  /* the offset, not the mask */

/* call is running a C function (still in first 16 bits) */
inline constexpr l_uint32 CIST_C = (1u << (CIST_RECST + 3));
/* call is on a fresh "luaV_execute" frame */
inline constexpr l_uint32 CIST_FRESH = (cast(l_uint32, CIST_C) << 1);
/* function is closing tbc variables */
inline constexpr l_uint32 CIST_CLSRET = (CIST_FRESH << 1);
/* function has tbc variables to close */
inline constexpr l_uint32 CIST_TBC = (CIST_CLSRET << 1);
/* original value of 'allowhook' */
inline constexpr l_uint32 CIST_OAH = (CIST_TBC << 1);
/* call is running a debug hook */
inline constexpr l_uint32 CIST_HOOKED = (CIST_OAH << 1);
/* doing a yieldable protected call */
inline constexpr l_uint32 CIST_YPCALL = (CIST_HOOKED << 1);
/* call was tail called */
inline constexpr l_uint32 CIST_TAIL = (CIST_YPCALL << 1);
/* last hook called yielded */
inline constexpr l_uint32 CIST_HOOKYIELD = (CIST_TAIL << 1);
/* function "called" a finalizer */
inline constexpr l_uint32 CIST_FIN = (CIST_HOOKYIELD << 1);


/*
** Information about a call.
** About union 'u':
** - field 'l' is used only for Lua functions;
** - field 'c' is used only for C functions.
** About union 'u2':
** - field 'funcidx' is used only by C functions while doing a
** protected call;
** - field 'nyield' is used only while a function is "doing" an
** yield (from the yield until the next resume);
** - field 'nres' is used only while closing tbc variables when
** returning from a function;
*/
class CallInfo {
private:
  StkIdRel func;  /* function index in the stack */
  StkIdRel top;  /* top for this function */
  struct CallInfo *previous, *next;  /* dynamic call link */
  union {
    struct {  /* only for Lua functions */
      const Instruction *savedpc;
      volatile l_signalT trap;  /* function is tracing lines/counts */
      int nextraargs;  /* # of extra arguments in vararg functions */
    } l;
    struct {  /* only for C functions */
      lua_KFunction k;  /* continuation in case of yields */
      ptrdiff_t old_errfunc;
      lua_KContext ctx;  /* context info. in case of yields */
    } c;
  } u;
  union {
    int funcidx;  /* called-function index */
    int nyield;  /* number of values yielded */
    int nres;  /* number of values returned */
  } u2;
  l_uint32 callstatus;

public:
  // Constructor: Initialize ALL fields to safe defaults
  CallInfo() noexcept {
    func.p = nullptr;
    top.p = nullptr;
    previous = nullptr;
    next = nullptr;

    // Initialize union members to safe defaults
    u.l.savedpc = nullptr;
    u.l.trap = 0;
    u.l.nextraargs = 0;

    u2.funcidx = 0;  // All union members are int-sized, 0 is safe

    callstatus = 0;
  }

  // Inline accessors for func and top (StkIdRel)
  StkIdRel& funcRef() noexcept { return func; }
  const StkIdRel& funcRef() const noexcept { return func; }
  StkIdRel& topRef() noexcept { return top; }
  const StkIdRel& topRef() const noexcept { return top; }

  // Link accessors
  CallInfo* getPrevious() const noexcept { return previous; }
  void setPrevious(CallInfo* prev) noexcept { previous = prev; }
  CallInfo** getPreviousPtr() noexcept { return &previous; }

  CallInfo* getNext() const noexcept { return next; }
  void setNext(CallInfo* n) noexcept { next = n; }
  CallInfo** getNextPtr() noexcept { return &next; }

  // CallStatus accessors
  l_uint32 getCallStatus() const noexcept { return callstatus; }
  void setCallStatus(l_uint32 status) noexcept { callstatus = status; }
  l_uint32& callStatusRef() noexcept { return callstatus; }

  // Type checks
  bool isLua() const noexcept { return (callstatus & CIST_C) == 0; }
  bool isC() const noexcept { return (callstatus & CIST_C) != 0; }
  bool isLuaCode() const noexcept { return (callstatus & (CIST_C | CIST_HOOKED)) == 0; }

  // OAH (original allow hook) accessors
  int getOAH() const noexcept { return (callstatus & CIST_OAH) ? 1 : 0; }
  void setOAH(bool v) noexcept {
    callstatus = v ? (callstatus | CIST_OAH) : (callstatus & ~CIST_OAH);
  }

  // Recover status accessors
  int getRecoverStatus() const noexcept { return (callstatus >> CIST_RECST) & 7; }
  void setRecoverStatus(int st) noexcept {
    lua_assert((st & 7) == st);  // status must fit in three bits
    callstatus = (callstatus & ~(7u << CIST_RECST)) | (cast(l_uint32, st) << CIST_RECST);
  }

  // Lua function union accessors
  const Instruction* getSavedPC() const noexcept { return u.l.savedpc; }
  void setSavedPC(const Instruction* pc) noexcept { u.l.savedpc = pc; }
  const Instruction** getSavedPCPtr() noexcept { return &u.l.savedpc; }

  volatile l_signalT& getTrap() noexcept { return u.l.trap; }
  const volatile l_signalT& getTrap() const noexcept { return u.l.trap; }

  int getExtraArgs() const noexcept { return u.l.nextraargs; }
  void setExtraArgs(int n) noexcept { u.l.nextraargs = n; }
  int& extraArgsRef() noexcept { return u.l.nextraargs; }

  // C function union accessors
  lua_KFunction getK() const noexcept { return u.c.k; }
  void setK(lua_KFunction kfunc) noexcept { u.c.k = kfunc; }

  ptrdiff_t getOldErrFunc() const noexcept { return u.c.old_errfunc; }
  void setOldErrFunc(ptrdiff_t ef) noexcept { u.c.old_errfunc = ef; }

  lua_KContext getCtx() const noexcept { return u.c.ctx; }
  void setCtx(lua_KContext context) noexcept { u.c.ctx = context; }

  // u2 union accessors
  int getFuncIdx() const noexcept { return u2.funcidx; }
  void setFuncIdx(int idx) noexcept { u2.funcidx = idx; }

  int getNYield() const noexcept { return u2.nyield; }
  void setNYield(int n) noexcept { u2.nyield = n; }

  int getNRes() const noexcept { return u2.nres; }
  void setNRes(int n) noexcept { u2.nres = n; }

  // Phase 44.5: Additional CallInfo helper methods

  // Get Lua closure from CallInfo
  LClosure* getFunc() const noexcept {
    return clLvalue(s2v(func.p));
  }

  // Extract nresults from status (static helper)
  static int getNResults(l_uint32 cs) noexcept {
    return cast_int(cs & CIST_NRESULTS) - 1;
  }
};

/* Phase 44.5: get_nresults macro replaced with CallInfo::getNResults() method */

/*
** Field CIST_RECST stores the "recover status", used to keep the error
** status while closing to-be-closed variables in coroutines, so that
** Lua can correctly resume after an yield from a __close method called
** because of an error.  (Three bits are enough for error status.)
*/


/*
** 'per thread' state
*/
class lua_State : public GCBase<lua_State> {
private:
  // Step 1: Stack fields (encapsulated)
  StkIdRel top;  /* first free slot in the stack */
  StkIdRel stack_last;  /* end of stack (last element + 1) */
  StkIdRel stack;  /* stack base */
  StkIdRel tbclist;  /* list of to-be-closed variables */

  // Step 2: CallInfo fields (encapsulated)
  CallInfo *ci;  /* call info for current function */
  CallInfo base_ci;  /* CallInfo for first level (C host) */

  // Step 3: GC and state management fields (encapsulated)
  global_State *l_G;
  UpVal *openupval;  /* list of open upvalues in this stack */
  GCObject *gclist;
  lua_State *twups;  /* list of threads with open upvalues */

  // Step 4: Status and error handling fields (encapsulated)
  TStatus status;
  struct lua_longjmp *errorJmp;  /* current error recover point */
  ptrdiff_t errfunc;  /* current error handling function (stack index) */

  // Step 5: Hook and debug fields (encapsulated)
  volatile lua_Hook hook;
  volatile l_signalT hookmask;
  lu_byte allowhook;
  int oldpc;  /* last pc traced */
  int basehookcount;
  int hookcount;
  struct {  /* info about transferred values (for call/return hooks) */
    int ftransfer;  /* offset of first value transferred */
    int ntransfer;  /* number of values transferred */
  } transferinfo;

  // Step 6: Call counter fields (encapsulated)
  l_uint32 nCcalls;  /* number of nested non-yieldable or C calls */
  int nci;  /* number of items in 'ci' list */

public:

  // Step 1: Stack field accessors - return references to allow .p access
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

  // Stack size computation
  int getStackSize() const noexcept { return cast_int(stack_last.p - stack.p); }

  // Step 2: CallInfo field accessors
  CallInfo* getCI() noexcept { return ci; }
  const CallInfo* getCI() const noexcept { return ci; }
  CallInfo* setCI(CallInfo* c) noexcept { ci = c; return ci; }  // Returns value for chaining
  CallInfo** getCIPtr() noexcept { return &ci; }

  CallInfo* getBaseCI() noexcept { return &base_ci; }
  const CallInfo* getBaseCI() const noexcept { return &base_ci; }

  // Step 3: GC and state management field accessors
  global_State* getGlobalState() noexcept { return l_G; }
  const global_State* getGlobalState() const noexcept { return l_G; }
  void setGlobalState(global_State* g) noexcept { l_G = g; }
  global_State*& getGlobalStateRef() noexcept { return l_G; }  // For G() macro

  UpVal* getOpenUpval() noexcept { return openupval; }
  const UpVal* getOpenUpval() const noexcept { return openupval; }
  void setOpenUpval(UpVal* uv) noexcept { openupval = uv; }
  UpVal** getOpenUpvalPtr() noexcept { return &openupval; }

  GCObject* getGclist() noexcept { return gclist; }
  const GCObject* getGclist() const noexcept { return gclist; }
  void setGclist(GCObject* gc) noexcept { gclist = gc; }
  GCObject** getGclistPtr() noexcept { return &gclist; }

  lua_State* getTwups() noexcept { return twups; }
  const lua_State* getTwups() const noexcept { return twups; }
  void setTwups(lua_State* tw) noexcept { twups = tw; }
  lua_State** getTwupsPtr() noexcept { return &twups; }

  // Step 4: Status and error handling field accessors
  TStatus getStatus() const noexcept { return status; }
  void setStatus(TStatus s) noexcept { status = s; }

  lua_longjmp* getErrorJmp() noexcept { return errorJmp; }
  const lua_longjmp* getErrorJmp() const noexcept { return errorJmp; }
  void setErrorJmp(lua_longjmp* ej) noexcept { errorJmp = ej; }
  lua_longjmp** getErrorJmpPtr() noexcept { return &errorJmp; }

  ptrdiff_t getErrFunc() const noexcept { return errfunc; }
  void setErrFunc(ptrdiff_t ef) noexcept { errfunc = ef; }

  // Step 5: Hook and debug field accessors
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

  // Step 6: Call counter field accessors
  l_uint32 getNCcalls() const noexcept { return nCcalls; }
  void setNCcalls(l_uint32 nc) noexcept { nCcalls = nc; }
  l_uint32& getNCcallsRef() noexcept { return nCcalls; }  // For increment/decrement

  int getNCI() const noexcept { return nci; }
  void setNCI(int n) noexcept { nci = n; }
  int& getNCIRef() noexcept { return nci; }  // For increment/decrement

  // Non-yieldable call management
  void incrementNonYieldable() noexcept { nCcalls += 0x10000; }
  void decrementNonYieldable() noexcept { nCcalls -= 0x10000; }

  // Phase 44.4: Additional lua_State helper methods

  // Thread with upvalues list check
  bool isInTwups() const noexcept {
    return twups != this;
  }

  // Hook count management
  void resetHookCount() noexcept {
    hookcount = basehookcount;
  }

  // Stack pointer save/restore (for reallocation safety)
  ptrdiff_t saveStack(StkId pt) const noexcept {
    return cast_charp(pt) - cast_charp(stack.p);
  }

  StkId restoreStack(ptrdiff_t n) const noexcept {
    return cast(StkId, cast_charp(stack.p) + n);
  }

  // Existing accessors (kept for compatibility)
  CallInfo* getCallInfo() const noexcept { return ci; }  // Alias for getCI()

  // Stack operation methods (implemented in ldo.cpp)
  void inctop();
  void shrinkStack();
  int growStack(int n, int raiseerror);
  int reallocStack(int newsize, int raiseerror);

  // Error handling methods (implemented in ldo.cpp)
  l_noret doThrow(TStatus errcode);
  l_noret throwBaseLevel(TStatus errcode);
  l_noret errorError();
  void setErrorObj(TStatus errcode, StkId oldtop);

  // Hook/debugging methods (implemented in ldo.cpp)
  void callHook(int event, int line, int fTransfer, int nTransfer);
  void hookCall(CallInfo *ci);

  // Call operation methods (implemented in ldo.cpp)
  CallInfo* preCall(StkId func, int nResults);
  void postCall(CallInfo *ci, int nres);
  int preTailCall(CallInfo *ci, StkId func, int narg1, int delta);
  void call(StkId func, int nResults);
  void callNoYield(StkId func, int nResults);

  // Protected operation methods (implemented in ldo.cpp)
  TStatus rawRunProtected(Pfunc f, void *ud);
  TStatus pCall(Pfunc func, void *u, ptrdiff_t oldtop, ptrdiff_t ef);
  TStatus closeProtected(ptrdiff_t level, TStatus status);
  TStatus protectedParser(ZIO *z, const char *name, const char *mode);

  // Internal helper methods (used by Pfunc callbacks in ldo.cpp)
  void cCall(StkId func, int nResults, l_uint32 inc);
  void unrollContinuation(void *ud);
  TStatus finishPCallK(CallInfo *ci);
  void finishCCall(CallInfo *ci);
  CallInfo* findPCall();

  // Error and debug methods (implemented in ldebug.cpp)
  const char* findLocal(CallInfo *ci, int n, StkId *pos);
  l_noret typeError(const TValue *o, const char *opname);
  l_noret callError(const TValue *o);
  l_noret forError(const TValue *o, const char *what);
  l_noret concatError(const TValue *p1, const TValue *p2);
  l_noret opinterError(const TValue *p1, const TValue *p2, const char *msg);
  l_noret toIntError(const TValue *p1, const TValue *p2);
  l_noret orderError(const TValue *p1, const TValue *p2);
  l_noret runError(const char *fmt, ...);
  const char* addInfo(const char *msg, TString *src, int line);
  l_noret errorMsg();
  int traceExec(const Instruction *pc);
  int traceCall();

private:
  // Private helper methods (implementation details in ldo.cpp)

  // Stack manipulation helpers
  void relStack();
  void correctStack(StkId oldstack);
  int stackInUse();

  // Call/hook helpers
  void retHook(CallInfo *ci, int nres);
  unsigned tryFuncTM(StkId func, unsigned status);
  void genMoveResults(StkId res, int nres, int wanted);
  void moveResults(StkId res, int nres, l_uint32 fwanted);
  CallInfo* prepareCallInfo(StkId func, unsigned status, StkId top);
  int preCallC(StkId func, unsigned status, lua_CFunction f);
};


/*
** Inline helper functions for lua_State (defined after class for complete type)
*/

/* true if this thread does not have non-yieldable calls in the stack */
inline constexpr bool yieldable(const lua_State* L) noexcept {
	return ((L->getNCcalls() & 0xffff0000) == 0);
}

/* real number of C calls */
inline constexpr l_uint32 getCcalls(const lua_State* L) noexcept {
	return (L->getNCcalls() & 0xffff);
}

/* Increment the number of non-yieldable calls */
inline void incnny(lua_State* L) noexcept {
	L->incrementNonYieldable();
}

/* Decrement the number of non-yieldable calls */
inline void decnny(lua_State* L) noexcept {
	L->decrementNonYieldable();
}


/*
** thread state + extra space
*/
typedef struct LX {
  lu_byte extra_[LUA_EXTRASPACE];
  lua_State l;
} LX;


/*
** global_State Subsystems - Single Responsibility Principle refactoring
** These classes separate global_State's 46+ fields into focused components
*/

/* 1. Memory Allocator - Memory allocation management */
class MemoryAllocator {
private:
  lua_Alloc frealloc;  /* function to reallocate memory */
  void *ud;            /* auxiliary data to 'frealloc' */

public:
  inline lua_Alloc getFrealloc() const noexcept { return frealloc; }
  inline void setFrealloc(lua_Alloc f) noexcept { frealloc = f; }
  inline void* getUd() const noexcept { return ud; }
  inline void setUd(void* u) noexcept { ud = u; }
};


/* 2. GC Accounting - Memory tracking for garbage collection */
class GCAccounting {
private:
  l_mem totalbytes;    /* Total allocated bytes + debt */
  l_mem debt;          /* Bytes counted but not yet allocated */
  l_mem marked;        /* Objects marked in current GC cycle */
  l_mem majorminor;    /* Counter to control major-minor shifts */

public:
  inline l_mem getTotalBytes() const noexcept { return totalbytes; }
  inline void setTotalBytes(l_mem bytes) noexcept { totalbytes = bytes; }
  inline l_mem& getTotalBytesRef() noexcept { return totalbytes; }

  inline l_mem getDebt() const noexcept { return debt; }
  inline void setDebt(l_mem d) noexcept { debt = d; }
  inline l_mem& getDebtRef() noexcept { return debt; }

  inline l_mem getRealTotalBytes() const noexcept { return totalbytes - debt; }

  inline l_mem getMarked() const noexcept { return marked; }
  inline void setMarked(l_mem m) noexcept { marked = m; }
  inline l_mem& getMarkedRef() noexcept { return marked; }

  inline l_mem getMajorMinor() const noexcept { return majorminor; }
  inline void setMajorMinor(l_mem mm) noexcept { majorminor = mm; }
  inline l_mem& getMajorMinorRef() noexcept { return majorminor; }
};


/* 3. GC Parameters - Garbage collector configuration and state */
class GCParameters {
private:
  lu_byte params[LUA_GCPN];  /* GC tuning parameters */
  lu_byte currentwhite;      /* Current white color for GC */
  lu_byte state;             /* State of garbage collector */
  lu_byte kind;              /* Kind of GC running (incremental/generational) */
  lu_byte stopem;            /* Stops emergency collections */
  lu_byte stp;               /* Control whether GC is running */
  lu_byte emergency;         /* True if this is emergency collection */

public:
  inline lu_byte* getParams() noexcept { return params; }
  inline const lu_byte* getParams() const noexcept { return params; }
  inline lu_byte getParam(int idx) const noexcept { return params[idx]; }
  inline void setParam(int idx, lu_byte value) noexcept { params[idx] = value; }

  inline lu_byte getCurrentWhite() const noexcept { return currentwhite; }
  inline void setCurrentWhite(lu_byte cw) noexcept { currentwhite = cw; }

  inline GCState getState() const noexcept { return static_cast<GCState>(state); }
  inline void setState(GCState s) noexcept { state = static_cast<lu_byte>(s); }

  inline GCKind getKind() const noexcept { return static_cast<GCKind>(kind); }
  inline void setKind(GCKind k) noexcept { kind = static_cast<lu_byte>(k); }

  inline lu_byte getStopEm() const noexcept { return stopem; }
  inline void setStopEm(lu_byte stop) noexcept { stopem = stop; }

  inline lu_byte getStp() const noexcept { return stp; }
  inline void setStp(lu_byte s) noexcept { stp = s; }
  inline bool isRunning() const noexcept { return stp == 0; }

  inline lu_byte getEmergency() const noexcept { return emergency; }
  inline void setEmergency(lu_byte em) noexcept { emergency = em; }
};


/* 4. GC Object Lists - Linked lists of GC-managed objects */
class GCObjectLists {
private:
  /* Incremental collector lists */
  GCObject *allgc;        /* All collectable objects */
  GCObject **sweepgc;     /* Current sweep position */
  GCObject *finobj;       /* Objects with finalizers */
  GCObject *gray;         /* Gray objects (mark phase) */
  GCObject *grayagain;    /* Objects to revisit */
  GCObject *weak;         /* Weak-value tables */
  GCObject *ephemeron;    /* Ephemeron tables (weak keys) */
  GCObject *allweak;      /* All-weak tables */
  GCObject *tobefnz;      /* To be finalized */
  GCObject *fixedgc;      /* Never collected objects */

  /* Generational collector lists */
  GCObject *survival;     /* Survived one GC cycle */
  GCObject *old1;         /* Old generation 1 */
  GCObject *reallyold;    /* Old generation 2+ */
  GCObject *firstold1;    /* First OLD1 object (optimization) */
  GCObject *finobjsur;    /* Survival objects with finalizers */
  GCObject *finobjold1;   /* Old1 objects with finalizers */
  GCObject *finobjrold;   /* Really old objects with finalizers */

public:
  /* Incremental collector accessors */
  inline GCObject* getAllGC() const noexcept { return allgc; }
  inline void setAllGC(GCObject* gc) noexcept { allgc = gc; }
  inline GCObject** getAllGCPtr() noexcept { return &allgc; }

  inline GCObject** getSweepGC() const noexcept { return sweepgc; }
  inline void setSweepGC(GCObject** sweep) noexcept { sweepgc = sweep; }
  inline GCObject*** getSweepGCPtr() noexcept { return &sweepgc; }

  inline GCObject* getFinObj() const noexcept { return finobj; }
  inline void setFinObj(GCObject* fobj) noexcept { finobj = fobj; }
  inline GCObject** getFinObjPtr() noexcept { return &finobj; }

  inline GCObject* getGray() const noexcept { return gray; }
  inline void setGray(GCObject* g) noexcept { gray = g; }
  inline GCObject** getGrayPtr() noexcept { return &gray; }

  inline GCObject* getGrayAgain() const noexcept { return grayagain; }
  inline void setGrayAgain(GCObject* ga) noexcept { grayagain = ga; }
  inline GCObject** getGrayAgainPtr() noexcept { return &grayagain; }

  inline GCObject* getWeak() const noexcept { return weak; }
  inline void setWeak(GCObject* w) noexcept { weak = w; }
  inline GCObject** getWeakPtr() noexcept { return &weak; }

  inline GCObject* getEphemeron() const noexcept { return ephemeron; }
  inline void setEphemeron(GCObject* e) noexcept { ephemeron = e; }
  inline GCObject** getEphemeronPtr() noexcept { return &ephemeron; }

  inline GCObject* getAllWeak() const noexcept { return allweak; }
  inline void setAllWeak(GCObject* aw) noexcept { allweak = aw; }
  inline GCObject** getAllWeakPtr() noexcept { return &allweak; }

  inline GCObject* getToBeFnz() const noexcept { return tobefnz; }
  inline void setToBeFnz(GCObject* tbf) noexcept { tobefnz = tbf; }
  inline GCObject** getToBeFnzPtr() noexcept { return &tobefnz; }

  inline GCObject* getFixedGC() const noexcept { return fixedgc; }
  inline void setFixedGC(GCObject* fgc) noexcept { fixedgc = fgc; }
  inline GCObject** getFixedGCPtr() noexcept { return &fixedgc; }

  /* Generational collector accessors */
  inline GCObject* getSurvival() const noexcept { return survival; }
  inline void setSurvival(GCObject* s) noexcept { survival = s; }
  inline GCObject** getSurvivalPtr() noexcept { return &survival; }

  inline GCObject* getOld1() const noexcept { return old1; }
  inline void setOld1(GCObject* o1) noexcept { old1 = o1; }
  inline GCObject** getOld1Ptr() noexcept { return &old1; }

  inline GCObject* getReallyOld() const noexcept { return reallyold; }
  inline void setReallyOld(GCObject* ro) noexcept { reallyold = ro; }
  inline GCObject** getReallyOldPtr() noexcept { return &reallyold; }

  inline GCObject* getFirstOld1() const noexcept { return firstold1; }
  inline void setFirstOld1(GCObject* fo1) noexcept { firstold1 = fo1; }
  inline GCObject** getFirstOld1Ptr() noexcept { return &firstold1; }

  inline GCObject* getFinObjSur() const noexcept { return finobjsur; }
  inline void setFinObjSur(GCObject* fos) noexcept { finobjsur = fos; }
  inline GCObject** getFinObjSurPtr() noexcept { return &finobjsur; }

  inline GCObject* getFinObjOld1() const noexcept { return finobjold1; }
  inline void setFinObjOld1(GCObject* fo1) noexcept { finobjold1 = fo1; }
  inline GCObject** getFinObjOld1Ptr() noexcept { return &finobjold1; }

  inline GCObject* getFinObjROld() const noexcept { return finobjrold; }
  inline void setFinObjROld(GCObject* for_) noexcept { finobjrold = for_; }
  inline GCObject** getFinObjROldPtr() noexcept { return &finobjrold; }
};


/* 5. String Cache - String interning and caching */
class StringCache {
private:
  stringtable strt;                               /* String interning table */
  TString *cache[STRCACHE_N][STRCACHE_M];        /* API string cache */

public:
  inline stringtable* getTable() noexcept { return &strt; }
  inline const stringtable* getTable() const noexcept { return &strt; }

  inline TString* getCache(int n, int m) const noexcept { return cache[n][m]; }
  inline void setCache(int n, int m, TString* str) noexcept { cache[n][m] = str; }
};


/* 6. Type System - Type metatables and core values */
class TypeSystem {
private:
  TValue registry;                    /* Lua registry */
  TValue nilvalue;                    /* Canonical nil value */
  unsigned int seed;                  /* Hash seed for randomization */
  Table *metatables[LUA_NUMTYPES];   /* Metatables for basic types */
  TString *tmname[TM_N];             /* Tag method names */

public:
  inline TValue* getRegistry() noexcept { return &registry; }
  inline const TValue* getRegistry() const noexcept { return &registry; }

  inline TValue* getNilValue() noexcept { return &nilvalue; }
  inline const TValue* getNilValue() const noexcept { return &nilvalue; }
  inline bool isComplete() const noexcept { return ttisnil(&nilvalue); }

  inline unsigned int getSeed() const noexcept { return seed; }
  inline void setSeed(unsigned int s) noexcept { seed = s; }

  inline Table* getMetatable(int type) const noexcept { return metatables[type]; }
  inline void setMetatable(int type, Table* mt) noexcept { metatables[type] = mt; }
  inline Table** getMetatablePtr(int type) noexcept { return &metatables[type]; }

  inline TString* getTMName(int idx) const noexcept { return tmname[idx]; }
  inline void setTMName(int idx, TString* name) noexcept { tmname[idx] = name; }
  inline TString** getTMNamePtr(int idx) noexcept { return &tmname[idx]; }
};


/* 7. Runtime Services - Runtime state and service functions */
class RuntimeServices {
private:
  lua_State *twups;           /* Threads with open upvalues */
  lua_CFunction panic;        /* Panic handler for unprotected errors */
  TString *memerrmsg;         /* Memory error message */
  lua_WarnFunction warnf;     /* Warning function */
  void *ud_warn;              /* Auxiliary data for warning function */
  LX mainth;                  /* Main thread of this state */

public:
  inline lua_State* getTwups() const noexcept { return twups; }
  inline void setTwups(lua_State* tw) noexcept { twups = tw; }
  inline lua_State** getTwupsPtr() noexcept { return &twups; }

  inline lua_CFunction getPanic() const noexcept { return panic; }
  inline void setPanic(lua_CFunction p) noexcept { panic = p; }

  inline TString* getMemErrMsg() const noexcept { return memerrmsg; }
  inline void setMemErrMsg(TString* msg) noexcept { memerrmsg = msg; }

  inline lua_WarnFunction getWarnF() const noexcept { return warnf; }
  inline void setWarnF(lua_WarnFunction wf) noexcept { warnf = wf; }

  inline void* getUdWarn() const noexcept { return ud_warn; }
  inline void setUdWarn(void* uw) noexcept { ud_warn = uw; }

  inline LX* getMainThread() noexcept { return &mainth; }
  inline const LX* getMainThread() const noexcept { return &mainth; }
};


/*
** 'global state', shared by all threads of this state
*/
class global_State {
private:
  /* Subsystems (SRP refactoring) */
  MemoryAllocator memory;        /* Memory allocation management */
  GCAccounting gcAccounting;     /* GC memory tracking */
  GCParameters gcParams;         /* GC configuration & state */
  GCObjectLists gcLists;         /* GC object linked lists */
  StringCache strings;           /* String interning & caching */
  TypeSystem types;              /* Type metatables & core values */
  RuntimeServices runtime;       /* Runtime state & services */

public:
  /* Subsystem access methods (for direct subsystem manipulation) */
  inline MemoryAllocator& getMemoryAllocator() noexcept { return memory; }
  inline const MemoryAllocator& getMemoryAllocator() const noexcept { return memory; }
  inline GCAccounting& getGCAccountingSubsystem() noexcept { return gcAccounting; }
  inline const GCAccounting& getGCAccountingSubsystem() const noexcept { return gcAccounting; }
  inline GCParameters& getGCParametersSubsystem() noexcept { return gcParams; }
  inline const GCParameters& getGCParametersSubsystem() const noexcept { return gcParams; }
  inline GCObjectLists& getGCObjectListsSubsystem() noexcept { return gcLists; }
  inline const GCObjectLists& getGCObjectListsSubsystem() const noexcept { return gcLists; }
  inline StringCache& getStringCacheSubsystem() noexcept { return strings; }
  inline const StringCache& getStringCacheSubsystem() const noexcept { return strings; }
  inline TypeSystem& getTypeSystemSubsystem() noexcept { return types; }
  inline const TypeSystem& getTypeSystemSubsystem() const noexcept { return types; }
  inline RuntimeServices& getRuntimeServicesSubsystem() noexcept { return runtime; }
  inline const RuntimeServices& getRuntimeServicesSubsystem() const noexcept { return runtime; }

  /* Delegating accessors for MemoryAllocator */
  inline lua_Alloc getFrealloc() const noexcept { return memory.getFrealloc(); }
  inline void setFrealloc(lua_Alloc f) noexcept { memory.setFrealloc(f); }
  inline void* getUd() const noexcept { return memory.getUd(); }
  inline void setUd(void* u) noexcept { memory.setUd(u); }

  /* Delegating accessors for GCAccounting */
  inline l_mem getGCTotalBytes() const noexcept { return gcAccounting.getTotalBytes(); }
  inline void setGCTotalBytes(l_mem bytes) noexcept { gcAccounting.setTotalBytes(bytes); }
  inline l_mem& getGCTotalBytesRef() noexcept { return gcAccounting.getTotalBytesRef(); }

  inline l_mem getGCDebt() const noexcept { return gcAccounting.getDebt(); }
  inline void setGCDebt(l_mem debt) noexcept { gcAccounting.setDebt(debt); }
  inline l_mem& getGCDebtRef() noexcept { return gcAccounting.getDebtRef(); }

  inline l_mem getTotalBytes() const noexcept { return gcAccounting.getRealTotalBytes(); }

  inline l_mem getGCMarked() const noexcept { return gcAccounting.getMarked(); }
  inline void setGCMarked(l_mem marked) noexcept { gcAccounting.setMarked(marked); }
  inline l_mem& getGCMarkedRef() noexcept { return gcAccounting.getMarkedRef(); }

  inline l_mem getGCMajorMinor() const noexcept { return gcAccounting.getMajorMinor(); }
  inline void setGCMajorMinor(l_mem mm) noexcept { gcAccounting.setMajorMinor(mm); }
  inline l_mem& getGCMajorMinorRef() noexcept { return gcAccounting.getMajorMinorRef(); }

  /* Delegating accessors for GCParameters */
  inline lu_byte* getGCParams() noexcept { return gcParams.getParams(); }
  inline const lu_byte* getGCParams() const noexcept { return gcParams.getParams(); }
  inline lu_byte getGCParam(int idx) const noexcept { return gcParams.getParam(idx); }
  inline void setGCParam(int idx, lu_byte value) noexcept { gcParams.setParam(idx, value); }

  inline lu_byte getCurrentWhite() const noexcept { return gcParams.getCurrentWhite(); }
  inline void setCurrentWhite(lu_byte cw) noexcept { gcParams.setCurrentWhite(cw); }
  lu_byte getWhite() const noexcept;  // Defined in lgc.h (needs WHITEBITS)

  inline GCState getGCState() const noexcept { return gcParams.getState(); }
  inline void setGCState(GCState state) noexcept { gcParams.setState(state); }
  bool keepInvariant() const noexcept;  // Defined in lgc.h (needs GCState::Atomic)
  bool isSweepPhase() const noexcept;   // Defined in lgc.h (needs GCState::SweepAllGC/SweepEnd)

  inline GCKind getGCKind() const noexcept { return gcParams.getKind(); }
  inline void setGCKind(GCKind kind) noexcept { gcParams.setKind(kind); }

  inline lu_byte getGCStopEm() const noexcept { return gcParams.getStopEm(); }
  inline void setGCStopEm(lu_byte stop) noexcept { gcParams.setStopEm(stop); }

  inline lu_byte getGCStp() const noexcept { return gcParams.getStp(); }
  inline void setGCStp(lu_byte stp) noexcept { gcParams.setStp(stp); }
  inline bool isGCRunning() const noexcept { return gcParams.isRunning(); }

  inline lu_byte getGCEmergency() const noexcept { return gcParams.getEmergency(); }
  inline void setGCEmergency(lu_byte em) noexcept { gcParams.setEmergency(em); }

  /* Delegating accessors for GCObjectLists (incremental) */
  inline GCObject* getAllGC() const noexcept { return gcLists.getAllGC(); }
  inline void setAllGC(GCObject* gc) noexcept { gcLists.setAllGC(gc); }
  inline GCObject** getAllGCPtr() noexcept { return gcLists.getAllGCPtr(); }

  inline GCObject** getSweepGC() const noexcept { return gcLists.getSweepGC(); }
  inline void setSweepGC(GCObject** sweep) noexcept { gcLists.setSweepGC(sweep); }
  inline GCObject*** getSweepGCPtr() noexcept { return gcLists.getSweepGCPtr(); }

  inline GCObject* getFinObj() const noexcept { return gcLists.getFinObj(); }
  inline void setFinObj(GCObject* fobj) noexcept { gcLists.setFinObj(fobj); }
  inline GCObject** getFinObjPtr() noexcept { return gcLists.getFinObjPtr(); }

  inline GCObject* getGray() const noexcept { return gcLists.getGray(); }
  inline void setGray(GCObject* g) noexcept { gcLists.setGray(g); }
  inline GCObject** getGrayPtr() noexcept { return gcLists.getGrayPtr(); }

  inline GCObject* getGrayAgain() const noexcept { return gcLists.getGrayAgain(); }
  inline void setGrayAgain(GCObject* ga) noexcept { gcLists.setGrayAgain(ga); }
  inline GCObject** getGrayAgainPtr() noexcept { return gcLists.getGrayAgainPtr(); }

  inline GCObject* getWeak() const noexcept { return gcLists.getWeak(); }
  inline void setWeak(GCObject* w) noexcept { gcLists.setWeak(w); }
  inline GCObject** getWeakPtr() noexcept { return gcLists.getWeakPtr(); }

  inline GCObject* getEphemeron() const noexcept { return gcLists.getEphemeron(); }
  inline void setEphemeron(GCObject* e) noexcept { gcLists.setEphemeron(e); }
  inline GCObject** getEphemeronPtr() noexcept { return gcLists.getEphemeronPtr(); }

  inline GCObject* getAllWeak() const noexcept { return gcLists.getAllWeak(); }
  inline void setAllWeak(GCObject* aw) noexcept { gcLists.setAllWeak(aw); }
  inline GCObject** getAllWeakPtr() noexcept { return gcLists.getAllWeakPtr(); }

  inline GCObject* getToBeFnz() const noexcept { return gcLists.getToBeFnz(); }
  inline void setToBeFnz(GCObject* tbf) noexcept { gcLists.setToBeFnz(tbf); }
  inline GCObject** getToBeFnzPtr() noexcept { return gcLists.getToBeFnzPtr(); }

  inline GCObject* getFixedGC() const noexcept { return gcLists.getFixedGC(); }
  inline void setFixedGC(GCObject* fgc) noexcept { gcLists.setFixedGC(fgc); }
  inline GCObject** getFixedGCPtr() noexcept { return gcLists.getFixedGCPtr(); }

  /* Delegating accessors for GCObjectLists (generational) */
  inline GCObject* getSurvival() const noexcept { return gcLists.getSurvival(); }
  inline void setSurvival(GCObject* s) noexcept { gcLists.setSurvival(s); }
  inline GCObject** getSurvivalPtr() noexcept { return gcLists.getSurvivalPtr(); }

  inline GCObject* getOld1() const noexcept { return gcLists.getOld1(); }
  inline void setOld1(GCObject* o1) noexcept { gcLists.setOld1(o1); }
  inline GCObject** getOld1Ptr() noexcept { return gcLists.getOld1Ptr(); }

  inline GCObject* getReallyOld() const noexcept { return gcLists.getReallyOld(); }
  inline void setReallyOld(GCObject* ro) noexcept { gcLists.setReallyOld(ro); }
  inline GCObject** getReallyOldPtr() noexcept { return gcLists.getReallyOldPtr(); }

  inline GCObject* getFirstOld1() const noexcept { return gcLists.getFirstOld1(); }
  inline void setFirstOld1(GCObject* fo1) noexcept { gcLists.setFirstOld1(fo1); }
  inline GCObject** getFirstOld1Ptr() noexcept { return gcLists.getFirstOld1Ptr(); }

  inline GCObject* getFinObjSur() const noexcept { return gcLists.getFinObjSur(); }
  inline void setFinObjSur(GCObject* fos) noexcept { gcLists.setFinObjSur(fos); }
  inline GCObject** getFinObjSurPtr() noexcept { return gcLists.getFinObjSurPtr(); }

  inline GCObject* getFinObjOld1() const noexcept { return gcLists.getFinObjOld1(); }
  inline void setFinObjOld1(GCObject* fo1) noexcept { gcLists.setFinObjOld1(fo1); }
  inline GCObject** getFinObjOld1Ptr() noexcept { return gcLists.getFinObjOld1Ptr(); }

  inline GCObject* getFinObjROld() const noexcept { return gcLists.getFinObjROld(); }
  inline void setFinObjROld(GCObject* for_) noexcept { gcLists.setFinObjROld(for_); }
  inline GCObject** getFinObjROldPtr() noexcept { return gcLists.getFinObjROldPtr(); }

  /* Delegating accessors for StringCache */
  inline stringtable* getStringTable() noexcept { return strings.getTable(); }
  inline const stringtable* getStringTable() const noexcept { return strings.getTable(); }

  inline TString* getStrCache(int n, int m) const noexcept { return strings.getCache(n, m); }
  inline void setStrCache(int n, int m, TString* str) noexcept { strings.setCache(n, m, str); }

  /* Delegating accessors for TypeSystem */
  inline TValue* getRegistry() noexcept { return types.getRegistry(); }
  inline const TValue* getRegistry() const noexcept { return types.getRegistry(); }

  inline TValue* getNilValue() noexcept { return types.getNilValue(); }
  inline const TValue* getNilValue() const noexcept { return types.getNilValue(); }
  inline bool isComplete() const noexcept { return types.isComplete(); }

  inline unsigned int getSeed() const noexcept { return types.getSeed(); }
  inline void setSeed(unsigned int s) noexcept { types.setSeed(s); }

  inline Table* getMetatable(int type) const noexcept { return types.getMetatable(type); }
  inline void setMetatable(int type, Table* metatable) noexcept { types.setMetatable(type, metatable); }
  inline Table** getMetatablePtr(int type) noexcept { return types.getMetatablePtr(type); }

  inline TString* getTMName(int idx) const noexcept { return types.getTMName(idx); }
  inline void setTMName(int idx, TString* name) noexcept { types.setTMName(idx, name); }
  inline TString** getTMNamePtr(int idx) noexcept { return types.getTMNamePtr(idx); }

  /* Delegating accessors for RuntimeServices */
  inline lua_State* getTwups() const noexcept { return runtime.getTwups(); }
  inline void setTwups(lua_State* tw) noexcept { runtime.setTwups(tw); }
  inline lua_State** getTwupsPtr() noexcept { return runtime.getTwupsPtr(); }

  inline lua_CFunction getPanic() const noexcept { return runtime.getPanic(); }
  inline void setPanic(lua_CFunction p) noexcept { runtime.setPanic(p); }

  inline TString* getMemErrMsg() const noexcept { return runtime.getMemErrMsg(); }
  inline void setMemErrMsg(TString* msg) noexcept { runtime.setMemErrMsg(msg); }

  inline lua_WarnFunction getWarnF() const noexcept { return runtime.getWarnF(); }
  inline void setWarnF(lua_WarnFunction wf) noexcept { runtime.setWarnF(wf); }

  inline void* getUdWarn() const noexcept { return runtime.getUdWarn(); }
  inline void setUdWarn(void* uw) noexcept { runtime.setUdWarn(uw); }

  inline LX* getMainThread() noexcept { return runtime.getMainThread(); }
  inline const LX* getMainThread() const noexcept { return runtime.getMainThread(); }
};


/* Get global state from lua_State (returns reference to allow assignment) */
inline global_State*& G(lua_State* L) noexcept { return L->getGlobalStateRef(); }
inline global_State* G(const lua_State* L) noexcept { return const_cast<global_State*>(L->getGlobalState()); }

/* Get main thread from global_State */
inline lua_State* mainthread(global_State* g) noexcept { return &g->getMainThread()->l; }
inline const lua_State* mainthread(const global_State* g) noexcept { return &g->getMainThread()->l; }

// Phase 88: Define gfasttm() and fasttm() inline functions (declared in ltm.h)
// Must be defined here after global_State is fully defined
inline const TValue* gfasttm(global_State* g, const Table* mt, TMS e) noexcept {
	return checknoTM(mt, e) ? nullptr : luaT_gettm(const_cast<Table*>(mt), e, g->getTMName(e));
}

inline const TValue* fasttm(lua_State* l, const Table* mt, TMS e) noexcept {
	return gfasttm(G(l), mt, e);
}

/*
** GCUnion and cast_u removed (no longer needed)
** All GC object conversions now use type-safe reinterpret_cast via gco2* functions.
** The old GCUnion was only used for pointer casting, not actual memory allocation.
** Each GC type inherits from GCBase<T> and has proper memory layout.
*/

/* Convert GCObject to specific types using reinterpret_cast */
inline TString* gco2ts(GCObject* o) noexcept {
	lua_assert(novariant(o->getType()) == LUA_TSTRING);
	return reinterpret_cast<TString*>(o);
}

inline Udata* gco2u(GCObject* o) noexcept {
	lua_assert(o->getType() == LUA_VUSERDATA);
	return reinterpret_cast<Udata*>(o);
}

inline LClosure* gco2lcl(GCObject* o) noexcept {
	lua_assert(o->getType() == LUA_VLCL);
	return reinterpret_cast<LClosure*>(o);
}

inline CClosure* gco2ccl(GCObject* o) noexcept {
	lua_assert(o->getType() == LUA_VCCL);
	return reinterpret_cast<CClosure*>(o);
}

inline Closure* gco2cl(GCObject* o) noexcept {
	lua_assert(novariant(o->getType()) == LUA_TFUNCTION);
	return reinterpret_cast<Closure*>(o);
}

inline Table* gco2t(GCObject* o) noexcept {
	lua_assert(o->getType() == LUA_VTABLE);
	return reinterpret_cast<Table*>(o);
}

inline Proto* gco2p(GCObject* o) noexcept {
	lua_assert(o->getType() == LUA_VPROTO);
	return reinterpret_cast<Proto*>(o);
}

inline lua_State* gco2th(GCObject* o) noexcept {
	lua_assert(o->getType() == LUA_VTHREAD);
	return reinterpret_cast<lua_State*>(o);
}

inline UpVal* gco2upv(GCObject* o) noexcept {
	lua_assert(o->getType() == LUA_VUPVAL);
	return reinterpret_cast<UpVal*>(o);
}


/*
** Convert a Lua object to GCObject using reinterpret_cast
** Note: Returns non-const even for const input (for GC marking compatibility)
*/
inline GCObject* obj2gco(void* v) noexcept {
	return reinterpret_cast<GCObject*>(v);
}

inline GCObject* obj2gco(const void* v) noexcept {
	return reinterpret_cast<GCObject*>(const_cast<void*>(v));
}


LUAI_FUNC void luaE_setdebt (global_State *g, l_mem debt);
LUAI_FUNC void luaE_freethread (lua_State *L, lua_State *L1);
LUAI_FUNC lu_mem luaE_threadsize (lua_State *L);
LUAI_FUNC CallInfo *luaE_extendCI (lua_State *L);
LUAI_FUNC void luaE_shrinkCI (lua_State *L);
LUAI_FUNC void luaE_checkcstack (lua_State *L);
LUAI_FUNC void luaE_incCstack (lua_State *L);
LUAI_FUNC void luaE_warning (lua_State *L, const char *msg, int tocont);
LUAI_FUNC void luaE_warnerror (lua_State *L, const char *where);
LUAI_FUNC TStatus luaE_resetthread (lua_State *L, TStatus status);


#endif

