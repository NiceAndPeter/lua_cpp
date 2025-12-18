/*
** $Id: lstate.c $
** Global State
** See Copyright Notice in lua.h
*/

#define lstate_c
#define LUA_CORE

#include "lprefix.h"


#include <algorithm>
#include <cstddef>
#include <cstring>

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "../vm/lvirtualmachine.h"



// Replace offsetof with constexpr calculation for non-standard-layout type
inline constexpr size_t lxOffset() noexcept {
  // LX has: extra_[LUA_EXTRASPACE] + lua_State l
  // lua_State inherits from GCBase, so offset is just the extra_ array size
  return LUA_EXTRASPACE;
}

inline LX* fromstate(lua_State* L) noexcept {
  return reinterpret_cast<LX*>(reinterpret_cast<lu_byte*>(L) - lxOffset());
}


/*
** these macros allow user-specific actions when a thread is
** created/deleted
*/
#if !defined(luai_userstateopen)
#define luai_userstateopen(L)		((void)L)
#endif

#if !defined(luai_userstateclose)
#define luai_userstateclose(L)		((void)L)
#endif

#if !defined(luai_userstatethread)
#define luai_userstatethread(L,L1)	((void)L)
#endif

#if !defined(luai_userstatefree)
#define luai_userstatefree(L,L1)	((void)L)
#endif


/*
** set GCdebt to a new value keeping the real number of allocated
** objects (GCtotalobjs - GCdebt) invariant and avoiding overflows in
** 'GCtotalobjs'.
*/
void luaE_setdebt (global_State *g, l_mem debt) {
  l_mem tb = g->getTotalBytes();
  lua_assert(tb > 0);
  if (debt > MAX_LMEM - tb)
    debt = MAX_LMEM - tb;  /* will make GCtotalbytes == MAX_LMEM */
  g->setGCTotalBytes(tb + debt);
  g->setGCDebt(debt);
}


CallInfo *luaE_extendCI (lua_State *L) {
  CallInfo *ci;
  lua_assert(L->getCI()->getNext() == nullptr);
  // Use placement new to call constructor (initializes all 9 fields)
  ci = new (luaM_malloc_(L, sizeof(CallInfo), 0)) CallInfo();
  lua_assert(L->getCI()->getNext() == nullptr);
  L->getCI()->setNext(ci);
  ci->setPrevious(L->getCI());
  ci->setNext(nullptr);
  // trap already initialized to 0 in constructor, but keep this for clarity
  ci->getTrap() = 0;
  L->getNumberOfCallInfosRef()++;
  return ci;
}


/*
** free all CallInfo structures not in use by a thread
*/
void lua_State::freeCI() {
  CallInfo *callInfo = getCI();
  CallInfo *nextCallInfo = callInfo->getNext();
  callInfo->setNext(nullptr);
  while ((callInfo = nextCallInfo) != nullptr) {
    nextCallInfo = callInfo->getNext();
    luaM_free(this, callInfo);
    getNumberOfCallInfosRef()--;
  }
}


/*
** free half of the CallInfo structures not in use by a thread,
** keeping the first one.
*/
void luaE_shrinkCI (lua_State *L) {
  CallInfo *ci = L->getCI()->getNext();  /* first free CallInfo */
  CallInfo *next;
  if (ci == nullptr)
    return;  /* no extra elements */
  while ((next = ci->getNext()) != nullptr) {  /* two extra elements? */
    CallInfo *next2 = next->getNext();  /* next's next */
    ci->setNext(next2);  /* remove next from the list */
    L->getNumberOfCallInfosRef()--;
    luaM_free(L, next);  /* free next */
    if (next2 == nullptr)
      break;  /* no more elements */
    else {
      next2->setPrevious(ci);
      ci = next2;  /* continue */
    }
  }
}


