/*
** $Id: lstate.c $
** Global State
** See Copyright Notice in lua.h
*/

#define lstate_c
#define LUA_CORE

#include "lprefix.h"


#include <stddef.h>
#include <string.h>

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



// Replace offsetof with constexpr calculation for non-standard-layout type
inline constexpr size_t lxOffset() noexcept {
  // LX has: extra_[LUA_EXTRASPACE] + lua_State l
  // lua_State inherits from GCBase, so offset is just the extra_ array size
  return LUA_EXTRASPACE;
}

inline LX* fromstate(lua_State* L) noexcept {
  return cast(LX *, cast(lu_byte *, (L)) - lxOffset());
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
  l_mem tb = gettotalbytes(g);
  lua_assert(tb > 0);
  if (debt > MAX_LMEM - tb)
    debt = MAX_LMEM - tb;  /* will make GCtotalbytes == MAX_LMEM */
  g->GCtotalbytes = tb + debt;
  g->GCdebt = debt;
}


CallInfo *luaE_extendCI (lua_State *L) {
  CallInfo *ci;
  lua_assert(L->getCI()->getNext() == NULL);
  ci = luaM_new(L, CallInfo);
  lua_assert(L->getCI()->getNext() == NULL);
  L->getCI()->setNext(ci);
  ci->setPrevious(L->getCI());
  ci->setNext(NULL);
  ci->getTrap() = 0;
  L->getNCIRef()++;
  return ci;
}


/*
** free all CallInfo structures not in use by a thread
*/
static void freeCI (lua_State *L) {
  CallInfo *ci = L->getCI();
  CallInfo *next = ci->getNext();
  ci->setNext(NULL);
  while ((ci = next) != NULL) {
    next = ci->getNext();
    luaM_free(L, ci);
    L->getNCIRef()--;
  }
}


