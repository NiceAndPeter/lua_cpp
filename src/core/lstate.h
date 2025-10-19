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
};


#define get_nresults(cs)  (cast_int((cs) & CIST_NRESULTS) - 1)

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
** 'global state', shared by all threads of this state
*/
class global_State {
private:
  // Group 1: Memory allocator fields
  lua_Alloc frealloc;  /* function to reallocate memory */
  void *ud;         /* auxiliary data to 'frealloc' */

  // Group 2: GC memory accounting fields
  l_mem GCtotalbytes;  /* number of bytes currently allocated + debt */
  l_mem GCdebt;  /* bytes counted but not yet allocated */
  l_mem GCmarked;  /* number of objects marked in a GC cycle */
  l_mem GCmajorminor;  /* auxiliary counter to control major-minor shifts */

  // Group 3: String and value storage
  stringtable strt;  /* hash table for strings */
  TValue l_registry;
  TValue nilvalue;  /* a nil value */
  unsigned int seed;  /* randomized seed for hashes */

  // Group 4: GC parameters and state
  lu_byte gcparams[LUA_GCPN];
  lu_byte currentwhite;
  lu_byte gcstate;  /* state of garbage collector */
  lu_byte gckind;  /* kind of GC running */
  lu_byte gcstopem;  /* stops emergency collections */
  lu_byte gcstp;  /* control whether GC is running */
  lu_byte gcemergency;  /* true if this is an emergency collection */

  // Group 5: GC object lists (incremental collector)
  GCObject *allgc;  /* list of all collectable objects */
  GCObject **sweepgc;  /* current position of sweep in list */
  GCObject *finobj;  /* list of collectable objects with finalizers */
  GCObject *gray;  /* list of gray objects */
  GCObject *grayagain;  /* list of objects to be traversed atomically */
  GCObject *weak;  /* list of tables with weak values */
  GCObject *ephemeron;  /* list of ephemeron tables (weak keys) */
  GCObject *allweak;  /* list of all-weak tables */
  GCObject *tobefnz;  /* list of userdata to be GC */
  GCObject *fixedgc;  /* list of objects not to be collected */

  // Group 6: GC object lists (generational collector)
  GCObject *survival;  /* start of objects that survived one GC cycle */
  GCObject *old1;  /* start of old1 objects */
  GCObject *reallyold;  /* objects more than one cycle old ("really old") */
  GCObject *firstold1;  /* first OLD1 object in the list (if any) */
  GCObject *finobjsur;  /* list of survival objects with finalizers */
  GCObject *finobjold1;  /* list of old1 objects with finalizers */
  GCObject *finobjrold;  /* list of really old objects with finalizers */

  // Group 7: Runtime state
  struct lua_State *twups;  /* list of threads with open upvalues */
  lua_CFunction panic;  /* to be called in unprotected errors */
  TString *memerrmsg;  /* message for memory-allocation errors */
  TString *tmname[TM_N];  /* array with tag-method names */
  struct Table *mt[LUA_NUMTYPES];  /* metatables for basic types */
  TString *strcache[STRCACHE_N][STRCACHE_M];  /* cache for strings in API */
  lua_WarnFunction warnf;  /* warning function */
  void *ud_warn;         /* auxiliary data to 'warnf' */
  LX mainth;  /* main thread of this state */

public:
  // Group 1: Memory allocator accessors
  lua_Alloc getFrealloc() const noexcept { return frealloc; }
  void setFrealloc(lua_Alloc f) noexcept { frealloc = f; }
  void* getUd() const noexcept { return ud; }
  void setUd(void* u) noexcept { ud = u; }

  // Group 2: GC memory accounting accessors
  l_mem getGCTotalBytes() const noexcept { return GCtotalbytes; }
  void setGCTotalBytes(l_mem bytes) noexcept { GCtotalbytes = bytes; }
  l_mem& getGCTotalBytesRef() noexcept { return GCtotalbytes; }

  l_mem getGCDebt() const noexcept { return GCdebt; }
  void setGCDebt(l_mem debt) noexcept { GCdebt = debt; }
  l_mem& getGCDebtRef() noexcept { return GCdebt; }

  l_mem getTotalBytes() const noexcept { return GCtotalbytes - GCdebt; }