/*
** Called when 'getCcalls(L)' larger or equal to LUAI_MAXCCALLS.
** If equal, raises an overflow error. If value is larger than
** LUAI_MAXCCALLS (which means it is handling an overflow) but
** not much larger, does not report an error (to allow overflow
** handling to work).
*/
void luaE_checkcstack (lua_State *L) {
  if (getCcalls(L) == LUAI_MAXCCALLS)
    luaG_runerror(L, "C stack overflow");
  else if (getCcalls(L) >= (LUAI_MAXCCALLS / 10 * 11))
    L->errorError();  /* error while handling stack error */
}


LUAI_FUNC void luaE_incCstack (lua_State *L) {
  L->getNumberOfCCallsRef()++;
  if (l_unlikely(getCcalls(L) >= LUAI_MAXCCALLS))
    luaE_checkcstack(L);
}


void lua_State::resetCI() {
  CallInfo *callInfo = setCI(getBaseCI());
  callInfo->funcRef().p = getStack().p;
  setnilvalue(s2v(callInfo->funcRef().p));  /* 'function' entry for basic 'ci' */
  callInfo->topRef().p = callInfo->funcRef().p + 1 + LUA_MINSTACK;  /* +1 for 'function' entry */
  callInfo->setK(nullptr);
  callInfo->setCallStatus(CIST_C);
  setStatus(LUA_OK);
  setErrFunc(0);  /* stack unwind can "throw away" the error function */
}


void lua_State::stack_init(lua_State *L) {
  /* initialize stack array via LuaStack subsystem */
  getStackSubsystem().init(L);
  /* initialize first ci */
  resetCI();
  getStackSubsystem().setTopPtr(getStack().p + 1);  /* +1 for 'function' entry */
}


void lua_State::freestack() {
  setCI(getBaseCI());  /* free the entire 'ci' list */
  freeCI();
  lua_assert(getNumberOfCallInfos() == 0);
  /* free stack via LuaStack subsystem */
  getStackSubsystem().free(this);
}


/*
** Create registry table and its predefined values
*/
void lua_State::init_registry(global_State *g) {
  /* create registry */
  TValue aux;
  Table *registry = Table::create(this);
  sethvalue(this, g->getRegistry(), registry);
  registry->resize(this, LUA_RIDX_LAST, 0);
  /* registry[1] = false */
  setbfvalue(&aux);
  registry->setInt(this, 1, &aux);
  /* registry[LUA_RIDX_MAINTHREAD] = L */
  setthvalue(this, &aux, this);
  registry->setInt(this, LUA_RIDX_MAINTHREAD, &aux);
  /* registry[LUA_RIDX_GLOBALS] = new table (table of globals) */
  sethvalue(this, &aux, Table::create(this));
  registry->setInt(this, LUA_RIDX_GLOBALS, &aux);
}


/*
** open parts of the state that may cause memory-allocation errors.
** Phase 2B: Friend function (callback needs to access private methods)
*/
void f_luaopen(lua_State *L, void *ud) {
  global_State *g = G(L);
  UNUSED(ud);
  L->stack_init(L);  /* init stack */
  // Phase 122: Allocate VirtualMachine (after stack, as VM may use stack operations)
  L->initVM();
  L->init_registry(g);
  TString::init(L);
  luaT_init(L);
  luaX_init(L);
  g->setGCStp(0);  /* allow gc */
  setnilvalue(g->getNilValue());  /* now state is complete */
  luai_userstateopen(L);
}


/*
** preinitialize a thread with consistent values without allocating
** any memory (to avoid errors)
**
** IMPORTANT: GC fields (next, tt, marked) must be set by caller BEFORE
** calling this function. The init() method preserves them.
*/
void lua_State::preinit_thread(global_State *g) {
  init(g);  // Initialize lua_State fields (preserves GC fields)
  resetHookCount();   // Initialize hookcount = basehookcount
  getBaseCI()->setPrevious(nullptr);
  getBaseCI()->setNext(nullptr);
}


/*
** Phase 122: VM lifecycle management
*/

void lua_State::initVM() {
  // Allocate VirtualMachine using placement new to avoid C++ new operator
  // Use luaM_new for Lua's memory management
  vm_ = new VirtualMachine(this);
}

void lua_State::closeVM() {
  if (vm_ != nullptr) {
    delete vm_;
    vm_ = nullptr;
  }
}