/*
** free half of the CallInfo structures not in use by a thread,
** keeping the first one.
*/
void luaE_shrinkCI (lua_State *L) {
  CallInfo *ci = L->getCI()->getNext();  /* first free CallInfo */
  CallInfo *next;
  if (ci == NULL)
    return;  /* no extra elements */
  while ((next = ci->getNext()) != NULL) {  /* two extra elements? */
    CallInfo *next2 = next->getNext();  /* next's next */
    ci->setNext(next2);  /* remove next from the list */
    L->getNCIRef()--;
    luaM_free(L, next);  /* free next */
    if (next2 == NULL)
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
  L->getNCcallsRef()++;
  if (l_unlikely(getCcalls(L) >= LUAI_MAXCCALLS))
    luaE_checkcstack(L);
}


static void resetCI (lua_State *L) {
  CallInfo *ci = L->setCI(L->getBaseCI());
  ci->funcRef().p = L->getStack().p;
  setnilvalue(s2v(ci->funcRef().p));  /* 'function' entry for basic 'ci' */
  ci->topRef().p = ci->funcRef().p + 1 + LUA_MINSTACK;  /* +1 for 'function' entry */
  ci->setK(NULL);
  ci->setCallStatus(CIST_C);
  L->setStatus(LUA_OK);
  L->setErrFunc(0);  /* stack unwind can "throw away" the error function */
}


static void stack_init (lua_State *L1, lua_State *L) {
  int i;
  /* initialize stack array */
  L1->getStack().p = luaM_newvector(L, BASIC_STACK_SIZE + EXTRA_STACK, StackValue);
  L1->getTbclist().p = L1->getStack().p;
  for (i = 0; i < BASIC_STACK_SIZE + EXTRA_STACK; i++)
    setnilvalue(s2v(L1->getStack().p + i));  /* erase new stack */
  L1->getStackLast().p = L1->getStack().p + BASIC_STACK_SIZE;
  /* initialize first ci */
  resetCI(L1);
  L1->getTop().p = L1->getStack().p + 1;  /* +1 for 'function' entry */
}


static void freestack (lua_State *L) {
  if (L->getStack().p == NULL)
    return;  /* stack not completely built yet */
  L->setCI(L->getBaseCI());  /* free the entire 'ci' list */
  freeCI(L);
  lua_assert(L->getNCI() == 0);
  /* free stack */
  luaM_freearray(L, L->getStack().p, cast_sizet(stacksize(L) + EXTRA_STACK));
}


/*
** Create registry table and its predefined values
*/
static void init_registry (lua_State *L, global_State *g) {
  /* create registry */
  TValue aux;
  Table *registry = luaH_new(L);
  sethvalue(L, &g->l_registry, registry);
  luaH_resize(L, registry, LUA_RIDX_LAST, 0);
  /* registry[1] = false */
  setbfvalue(&aux);
  luaH_setint(L, registry, 1, &aux);
  /* registry[LUA_RIDX_MAINTHREAD] = L */
  setthvalue(L, &aux, L);
  luaH_setint(L, registry, LUA_RIDX_MAINTHREAD, &aux);
  /* registry[LUA_RIDX_GLOBALS] = new table (table of globals) */
  sethvalue(L, &aux, luaH_new(L));
  luaH_setint(L, registry, LUA_RIDX_GLOBALS, &aux);
}


/*
** open parts of the state that may cause memory-allocation errors.
*/
static void f_luaopen (lua_State *L, void *ud) {
  global_State *g = G(L);
  UNUSED(ud);
  stack_init(L, L);  /* init stack */
  init_registry(L, g);
  luaS_init(L);
  luaT_init(L);
  luaX_init(L);
  g->gcstp = 0;  /* allow gc */
  setnilvalue(&g->nilvalue);  /* now state is complete */
  luai_userstateopen(L);
}


/*
** preinitialize a thread with consistent values without allocating
** any memory (to avoid errors)
*/
static void preinit_thread (lua_State *L, global_State *g) {
  G(L) = g;
  L->getStack().p = NULL;
  L->setCI(NULL);
  L->setNCI(0);
  L->setTwups(L);  /* thread has no upvalues */
  L->setNCcalls(0);
  L->setErrorJmp(NULL);
  L->setHook(NULL);
  L->setHookMask(0);
  L->setBaseHookCount(0);
  L->setAllowHook(1);
  resethookcount(L);
  L->setOpenUpval(NULL);
  L->setStatus(LUA_OK);
  L->setErrFunc(0);
  L->setOldPC(0);
  L->getBaseCI()->setPrevious(NULL);
  L->getBaseCI()->setNext(NULL);
}


lu_mem luaE_threadsize (lua_State *L) {
  lu_mem sz = cast(lu_mem, sizeof(LX))
            + cast_uint(L->getNCI()) * sizeof(CallInfo);
  if (L->getStack().p != NULL)
    sz += cast_uint(stacksize(L) + EXTRA_STACK) * sizeof(StackValue);
  return sz;
}


static void close_state (lua_State *L) {
  global_State *g = G(L);
  if (!completestate(g))  /* closing a partially built state? */
    luaC_freeallobjects(L);  /* just collect its objects */
  else {  /* closing a fully built state */
    resetCI(L);
    L->closeProtected( 1, LUA_OK);  /* close all upvalues */
    L->getTop().p = L->getStack().p + 1;  /* empty the stack to run finalizers */
    luaC_freeallobjects(L);  /* collect all objects */
    luai_userstateclose(L);
  }
  luaM_freearray(L, G(L)->strt.getHash(), cast_sizet(G(L)->strt.getSize()));
  freestack(L);
  lua_assert(gettotalbytes(g) == sizeof(global_State));
  (*g->frealloc)(g->ud, g, sizeof(global_State), 0);  /* free main block */
}


LUA_API lua_State *lua_newthread (lua_State *L) {
  global_State *g = G(L);
  GCObject *o;
  lua_State *L1;
  lua_lock(L);
  luaC_checkGC(L);
  /* create new thread */
  o = luaC_newobjdt(L, LUA_TTHREAD, sizeof(LX), lxOffset());
  L1 = gco2th(o);
  /* anchor it on L stack */
  setthvalue2s(L, L->getTop().p, L1);
  api_incr_top(L);
  preinit_thread(L1, g);
  L1->setHookMask(L->getHookMask());
  L1->setBaseHookCount(L->getBaseHookCount());
  L1->setHook(L->getHook());
  resethookcount(L1);
  /* initialize L1 extra space */
  memcpy(lua_getextraspace(L1), lua_getextraspace(mainthread(g)),
         LUA_EXTRASPACE);
  luai_userstatethread(L, L1);
  stack_init(L1, L);  /* init stack */
  lua_unlock(L);
  return L1;
}


void luaE_freethread (lua_State *L, lua_State *L1) {
  LX *l = fromstate(L1);
  luaF_closeupval(L1, L1->getStack().p);  /* close all upvalues */
  lua_assert(L1->getOpenUpval() == NULL);
  luai_userstatefree(L, L1);
  freestack(L1);
  luaM_free(L, l);
}


TStatus luaE_resetthread (lua_State *L, TStatus status) {
  resetCI(L);
  if (status == LUA_YIELD)
    status = LUA_OK;
  status = L->closeProtected( 1, status);
  if (status != LUA_OK)  /* errors? */
    L->setErrorObj( status, L->getStack().p + 1);
  else
    L->getTop().p = L->getStack().p + 1;
  L->reallocStack(cast_int(L->getCI()->topRef().p - L->getStack().p), 0);
  return status;
}


LUA_API int lua_closethread (lua_State *L, lua_State *from) {
  TStatus status;
  lua_lock(L);
  L->setNCcalls((from) ? getCcalls(from) : 0);
  status = luaE_resetthread(L, L->getStatus());
  if (L == from)  /* closing itself? */
    L->throwBaseLevel( status);
  lua_unlock(L);
  return APIstatus(status);
}


LUA_API lua_State *lua_newstate (lua_Alloc f, void *ud, unsigned seed) {
  int i;
  lua_State *L;
  global_State *g = cast(global_State*,
                       (*f)(ud, NULL, LUA_TTHREAD, sizeof(global_State)));
  if (g == NULL) return NULL;
  L = &g->mainth.l;
  L->setType(LUA_VTHREAD);
  g->currentwhite = bitmask(WHITE0BIT);
  L->setMarked(luaC_white(g));
  preinit_thread(L, g);
  g->allgc = obj2gco(L);  /* by now, only object is the main thread */
  L->setNext(NULL);
  incnny(L);  /* main thread is always non yieldable */
  g->frealloc = f;
  g->ud = ud;
  g->warnf = NULL;
  g->ud_warn = NULL;
  g->seed = seed;
  g->gcstp = GCSTPGC;  /* no GC while building state */
  g->strt.setSize(0);
  g->strt.setNumElements(0);
  g->strt.setHash(NULL);
  setnilvalue(&g->l_registry);
  g->panic = NULL;
  g->gcstate = GCSpause;
  g->gckind = KGC_INC;
  g->gcstopem = 0;
  g->gcemergency = 0;
  g->finobj = g->tobefnz = g->fixedgc = NULL;
  g->firstold1 = g->survival = g->old1 = g->reallyold = NULL;
  g->finobjsur = g->finobjold1 = g->finobjrold = NULL;
  g->sweepgc = NULL;
  g->gray = g->grayagain = NULL;
  g->weak = g->ephemeron = g->allweak = NULL;
  g->twups = NULL;
  g->GCtotalbytes = sizeof(global_State);
  g->GCmarked = 0;
  g->GCdebt = 0;
  setivalue(&g->nilvalue, 0);  /* to signal that state is not yet built */
  setgcparam(g, PAUSE, LUAI_GCPAUSE);
  setgcparam(g, STEPMUL, LUAI_GCMUL);
  setgcparam(g, STEPSIZE, LUAI_GCSTEPSIZE);
  setgcparam(g, MINORMUL, LUAI_GENMINORMUL);
  setgcparam(g, MINORMAJOR, LUAI_MINORMAJOR);
  setgcparam(g, MAJORMINOR, LUAI_MAJORMINOR);
  for (i=0; i < LUA_NUMTYPES; i++) g->mt[i] = NULL;
  if (L->rawRunProtected( f_luaopen, NULL) != LUA_OK) {
    /* memory allocation error: free partial state */
    close_state(L);
    L = NULL;
  }
  return L;
}


LUA_API void lua_close (lua_State *L) {
  lua_lock(L);
  L = mainthread(G(L));  /* only the main thread can be closed */
  close_state(L);
}


void luaE_warning (lua_State *L, const char *msg, int tocont) {
  lua_WarnFunction wf = G(L)->warnf;
  if (wf != NULL)
    wf(G(L)->ud_warn, msg, tocont);
}


/*
** Generate a warning from an error message
*/
void luaE_warnerror (lua_State *L, const char *where) {
  TValue *errobj = s2v(L->getTop().p - 1);  /* error object */
  const char *msg = (ttisstring(errobj))
                  ? getstr(tsvalue(errobj))
                  : "error object is not a string";
  /* produce warning "error in %s (%s)" (where, msg) */
  luaE_warning(L, "error in ", 1);
  luaE_warning(L, where, 1);
  luaE_warning(L, " (", 1);
  luaE_warning(L, msg, 1);
  luaE_warning(L, ")", 0);
}