  l_mem getGCMarked() const noexcept { return GCmarked; }
  void setGCMarked(l_mem marked) noexcept { GCmarked = marked; }
  l_mem& getGCMarkedRef() noexcept { return GCmarked; }

  l_mem getGCMajorMinor() const noexcept { return GCmajorminor; }
  void setGCMajorMinor(l_mem mm) noexcept { GCmajorminor = mm; }
  l_mem& getGCMajorMinorRef() noexcept { return GCmajorminor; }

  // Group 3: String and value storage accessors
  stringtable* getStringTable() noexcept { return &strt; }
  const stringtable* getStringTable() const noexcept { return &strt; }

  TValue* getRegistry() noexcept { return &l_registry; }
  const TValue* getRegistry() const noexcept { return &l_registry; }

  TValue* getNilValue() noexcept { return &nilvalue; }
  const TValue* getNilValue() const noexcept { return &nilvalue; }

  bool isComplete() const noexcept { return ttisnil(&nilvalue); }

  unsigned int getSeed() const noexcept { return seed; }
  void setSeed(unsigned int s) noexcept { seed = s; }

  // Group 4: GC parameters and state accessors
  lu_byte* getGCParams() noexcept { return gcparams; }
  const lu_byte* getGCParams() const noexcept { return gcparams; }
  lu_byte getGCParam(int idx) const noexcept { return gcparams[idx]; }
  void setGCParam(int idx, lu_byte value) noexcept { gcparams[idx] = value; }

  lu_byte getCurrentWhite() const noexcept { return currentwhite; }
  void setCurrentWhite(lu_byte cw) noexcept { currentwhite = cw; }
  lu_byte getWhite() const noexcept;  // Defined in lgc.h (needs WHITEBITS)

  GCState getGCState() const noexcept { return static_cast<GCState>(gcstate); }
  void setGCState(GCState state) noexcept { gcstate = static_cast<lu_byte>(state); }
  bool keepInvariant() const noexcept;  // Defined in lgc.h (needs GCState::Atomic)

  GCKind getGCKind() const noexcept { return static_cast<GCKind>(gckind); }
  void setGCKind(GCKind kind) noexcept { gckind = static_cast<lu_byte>(kind); }

  lu_byte getGCStopEm() const noexcept { return gcstopem; }
  void setGCStopEm(lu_byte stop) noexcept { gcstopem = stop; }

  lu_byte getGCStp() const noexcept { return gcstp; }
  void setGCStp(lu_byte stp) noexcept { gcstp = stp; }
  bool isGCRunning() const noexcept { return gcstp == 0; }

  lu_byte getGCEmergency() const noexcept { return gcemergency; }
  void setGCEmergency(lu_byte em) noexcept { gcemergency = em; }

  // Group 5: GC object lists (incremental) accessors
  GCObject* getAllGC() const noexcept { return allgc; }
  void setAllGC(GCObject* gc) noexcept { allgc = gc; }
  GCObject** getAllGCPtr() noexcept { return &allgc; }

  GCObject** getSweepGC() const noexcept { return sweepgc; }
  void setSweepGC(GCObject** sweep) noexcept { sweepgc = sweep; }
  GCObject*** getSweepGCPtr() noexcept { return &sweepgc; }

  GCObject* getFinObj() const noexcept { return finobj; }
  void setFinObj(GCObject* fobj) noexcept { finobj = fobj; }
  GCObject** getFinObjPtr() noexcept { return &finobj; }

  GCObject* getGray() const noexcept { return gray; }
  void setGray(GCObject* g) noexcept { gray = g; }
  GCObject** getGrayPtr() noexcept { return &gray; }

  GCObject* getGrayAgain() const noexcept { return grayagain; }
  void setGrayAgain(GCObject* ga) noexcept { grayagain = ga; }
  GCObject** getGrayAgainPtr() noexcept { return &grayagain; }

  GCObject* getWeak() const noexcept { return weak; }
  void setWeak(GCObject* w) noexcept { weak = w; }
  GCObject** getWeakPtr() noexcept { return &weak; }

  GCObject* getEphemeron() const noexcept { return ephemeron; }
  void setEphemeron(GCObject* e) noexcept { ephemeron = e; }
  GCObject** getEphemeronPtr() noexcept { return &ephemeron; }

  GCObject* getAllWeak() const noexcept { return allweak; }
  void setAllWeak(GCObject* aw) noexcept { allweak = aw; }
  GCObject** getAllWeakPtr() noexcept { return &allweak; }