lu_mem luaE_threadsize (lua_State *L) {
  lu_mem sz = static_cast<lu_mem>(sizeof(LX))
            + cast_uint(L->getNumberOfCallInfos()) * sizeof(CallInfo);
  if (L->getStack().p != nullptr) {
    /* Account for BOTH the values array and deltas array (single-block allocation) */
    int allocated_size = L->getStackSize() + EXTRA_STACK;
    sz += cast_uint(allocated_size) * sizeof(StackValue);  /* values array */
    sz += cast_uint(allocated_size) * sizeof(unsigned short);  /* deltas array */
  }
  return sz;
}


void lua_State::close_state() {
  global_State *g = G(this);
  if (!g->isComplete())  /* closing a partially built state? */
    luaC_freeallobjects(*this);  /* just collect its objects */
  else {  /* closing a fully built state */
    resetCI();
    (void)closeProtected( 1, LUA_OK);  /* close all upvalues - ignore status during shutdown */
    getStackSubsystem().setTopPtr(getStack().p + 1);  /* empty the stack to run finalizers */
    luaC_freeallobjects(*this);  /* collect all objects */
    luai_userstateclose(this);
  }
  luaM_freearray(this, G(this)->getStringTable()->getHash(), cast_sizet(G(this)->getStringTable()->getSize()));
  closeVM();  // Phase 122: Free VirtualMachine before freeing stack
  freestack();
  lua_assert(g->getTotalBytes() == sizeof(global_State));
  (*g->getFrealloc())(g->getUd(), g, sizeof(global_State), 0);  /* free main block */
}


LUA_API lua_State *lua_newthread (lua_State *L) {
  global_State *g = G(L);
  GCObject *o;
  lua_State *L1;
  lua_lock(L);
  luaC_checkGC(L);
  /* create new thread */
  o = luaC_newobjdt(*L, ctb(LuaT::THREAD), sizeof(LX), lxOffset());
  L1 = gco2th(o);
  /* anchor it on L stack */
  setthvalue2s(L, L->getTop().p, L1);
  api_incr_top(L);
  L1->preinit_thread(g);
  L1->setHookMask(L->getHookMask());
  L1->setBaseHookCount(L->getBaseHookCount());
  L1->setHook(L->getHook());
  L1->resetHookCount();
  /* initialize L1 extra space */
  memcpy(lua_getextraspace(L1), lua_getextraspace(mainthread(g)),
         LUA_EXTRASPACE);
  luai_userstatethread(L, L1);
  L1->stack_init(L);  /* init stack */
  L1->initVM();  // Phase 122: Allocate VirtualMachine for new thread
  lua_unlock(L);
  return L1;
}


void luaE_freethread (lua_State *L, lua_State *L1) {
  LX *l = fromstate(L1);
  luaF_closeupval(L1, L1->getStack().p);  /* close all upvalues */
  lua_assert(L1->getOpenUpval() == nullptr);
  luai_userstatefree(L, L1);
  L1->closeVM();  // Phase 122: Free VirtualMachine before freeing stack
  L1->freestack();
  luaM_free(L, l);
}


TStatus luaE_resetthread (lua_State *L, TStatus status) {
  L->resetCI();
  if (status == LUA_YIELD)
    status = LUA_OK;
  status = L->closeProtected( 1, status);
  if (status != LUA_OK)  /* errors? */
    L->setErrorObj( status, L->getStack().p + 1);
  else
    L->getStackSubsystem().setTopPtr(L->getStack().p + 1);
  if (!L->reallocStack(cast_int(L->getCI()->topRef().p - L->getStack().p), 0))
    status = LUA_ERRMEM;  /* stack reallocation failed */
  return status;
}


LUA_API int lua_closethread (lua_State *L, lua_State *from) {
  TStatus status;
  lua_lock(L);
  L->setNumberOfCCalls((from) ? getCcalls(from) : 0);
  status = luaE_resetthread(L, L->getStatus());
  if (L == from)  /* closing itself? */
    L->throwBaseLevel( status);
  lua_unlock(L);
  return APIstatus(status);
}


LUA_API lua_State *lua_newstate (lua_Alloc f, void *ud, unsigned seed) {
  lua_State *L;
  global_State *g = static_cast<global_State*>(
                       (*f)(ud, nullptr, LUA_TTHREAD, sizeof(global_State)));
  if (g == nullptr) return nullptr;
  L = &g->getMainThread()->l;
  L->setType(ctb(LuaT::THREAD));
  g->setCurrentWhite(bitmask(WHITE0BIT));
  L->setMarked(g->getWhite());
  L->preinit_thread(g);
  g->setAllGC(obj2gco(L));  /* by now, only object is the main thread */
  L->setNext(nullptr);
  incnny(L);  /* main thread is always non yieldable */
  g->setFrealloc(f);
  g->setUd(ud);
  g->setWarnF(nullptr);
  g->setUdWarn(nullptr);
  g->setSeed(seed);
  g->setGCStp(GCSTPGC);  /* no GC while building state */
  g->getStringTable()->setSize(0);
  g->getStringTable()->setNumElements(0);
  g->getStringTable()->setHash(nullptr);
  setnilvalue(g->getRegistry());
  g->setPanic(nullptr);
  g->setGCState(GCState::Pause);
  g->setGCKind(GCKind::Incremental);
  g->setGCStopEm(0);
  g->setGCEmergency(0);
  g->setFinObj(nullptr); g->setToBeFnz(nullptr); g->setFixedGC(nullptr);
  g->setFirstOld1(nullptr); g->setSurvival(nullptr); g->setOld1(nullptr); g->setReallyOld(nullptr);
  g->setFinObjSur(nullptr); g->setFinObjOld1(nullptr); g->setFinObjROld(nullptr);
  g->setSweepGC(nullptr);
  g->setGray(nullptr); g->setGrayAgain(nullptr);
  g->setWeak(nullptr); g->setEphemeron(nullptr); g->setAllWeak(nullptr);
  g->setTwups(nullptr);
  g->setGCTotalBytes(sizeof(global_State));
  g->setGCMarked(0);
  g->setGCDebt(0);
  g->getNilValue()->setInt(0);  /* to signal that state is not yet built */
  setgcparam(g, PAUSE, LUAI_GCPAUSE);
  setgcparam(g, STEPMUL, LUAI_GCMUL);
  setgcparam(g, STEPSIZE, LUAI_GCSTEPSIZE);
  setgcparam(g, MINORMUL, LUAI_GENMINORMUL);
  setgcparam(g, MINORMAJOR, LUAI_MINORMAJOR);
  setgcparam(g, MAJORMINOR, LUAI_MAJORMINOR);
  for (int typeIndex = 0; typeIndex < LUA_NUMTYPES; typeIndex++) {
    g->setMetatable(typeIndex, nullptr);
  }
  if (L->rawRunProtected( f_luaopen, nullptr) != LUA_OK) {
    /* memory allocation error: free partial state */
    L->close_state();
    L = nullptr;
  }
  return L;
}


LUA_API void lua_close (lua_State *L) {
  lua_lock(L);
  L = mainthread(G(L));  /* only the main thread can be closed */
  L->close_state();
}


void luaE_warning (lua_State *L, const char *msg, int tocont) {
  lua_WarnFunction wf = G(L)->getWarnF();
  if (wf != nullptr)
    wf(G(L)->getUdWarn(), msg, tocont);
}


/*
** Generate a warning from an error message
*/
void luaE_warnerror (lua_State *L, const char *where) {
  TValue *errobj = s2v(L->getTop().p - 1);  /* error object */
  const char *msg = (ttisstring(errobj))
                  ? getStringContents(tsvalue(errobj))
                  : "error object is not a string";
  /* produce warning "error in %s (%s)" (where, msg) */
  luaE_warning(L, "error in ", 1);
  luaE_warning(L, where, 1);
  luaE_warning(L, " (", 1);
  luaE_warning(L, msg, 1);
  luaE_warning(L, ")", 0);
}