  GCObject* getToBeFnz() const noexcept { return tobefnz; }
  void setToBeFnz(GCObject* tbf) noexcept { tobefnz = tbf; }
  GCObject** getToBeFnzPtr() noexcept { return &tobefnz; }

  GCObject* getFixedGC() const noexcept { return fixedgc; }
  void setFixedGC(GCObject* fgc) noexcept { fixedgc = fgc; }
  GCObject** getFixedGCPtr() noexcept { return &fixedgc; }

  // Group 6: GC object lists (generational) accessors
  GCObject* getSurvival() const noexcept { return survival; }
  void setSurvival(GCObject* s) noexcept { survival = s; }
  GCObject** getSurvivalPtr() noexcept { return &survival; }

  GCObject* getOld1() const noexcept { return old1; }
  void setOld1(GCObject* o1) noexcept { old1 = o1; }
  GCObject** getOld1Ptr() noexcept { return &old1; }

  GCObject* getReallyOld() const noexcept { return reallyold; }
  void setReallyOld(GCObject* ro) noexcept { reallyold = ro; }
  GCObject** getReallyOldPtr() noexcept { return &reallyold; }

  GCObject* getFirstOld1() const noexcept { return firstold1; }
  void setFirstOld1(GCObject* fo1) noexcept { firstold1 = fo1; }
  GCObject** getFirstOld1Ptr() noexcept { return &firstold1; }

  GCObject* getFinObjSur() const noexcept { return finobjsur; }
  void setFinObjSur(GCObject* fos) noexcept { finobjsur = fos; }
  GCObject** getFinObjSurPtr() noexcept { return &finobjsur; }

  GCObject* getFinObjOld1() const noexcept { return finobjold1; }
  void setFinObjOld1(GCObject* fo1) noexcept { finobjold1 = fo1; }
  GCObject** getFinObjOld1Ptr() noexcept { return &finobjold1; }

  GCObject* getFinObjROld() const noexcept { return finobjrold; }
  void setFinObjROld(GCObject* for_) noexcept { finobjrold = for_; }
  GCObject** getFinObjROldPtr() noexcept { return &finobjrold; }

  // Group 7: Runtime state accessors
  lua_State* getTwups() const noexcept { return twups; }
  void setTwups(lua_State* tw) noexcept { twups = tw; }
  lua_State** getTwupsPtr() noexcept { return &twups; }

  lua_CFunction getPanic() const noexcept { return panic; }
  void setPanic(lua_CFunction p) noexcept { panic = p; }

  TString* getMemErrMsg() const noexcept { return memerrmsg; }
  void setMemErrMsg(TString* msg) noexcept { memerrmsg = msg; }

  TString* getTMName(int idx) const noexcept { return tmname[idx]; }
  void setTMName(int idx, TString* name) noexcept { tmname[idx] = name; }
  TString** getTMNamePtr(int idx) noexcept { return &tmname[idx]; }

  Table* getMetatable(int type) const noexcept { return mt[type]; }
  void setMetatable(int type, Table* metatable) noexcept { mt[type] = metatable; }
  Table** getMetatablePtr(int type) noexcept { return &mt[type]; }

  TString* getStrCache(int n, int m) const noexcept { return strcache[n][m]; }
  void setStrCache(int n, int m, TString* str) noexcept { strcache[n][m] = str; }

  lua_WarnFunction getWarnF() const noexcept { return warnf; }
  void setWarnF(lua_WarnFunction wf) noexcept { warnf = wf; }

  void* getUdWarn() const noexcept { return ud_warn; }
  void setUdWarn(void* uw) noexcept { ud_warn = uw; }

  LX* getMainThread() noexcept { return &mainth; }
  const LX* getMainThread() const noexcept { return &mainth; }
};


/* Get global state from lua_State (returns reference to allow assignment) */
inline global_State*& G(lua_State* L) noexcept { return L->getGlobalStateRef(); }
inline global_State* G(const lua_State* L) noexcept { return const_cast<global_State*>(L->getGlobalState()); }

/* Get main thread from global_State */
inline lua_State* mainthread(global_State* g) noexcept { return &g->getMainThread()->l; }
inline const lua_State* mainthread(const global_State* g) noexcept { return &g->getMainThread()->l; }

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

