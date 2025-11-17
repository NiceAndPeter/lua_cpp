/*
** $Id: lgc.c $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#define lgc_c
#define LUA_CORE

#include "lprefix.h"

#include <cstring>


#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "gc/gc_core.h"
#include "gc/gc_marking.h"
#include "gc/gc_sweeping.h"
#include "gc/gc_finalizer.h"
#include "gc/gc_weak.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"


/*
** Maximum number of elements to sweep in each single step.
** (Large enough to dissipate fixed overheads but small enough
** to allow small steps for the collector.)
*/
#define GCSWEEPMAX	20


/*
** Cost (in work units) of running one finalizer.
*/
#define CWUFIN	10


/*
** TRI-COLOR MARKING ALGORITHM:
**
** Lua uses a tri-color incremental mark-and-sweep garbage collector.
** Each object has one of three colors stored in its 'marked' field:
**
** WHITE: Not yet visited in this GC cycle (candidates for collection)
**   - Two white shades alternate between GC cycles to handle new allocations
**   - Objects allocated during marking use the "other" white shade
**   - At sweep time, only the "old" white shade is collected
**
** GRAY: Visited but not yet processed (in the work queue)
**   - Object is reachable but its children haven't been marked yet
**   - Stored in various gray lists (gray, grayagain, etc.)
**   - Ensures incremental progress: each work unit processes some gray objects
**
** BLACK: Visited and fully processed (definitely reachable)
**   - Object and all its children have been marked
**   - Collector invariant: no black object points to white object
**   - Barrier operations (write barriers) maintain this invariant
**
** INCREMENTAL COLLECTION:
** Instead of stopping the world, Lua interleaves GC work with program execution.
** The tri-color scheme ensures we never collect reachable objects even though
** the program modifies the object graph during collection.
*/

/* Note: Color manipulation functions (makewhite, set2gray, set2black, etc.)
** are now in lgc.h for use by all GC modules. */


/*
** Access to collectable objects in array part of tables
*/
#define gcvalarr(t,i)  \
	((*(t)->getArrayTag(i) & BIT_ISCOLLECTABLE) ? (t)->getArrayVal(i)->gc : NULL)


#define markvalue(g,o) { checkliveness(mainthread(g),o); \
  if (valiswhite(o)) reallymarkobject(g,gcvalue(o)); }

#define markkey(g, n)	{ if (keyiswhite(n)) reallymarkobject(g,n->getKeyGC()); }

#define markobject(g,t)	{ if (iswhite(t)) reallymarkobject(g, obj2gco(t)); }

/*
** mark an object that can be NULL (either because it is really optional,
** or it was stripped as debug info, or inside an uncompleted structure)
*/
#define markobjectN(g,t)	{ if (t) markobject(g,t); }


static void reallymarkobject (global_State *g, GCObject *o);
static void atomic (lua_State *L);
static void entersweep (lua_State *L);


/*
** {======================================================
** Generic functions
** =======================================================
*/


/*
** one after last element in a hash array
*/
inline Node* gnodelast(Table* h) noexcept {
	return gnode(h, cast_sizet(h->nodeSize()));
}

inline Node* gnodelast(const Table* h) noexcept {
	return gnode(h, cast_sizet(h->nodeSize()));
}


/* Wrapper for GCCore::objsize - now in gc_core module */
static l_mem objsize(GCObject* o) {
  return GCCore::objsize(o);
}


/* Wrapper for GCCore::getgclist - now in gc_core module */
static GCObject** getgclist(GCObject* o) {
  return GCCore::getgclist(o);
}


/* Wrapper for GCCore::linkgclist_ - now in gc_core module */
static void linkgclist_(GCObject* o, GCObject** pnext, GCObject** list) {
  GCCore::linkgclist_(o, pnext, list);
}

/*
** Link a collectable object 'o' with a known type into the list 'p'.
** (Must be a macro to access the 'gclist' field in different types.)
*/
#define linkgclist(o,p)	linkgclist_(obj2gco(o), &(o)->gclist, &(p))

/* Specialized version for Table (with encapsulated gclist) */
inline void linkgclistTable(Table *h, GCObject *&p) {
  linkgclist_(obj2gco(h), h->getGclistPtr(), &p);
}

/* Specialized version for lua_State (with encapsulated gclist) */
inline void linkgclistThread(lua_State *th, GCObject *&p) {
  linkgclist_(obj2gco(th), th->getGclistPtr(), &p);
}


/*
** Link a generic collectable object 'o' into the list 'p'.
*/
#define linkobjgclist(o,p) linkgclist_(obj2gco(o), getgclist(o), &(p))



/* Wrapper for GCCore::clearkey - now in gc_core module */
static void clearkey(Node* n) {
  GCCore::clearkey(n);
}


/*
** Barrier that moves collector forward, that is, marks the white object
** 'v' being pointed by the black object 'o'.  In the generational
** mode, 'v' must also become old, if 'o' is old; however, it cannot
** be changed directly to OLD, because it may still point to non-old
** objects. So, it is marked as OLD0. In the next cycle it will become
** OLD1, and in the next it will finally become OLD (regular old). By
** then, any object it points to will also be old.  If called in the
** incremental sweep phase, it clears the black object to white (sweep
** it) to avoid other barrier calls for this same object. (That cannot
** be done is generational mode, as its sweep does not distinguish
** white from dead.)
*/
void luaC_barrier_ (lua_State *L, GCObject *o, GCObject *v) {
  global_State *g = G(L);
  lua_assert(isblack(o) && iswhite(v) && !isdead(g, v) && !isdead(g, o));
  if (g->keepInvariant()) {  /* must keep invariant? */
    reallymarkobject(g, v);  /* restore invariant */
    if (isold(o)) {
      lua_assert(!isold(v));  /* white object could not be old */
      setage(v, GCAge::Old0);  /* restore generational invariant */
    }
  }
  else {  /* sweep phase */
    lua_assert(g->isSweepPhase());
    if (g->getGCKind() != GCKind::GenerationalMinor)  /* incremental mode? */
      makewhite(g, o);  /* mark 'o' as white to avoid other barriers */
  }
}


/*
** barrier that moves collector backward, that is, mark the black object
** pointing to a white object as gray again.
*/
void luaC_barrierback_ (lua_State *L, GCObject *o) {
  global_State *g = G(L);
  lua_assert(isblack(o) && !isdead(g, o));
  lua_assert((g->getGCKind() != GCKind::GenerationalMinor)
          || (isold(o) && getage(o) != GCAge::Touched1));
  if (getage(o) == GCAge::Touched2)  /* already in gray list? */
    set2gray(o);  /* make it gray to become touched1 */
  else  /* link it in 'grayagain' and paint it gray */
    linkobjgclist(o, *g->getGrayAgainPtr());
  if (isold(o))  /* generational mode? */
    setage(o, GCAge::Touched1);  /* touched in current cycle */
}




/*
** create a new collectable object (with given type, size, and offset)
** and link it to 'allgc' list.
*/
GCObject *luaC_newobjdt (lua_State *L, lu_byte tt, size_t sz, size_t offset) {
  global_State *g = G(L);
  char *p = cast_charp(luaM_newobject(L, novariant(tt), sz));
  GCObject *o = cast(GCObject *, p + offset);
  o->setMarked(g->getWhite());
  o->setType(tt);
  o->setNext(g->getAllGC());
  g->setAllGC(o);
  return o;
}


/*
** create a new collectable object with no offset.
*/
GCObject *luaC_newobj (lua_State *L, lu_byte tt, size_t sz) {
  return luaC_newobjdt(L, tt, sz, 0);
}

/* }====================================================== */



/*
** {======================================================
** Mark functions
** =======================================================
*/


/*
** Mark an object.  Userdata with no user values, strings, and closed
** upvalues are visited and turned black here.  Open upvalues are
** already indirectly linked through their respective threads in the
** 'twups' list, so they don't go to the gray list; nevertheless, they
** are kept gray to avoid barriers, as their values will be revisited
** by the thread or by 'remarkupvals'.  Other objects are added to the
** gray list to be visited (and turned black) later.  Both userdata and
** upvalues can call this function recursively, but this recursion goes
** for at most two levels: An upvalue cannot refer to another upvalue
** (only closures can), and a userdata's metatable must be a table.
*/
static void reallymarkobject (global_State *g, GCObject *o) {
  g->setGCMarked(g->getGCMarked() + objsize(o));
  switch (o->getType()) {
    case LUA_VSHRSTR:
    case LUA_VLNGSTR: {
      set2black(o);  /* nothing to visit */
      break;
    }
    case LUA_VUPVAL: {
      UpVal *uv = gco2upv(o);
      if (uv->isOpen())
        set2gray(uv);  /* open upvalues are kept gray */
      else
        set2black(uv);  /* closed upvalues are visited here */
      markvalue(g, uv->getVP());  /* mark its content */
      break;
    }
    case LUA_VUSERDATA: {
      Udata *u = gco2u(o);
      if (u->getNumUserValues() == 0) {  /* no user values? */
        markobjectN(g, u->getMetatable());  /* mark its metatable */
        set2black(u);  /* nothing else to mark */
        break;
      }
      /* else... */
    }  /* FALLTHROUGH */
    case LUA_VLCL: case LUA_VCCL: case LUA_VTABLE:
    case LUA_VTHREAD: case LUA_VPROTO: {
      linkobjgclist(o, *g->getGrayPtr());  /* to be visited later */
      break;
    }
    default: lua_assert(0); break;
  }
}


/*
** mark metamethods for basic types
*/
static void markmt (global_State *g) {
  int i;
  for (i=0; i < LUA_NUMTYPES; i++)
    markobjectN(g, g->getMetatable(i));
}


/*
** mark all objects in list of being-finalized
*/
static void markbeingfnz (global_State *g) {
  GCObject *o;
  for (o = g->getToBeFnz(); o != NULL; o = o->getNext())
    markobject(g, o);
}


/*
** For each non-marked thread, simulates a barrier between each open
** upvalue and its value. (If the thread is collected, the value will be
** assigned to the upvalue, but then it can be too late for the barrier
** to act. The "barrier" does not need to check colors: A non-marked
** thread must be young; upvalues cannot be older than their threads; so
** any visited upvalue must be young too.) Also removes the thread from
** the list, as it was already visited. Removes also threads with no
** upvalues, as they have nothing to be checked. (If the thread gets an
** upvalue later, it will be linked in the list again.)
*/
static void remarkupvals (global_State *g) {
  lua_State *thread;
  lua_State **p = g->getTwupsPtr();
  while ((thread = *p) != NULL) {
    if (!iswhite(thread) && thread->getOpenUpval() != NULL)
      p = thread->getTwupsPtr();  /* keep marked thread with upvalues in the list */
    else {  /* thread is not marked or without upvalues */
      UpVal *uv;
      lua_assert(!isold(thread) || thread->getOpenUpval() == NULL);
      *p = thread->getTwups();  /* remove thread from the list */
      thread->setTwups(thread);  /* mark that it is out of list */
      for (uv = thread->getOpenUpval(); uv != NULL; uv = uv->getOpenNext()) {
        lua_assert(getage(uv) <= getage(thread));
        if (!iswhite(uv)) {  /* upvalue already visited? */
          lua_assert(uv->isOpen() && isgray(uv));
          markvalue(g, uv->getVP());  /* mark its value */
        }
      }
    }
  }
}


static void cleargraylists (global_State *g) {
  *g->getGrayPtr() = *g->getGrayAgainPtr() = NULL;
  *g->getWeakPtr() = *g->getAllWeakPtr() = *g->getEphemeronPtr() = NULL;
}


/*
** mark root set and reset all gray lists, to start a new collection.
** 'GCmarked' is initialized to count the total number of live bytes
** during a cycle.
*/
static void restartcollection (global_State *g) {
  cleargraylists(g);
  g->setGCMarked(0);
  markobject(g, mainthread(g));
  markvalue(g, g->getRegistry());
  markmt(g);
  markbeingfnz(g);  /* mark any finalizing object left from previous cycle */
}

/* }====================================================== */


/*
** {======================================================
** Traverse functions
** =======================================================
*/


/*
** Check whether object 'o' should be kept in the 'grayagain' list for
** post-processing by 'correctgraylist'. (It could put all old objects
** in the list and leave all the work to 'correctgraylist', but it is
** more efficient to avoid adding elements that will be removed.) Only
** TOUCHED1 objects need to be in the list. TOUCHED2 doesn't need to go
** back to a gray list, but then it must become OLD. (That is what
** 'correctgraylist' does when it finds a TOUCHED2 object.)
** This function is a no-op in incremental mode, as objects cannot be
** marked as touched in that mode.
*/
static void genlink (global_State *g, GCObject *o) {
  lua_assert(isblack(o));
  if (getage(o) == GCAge::Touched1) {  /* touched in this cycle? */
    linkobjgclist(o, *g->getGrayAgainPtr());  /* link it back in 'grayagain' */
  }  /* everything else do not need to be linked back */
  else if (getage(o) == GCAge::Touched2)
    setage(o, GCAge::Old);  /* advance age */
}


/*
** Traverse a table with weak values and link it to proper list. During
** propagate phase, keep it in 'grayagain' list, to be revisited in the
** atomic phase. In the atomic phase, if table has any white value,
** put it in 'weak' list, to be cleared; otherwise, call 'genlink'
** to check table age in generational mode.
*/
/*
** Wrapper for traverseweakvalue - delegates to GCWeak module.
** See gc_weak.cpp for implementation.
*/
void traverseweakvalue (global_State *g, Table *h) {
  GCWeak::traverseweakvalue(g, h);
}


/*
** Traverse the array part of a table.
*/
static int traversearray (global_State *g, Table *h) {
  unsigned asize = h->arraySize();
  int marked = 0;  /* true if some object is marked in this traversal */
  unsigned i;
  for (i = 0; i < asize; i++) {
    GCObject *o = gcvalarr(h, i);
    if (o != NULL && iswhite(o)) {
      marked = 1;
      reallymarkobject(g, o);
    }
  }
  return marked;
}


static void traversestrongtable (global_State *g, Table *h) {
  Node *n, *limit = gnodelast(h);
  traversearray(g, h);
  for (n = gnode(h, 0); n < limit; n++) {  /* traverse hash part */
    if (isempty(gval(n)))  /* entry is empty? */
      clearkey(n);  /* clear its key */
    else {
      lua_assert(!n->isKeyNil());
      markkey(g, n);
      markvalue(g, gval(n));
    }
  }
  genlink(g, obj2gco(h));
}


/*
** (result & 1) iff weak values; (result & 2) iff weak keys.
*/
static l_mem traversetable (global_State *g, Table *h) {
  markobjectN(g, h->getMetatable());
  switch (GCWeak::getmode(g, h)) {
    case 0:  /* not weak */
      traversestrongtable(g, h);
      break;
    case 1:  /* weak values */
      traverseweakvalue(g, h);
      break;
    case 2:  /* weak keys */
      GCWeak::traverseephemeron(g, h, 0);
      break;
    case 3:  /* all weak; nothing to traverse */
      if (g->getGCState() == GCState::Propagate)
        linkgclistTable(h, *g->getGrayAgainPtr());  /* must visit again its metatable */
      else
        linkgclistTable(h, *g->getAllWeakPtr());  /* must clear collected entries */
      break;
  }
  return cast(l_mem, 1 + 2*h->nodeSize() + h->arraySize());
}


static l_mem traverseudata (global_State *g, Udata *u) {
  int i;
  markobjectN(g, u->getMetatable());  /* mark its metatable */
  for (i = 0; i < u->getNumUserValues(); i++)
    markvalue(g, &u->getUserValue(i)->uv);
  genlink(g, obj2gco(u));
  return 1 + u->getNumUserValues();
}


/*
** Traverse a prototype. (While a prototype is being build, its
** arrays can be larger than needed; the extra slots are filled with
** NULL, so the use of 'markobjectN')
*/
static l_mem traverseproto (global_State *g, Proto *f) {
  int i;
  markobjectN(g, f->getSource());
  for (i = 0; i < f->getConstantsSize(); i++)  /* mark literals */
    markvalue(g, &f->getConstants()[i]);
  for (i = 0; i < f->getUpvaluesSize(); i++)  /* mark upvalue names */
    markobjectN(g, f->getUpvalues()[i].getName());
  for (i = 0; i < f->getProtosSize(); i++)  /* mark nested protos */
    markobjectN(g, f->getProtos()[i]);
  for (i = 0; i < f->getLocVarsSize(); i++)  /* mark local-variable names */
    markobjectN(g, f->getLocVars()[i].getVarName());
  return 1 + f->getConstantsSize() + f->getUpvaluesSize() + f->getProtosSize() + f->getLocVarsSize();
}


static l_mem traverseCclosure (global_State *g, CClosure *cl) {
  int i;
  for (i = 0; i < cl->getNumUpvalues(); i++)  /* mark its upvalues */
    markvalue(g, cl->getUpvalue(i));
  return 1 + cl->getNumUpvalues();
}

/*
** Traverse a Lua closure, marking its prototype and its upvalues.
** (Both can be NULL while closure is being created.)
*/
static l_mem traverseLclosure (global_State *g, LClosure *cl) {
  int i;
  markobjectN(g, cl->getProto());  /* mark its prototype */
  for (i = 0; i < cl->getNumUpvalues(); i++) {  /* visit its upvalues */
    UpVal *uv = cl->getUpval(i);
    markobjectN(g, uv);  /* mark upvalue */
  }
  return 1 + cl->getNumUpvalues();
}


/*
** Traverse a thread, marking the elements in the stack up to its top
** and cleaning the rest of the stack in the final traversal. That
** ensures that the entire stack have valid (non-dead) objects.
** Threads have no barriers. In gen. mode, old threads must be visited
** at every cycle, because they might point to young objects.  In inc.
** mode, the thread can still be modified before the end of the cycle,
** and therefore it must be visited again in the atomic phase. To ensure
** these visits, threads must return to a gray list if they are not new
** (which can only happen in generational mode) or if the traverse is in
** the propagate phase (which can only happen in incremental mode).
*/
static l_mem traversethread (global_State *g, lua_State *th) {
  UpVal *uv;
  StkId o = th->getStack().p;
  if (isold(th) || g->getGCState() == GCState::Propagate)
    linkgclistThread(th, *g->getGrayAgainPtr());  /* insert into 'grayagain' list */
  if (o == NULL)
    return 0;  /* stack not completely built yet */
  lua_assert(g->getGCState() == GCState::Atomic ||
             th->getOpenUpval() == NULL || th->isInTwups());
  for (; o < th->getTop().p; o++)  /* mark live elements in the stack */
    markvalue(g, s2v(o));
  for (uv = th->getOpenUpval(); uv != NULL; uv = uv->getOpenNext())
    markobject(g, uv);  /* open upvalues cannot be collected */
  if (g->getGCState() == GCState::Atomic) {  /* final traversal? */
    if (!g->getGCEmergency())
      th->shrinkStack();  /* do not change stack in emergency cycle */
    for (o = th->getTop().p; o < th->getStackLast().p + EXTRA_STACK; o++)
      setnilvalue(s2v(o));  /* clear dead stack slice */
    /* 'remarkupvals' may have removed thread from 'twups' list */
    if (!th->isInTwups() && th->getOpenUpval() != NULL) {
      th->setTwups(g->getTwups());  /* link it back to the list */
      g->setTwups(th);
    }
  }
  return 1 + (th->getTop().p - th->getStack().p);
}


/*
** traverse one gray object, turning it to black. Return an estimate
** of the number of slots traversed.
*/
static l_mem propagatemark (global_State *g) {
  GCObject *o = g->getGray();
  nw2black(o);
  g->setGray(*getgclist(o));  /* remove from 'gray' list */
  switch (o->getType()) {
    case LUA_VTABLE: return traversetable(g, gco2t(o));
    case LUA_VUSERDATA: return traverseudata(g, gco2u(o));
    case LUA_VLCL: return traverseLclosure(g, gco2lcl(o));
    case LUA_VCCL: return traverseCclosure(g, gco2ccl(o));
    case LUA_VPROTO: return traverseproto(g, gco2p(o));
    case LUA_VTHREAD: return traversethread(g, gco2th(o));
    default: lua_assert(0); return 0;
  }
}


static void propagateall (global_State *g) {
  while (g->getGray())
    propagatemark(g);
}


/*
** Traverse all ephemeron tables propagating marks from keys to values.
** Repeat until it converges, that is, nothing new is marked. 'dir'
** inverts the direction of the traversals, trying to speed up
** convergence on chains in the same table.
*/
/*
** Wrapper for convergeephemerons - delegates to GCWeak module.
** See gc_weak.cpp for implementation.
*/
static void convergeephemerons (global_State *g) {
  GCWeak::convergeephemerons(g);
}

/* }====================================================== */


/*
** {======================================================
** Sweep Functions
** =======================================================
*/


/*
** Wrapper for clearbykeys - delegates to GCWeak module.
** See gc_weak.cpp for implementation.
*/
static void clearbykeys (global_State *g, GCObject *l) {
  GCWeak::clearbykeys(g, l);
}


/*
** Wrapper for clearbyvalues - delegates to GCWeak module.
** See gc_weak.cpp for implementation.
*/
static void clearbyvalues (global_State *g, GCObject *l, GCObject *f) {
  GCWeak::clearbyvalues(g, l, f);
}


/* Wrapper for GCCore::freeupval - now in gc_core module */
static void freeupval(lua_State* L, UpVal* uv) {
  GCCore::freeupval(L, uv);
}


// Phase 50: Call destructors before freeing memory (proper RAII)
// Made non-static for use by gc_sweeping module (Phase 2)
void freeobj (lua_State *L, GCObject *o) {
  assert_code(l_mem newmem = G(L)->getTotalBytes() - objsize(o));
  switch (o->getType()) {
    case LUA_VPROTO: {
      Proto *p = gco2p(o);
      p->free(L);  /* Phase 25b - frees internal arrays */
      // Proto destructor is trivial, but call it for completeness
      p->~Proto();
      break;
    }
    case LUA_VUPVAL: {
      UpVal *uv = gco2upv(o);
      freeupval(L, uv);  // Note: freeupval calls destructor internally
      break;
    }
    case LUA_VLCL: {
      LClosure *cl = gco2lcl(o);
      cl->~LClosure();  // Call destructor
      luaM_freemem(L, cl, sizeLclosure(cl->getNumUpvalues()));
      break;
    }
    case LUA_VCCL: {
      CClosure *cl = gco2ccl(o);
      cl->~CClosure();  // Call destructor
      luaM_freemem(L, cl, sizeCclosure(cl->getNumUpvalues()));
      break;
    }
    case LUA_VTABLE: {
      Table *t = gco2t(o);
      luaH_free(L, t);  // Note: luaH_free calls destroy() which should handle cleanup
      break;
    }
    case LUA_VTHREAD:
      luaE_freethread(L, gco2th(o));
      break;
    case LUA_VUSERDATA: {
      Udata *u = gco2u(o);
      u->~Udata();  // Call destructor
      luaM_freemem(L, o, sizeudata(u->getNumUserValues(), u->getLen()));
      break;
    }
    case LUA_VSHRSTR: {
      TString *ts = gco2ts(o);
      size_t sz = sizestrshr(cast_uint(ts->getShrlen()));
      ts->remove(L);  /* use method instead of free function */
      // DON'T call destructor for TString - it's empty and might cause issues with variable-size objects
      // ts->~TString();
      luaM_freemem(L, ts, sz);
      break;
    }
    case LUA_VLNGSTR: {
      TString *ts = gco2ts(o);
      if (ts->getShrlen() == LSTRMEM)  /* must free external string? */
        (*ts->getFalloc())(ts->getUserData(), ts->getContentsField(), ts->getLnglen() + 1, 0);
      ts->~TString();  // Call destructor
      luaM_freemem(L, ts, luaS_sizelngstr(ts->getLnglen(), ts->getShrlen()));
      break;
    }
    default: lua_assert(0);
  }
  lua_assert(G(L)->getTotalBytes() == newmem);
}


/*
** sweep at most 'countin' elements from a list of GCObjects erasing dead
** objects, where a dead object is one marked with the old (non current)
** white; change all non-dead objects back to white (and new), preparing
** for next collection cycle. Return where to continue the traversal or
** NULL if list is finished.
*/
/*
** Sweep a linked list of GC objects, freeing dead objects.
**
** PARAMETERS:
** - p: Pointer to the head of the linked list (indirection allows list modification)
** - countin: Maximum number of objects to process (for incremental sweeping)
**
** RETURN:
** - NULL if list is fully swept
** - Pointer to next position to continue sweeping (for incremental work)
**
** TWO-WHITE SCHEME:
** Lua uses two white colors that alternate each GC cycle. During marking,
** new objects are allocated with the "other" white (currentwhite XOR 1).
** During sweeping, we only collect objects with the "old" white (otherwhite).
** This prevents collecting newly allocated objects before they can be marked.
**
** SWEEP PROCESS:
** 1. Check if object is dead (has the old white color)
** 2. If dead: remove from list and free memory
** 3. If alive: reset to current white and mark age as GCAge::New
**
** INCREMENTAL SWEEPING:
** The countin parameter limits work per step. This allows sweeping to be
** interleaved with program execution, preventing long pauses.
*/
/* sweeplist now in GCSweeping module */
/* }====================================================== */


/*
** {======================================================
** Finalization
** =======================================================
*/

/*
** If possible, shrink string table.
*/
/*
** Wrapper for checkSizes - delegates to GCFinalizer module.
** See gc_finalizer.cpp for implementation.
*/
static void checkSizes (lua_State *L, global_State *g) {
  GCFinalizer::checkSizes(L, g);
}


/* udata2finalize, dothecall now in GCFinalizer module */


/*
** Wrapper for GCTM - delegates to GCFinalizer module.
** See gc_finalizer.cpp for full implementation and documentation.
*/
static void GCTM (lua_State *L) {
  GCFinalizer::GCTM(L);
}


/*
** Wrapper for callallpendingfinalizers - delegates to GCFinalizer module.
** See gc_finalizer.cpp for implementation.
*/
static void callallpendingfinalizers (lua_State *L) {
  GCFinalizer::callallpendingfinalizers(L);
}


/* findlast, checkpointer now in GCFinalizer module */


/*
** Wrapper for separatetobefnz - delegates to GCFinalizer module.
** See gc_finalizer.cpp for implementation.
*/
static void separatetobefnz (global_State *g, int all) {
  GCFinalizer::separatetobefnz(g, all);
}


/*
** Wrapper for correctpointers - delegates to GCFinalizer module.
** See gc_finalizer.cpp for implementation.
*/
static void correctpointers (global_State *g, GCObject *o) {
  GCFinalizer::correctpointers(g, o);
}


/*
** if object 'o' has a finalizer, remove it from 'allgc' list (must
** search the list to find it) and link it in 'finobj' list.
*/

/* }====================================================== */


/*
** {======================================================
** Generational Collector
** =======================================================
*/

/*
** Fields 'GCmarked' and 'GCmajorminor' are used to control the pace and
** the mode of the collector. They play several roles, depending on the
** mode of the collector:
** * GCKind::Incremental:
**     GCmarked: number of marked bytes during a cycle.
**     GCmajorminor: not used.
** * GCKind::GenerationalMinor
**     GCmarked: number of bytes that became old since last major collection.
**     GCmajorminor: number of bytes marked in last major collection.
** * GCKind::GenerationalMajor
**     GCmarked: number of bytes that became old since last major collection.
**     GCmajorminor: number of bytes marked in last major collection.
*/


/*
** Set the "time" to wait before starting a new incremental cycle;
** cycle will start when number of bytes in use hits the threshold of
** approximately (marked * pause / 100).
*/
static void setpause (global_State *g) {
  l_mem threshold = applygcparam(g, PAUSE, g->getGCMarked());
  l_mem debt = threshold - g->getTotalBytes();
  if (debt < 0) debt = 0;
  luaE_setdebt(g, debt);
}


/*
** Sweep a list of objects to enter generational mode.  Deletes dead
** objects and turns the non dead to old. All non-dead threads---which
** are now old---must be in a gray list. Everything else is not in a
** gray list. Open upvalues are also kept gray.
*/
/*
** Wrapper for sweep2old - delegates to GCSweeping module.
** See gc_sweeping.cpp for implementation.
*/
static void sweep2old (lua_State *L, GCObject **p) {
  GCSweeping::sweep2old(L, p);
}

/*
** Correct a list of gray objects. Return a pointer to the last element
** left on the list, so that we can link another list to the end of
** this one.
** Because this correction is done after sweeping, young objects might
** be turned white and still be in the list. They are only removed.
** 'TOUCHED1' objects are advanced to 'TOUCHED2' and remain on the list;
** Non-white threads also remain on the list. 'TOUCHED2' objects and
** anything else become regular old, are marked black, and are removed
** from the list.
*/
static GCObject **correctgraylist (GCObject **p) {
  GCObject *curr;
  while ((curr = *p) != NULL) {
    GCObject **next = getgclist(curr);
    if (iswhite(curr))
      goto remove;  /* remove all white objects */
    else if (getage(curr) == GCAge::Touched1) {  /* touched in this cycle? */
      lua_assert(isgray(curr));
      nw2black(curr);  /* make it black, for next barrier */
      setage(curr, GCAge::Touched2);
      goto remain;  /* keep it in the list and go to next element */
    }
    else if (curr->getType() == LUA_VTHREAD) {
      lua_assert(isgray(curr));
      goto remain;  /* keep non-white threads on the list */
    }
    else {  /* everything else is removed */
      lua_assert(isold(curr));  /* young objects should be white here */
      if (getage(curr) == GCAge::Touched2)  /* advance from TOUCHED2... */
        setage(curr, GCAge::Old);  /* ... to OLD */
      nw2black(curr);  /* make object black (to be removed) */
      goto remove;
    }
    remove: *p = *next; continue;
    remain: p = next; continue;
  }
  return p;
}


/*
** Correct all gray lists, coalescing them into 'grayagain'.
*/
static void correctgraylists (global_State *g) {
  GCObject **list = correctgraylist(g->getGrayAgainPtr());
  *list = g->getWeak(); g->setWeak(NULL);
  list = correctgraylist(list);
  *list = g->getAllWeak(); g->setAllWeak(NULL);
  list = correctgraylist(list);
  *list = g->getEphemeron(); g->setEphemeron(NULL);
  correctgraylist(list);
}


/*
** Mark black 'OLD1' objects when starting a new young collection.
** Gray objects are already in some gray list, and so will be visited in
** the atomic step.
*/
static void markold (global_State *g, GCObject *from, GCObject *to) {
  GCObject *p;
  for (p = from; p != to; p = p->getNext()) {
    if (getage(p) == GCAge::Old1) {
      lua_assert(!iswhite(p));
      setage(p, GCAge::Old);  /* now they are old */
      if (isblack(p))
        reallymarkobject(g, p);
    }
  }
}


/*
** Finish a young-generation collection.
*/
static void finishgencycle (lua_State *L, global_State *g) {
  correctgraylists(g);
  checkSizes(L, g);
  g->setGCState(GCState::Propagate);  /* skip restart */
  if (!g->getGCEmergency())
    callallpendingfinalizers(L);
}


/*
** Shifts from a minor collection to major collections. It starts in
** the "sweep all" state to clear all objects, which are mostly black
** in generational mode.
*/
static void minor2inc (lua_State *L, global_State *g, GCKind kind) {
  g->setGCMajorMinor(g->getGCMarked());  /* number of live bytes */
  g->setGCKind(kind);
  g->setReallyOld(NULL); g->setOld1(NULL); g->setSurvival(NULL);
  g->setFinObjROld(NULL); g->setFinObjOld1(NULL); g->setFinObjSur(NULL);
  entersweep(L);  /* continue as an incremental cycle */
  /* set a debt equal to the step size */
  luaE_setdebt(g, applygcparam(g, STEPSIZE, 100));
}


/*
** Decide whether to shift to major mode. It shifts if the accumulated
** number of added old bytes (counted in 'GCmarked') is larger than
** 'minormajor'% of the number of lived bytes after the last major
** collection. (This number is kept in 'GCmajorminor'.)
*/
static int checkminormajor (global_State *g) {
  l_mem limit = applygcparam(g, MINORMAJOR, g->getGCMajorMinor());
  if (limit == 0)
    return 0;  /* special case: 'minormajor' 0 stops major collections */
  return (g->getGCMarked() >= limit);
}

/*
** Does a young collection. First, mark 'OLD1' objects. Then does the
** atomic step. Then, check whether to continue in minor mode. If so,
** sweep all lists and advance pointers. Finally, finish the collection.
*/
static void youngcollection (lua_State *L, global_State *g) {
  l_mem addedold1 = 0;
  l_mem marked = g->getGCMarked();  /* preserve 'g->getGCMarked()' */
  GCObject **psurvival;  /* to point to first non-dead survival object */
  GCObject *dummy;  /* dummy out parameter to 'sweepgen' */
  lua_assert(g->getGCState() == GCState::Propagate);
  if (g->getFirstOld1()) {  /* are there regular OLD1 objects? */
    markold(g, g->getFirstOld1(), g->getReallyOld());  /* mark them */
    g->setFirstOld1(NULL);  /* no more OLD1 objects (for now) */
  }
  markold(g, g->getFinObj(), g->getFinObjROld());
  markold(g, g->getToBeFnz(), NULL);

  atomic(L);  /* will lose 'g->marked' */

  /* sweep nursery and get a pointer to its last live element */
  g->setGCState(GCState::SweepAllGC);
  psurvival = GCSweeping::sweepgen(L, g, g->getAllGCPtr(), g->getSurvival(), g->getFirstOld1Ptr(), &addedold1);
  /* sweep 'survival' */
  GCSweeping::sweepgen(L, g, psurvival, g->getOld1(), g->getFirstOld1Ptr(), &addedold1);
  g->setReallyOld(g->getOld1());
  g->setOld1(*psurvival);  /* 'survival' survivals are old now */
  g->setSurvival(g->getAllGC());  /* all news are survivals */

  /* repeat for 'finobj' lists */
  dummy = NULL;  /* no 'firstold1' optimization for 'finobj' lists */
  psurvival = GCSweeping::sweepgen(L, g, g->getFinObjPtr(), g->getFinObjSur(), &dummy, &addedold1);
  /* sweep 'survival' */
  GCSweeping::sweepgen(L, g, psurvival, g->getFinObjOld1(), &dummy, &addedold1);
  g->setFinObjROld(g->getFinObjOld1());
  g->setFinObjOld1(*psurvival);  /* 'survival' survivals are old now */
  g->setFinObjSur(g->getFinObj());  /* all news are survivals */

  GCSweeping::sweepgen(L, g, g->getToBeFnzPtr(), NULL, &dummy, &addedold1);

  /* keep total number of added old1 bytes */
  g->setGCMarked(marked + addedold1);

  /* decide whether to shift to major mode */
  if (checkminormajor(g)) {
    minor2inc(L, g, GCKind::GenerationalMajor);  /* go to major mode */
    g->setGCMarked(0);  /* avoid pause in first major cycle (see 'setpause') */
  }
  else
    finishgencycle(L, g);  /* still in minor mode; finish it */
}


/*
** Clears all gray lists, sweeps objects, and prepare sublists to enter
** generational mode. The sweeps remove dead objects and turn all
** surviving objects to old. Threads go back to 'grayagain'; everything
** else is turned black (not in any gray list).
*/
static void atomic2gen (lua_State *L, global_State *g) {
  cleargraylists(g);
  /* sweep all elements making them old */
  g->setGCState(GCState::SweepAllGC);
  sweep2old(L, g->getAllGCPtr());
  /* everything alive now is old */
  GCObject *allgc = g->getAllGC();
  g->setReallyOld(allgc); g->setOld1(allgc); g->setSurvival(allgc);
  g->setFirstOld1(NULL);  /* there are no OLD1 objects anywhere */

  /* repeat for 'finobj' lists */
  sweep2old(L, g->getFinObjPtr());
  GCObject *finobj = g->getFinObj();
  g->setFinObjROld(finobj); g->setFinObjOld1(finobj); g->setFinObjSur(finobj);

  sweep2old(L, g->getToBeFnzPtr());

  g->setGCKind(GCKind::GenerationalMinor);
  g->setGCMajorMinor(g->getGCMarked());  /* "base" for number of bytes */
  g->setGCMarked(0);  /* to count the number of added old1 bytes */
  finishgencycle(L, g);
}


/*
** Set debt for the next minor collection, which will happen when
** total number of bytes grows 'genminormul'% in relation to
** the base, GCmajorminor, which is the number of bytes being used
** after the last major collection.
*/
static void setminordebt (global_State *g) {
  luaE_setdebt(g, applygcparam(g, MINORMUL, g->getGCMajorMinor()));
}


/*
** Enter generational mode. Must go until the end of an atomic cycle
** to ensure that all objects are correctly marked and weak tables
** are cleared. Then, turn all objects into old and finishes the
** collection.
*/
static void entergen (lua_State *L, global_State *g) {
  luaC_runtilstate(L, GCState::Pause, 1);  /* prepare to start a new cycle */
  luaC_runtilstate(L, GCState::Propagate, 1);  /* start new cycle */
  atomic(L);  /* propagates all and then do the atomic stuff */
  atomic2gen(L, g);
  setminordebt(g);  /* set debt assuming next cycle will be minor */
}


/*
** Change collector mode to 'newmode'.
*/
void luaC_changemode (lua_State *L, GCKind newmode) {
  global_State *g = G(L);
  if (g->getGCKind() == GCKind::GenerationalMajor)  /* doing major collections? */
    g->setGCKind(GCKind::Incremental);  /* already incremental but in name */
  if (newmode != g->getGCKind()) {  /* does it need to change? */
    if (newmode == GCKind::Incremental)  /* entering incremental mode? */
      minor2inc(L, g, GCKind::Incremental);  /* entering incremental mode */
    else {
      lua_assert(newmode == GCKind::GenerationalMinor);
      entergen(L, g);
    }
  }
}


/*
** Does a full collection in generational mode.
*/
static void fullgen (lua_State *L, global_State *g) {
  minor2inc(L, g, GCKind::Incremental);
  entergen(L, g);
}


/*
** After an atomic incremental step from a major collection,
** check whether collector could return to minor collections.
** It checks whether the number of bytes 'tobecollected'
** is greater than 'majorminor'% of the number of bytes added
** since the last collection ('addedbytes').
*/
static int checkmajorminor (lua_State *L, global_State *g) {
  if (g->getGCKind() == GCKind::GenerationalMajor) {  /* generational mode? */
    l_mem numbytes = g->getTotalBytes();
    l_mem addedbytes = numbytes - g->getGCMajorMinor();
    l_mem limit = applygcparam(g, MAJORMINOR, addedbytes);
    l_mem tobecollected = numbytes - g->getGCMarked();
    if (tobecollected > limit) {
      atomic2gen(L, g);  /* return to generational mode */
      setminordebt(g);
      return 1;  /* exit incremental collection */
    }
  }
  g->setGCMajorMinor(g->getGCMarked());  /* prepare for next collection */
  return 0;  /* stay doing incremental collections */
}

/* }====================================================== */


/*
** {======================================================
** GC control
** =======================================================
*/


/*
** Wrapper for entersweep - delegates to GCSweeping module.
** See gc_sweeping.cpp for implementation.
*/
static void entersweep (lua_State *L) {
  GCSweeping::entersweep(L);
}


/*
** Wrapper for deletelist - delegates to GCSweeping module.
** See gc_sweeping.cpp for implementation.
*/
static void deletelist (lua_State *L, GCObject *p, GCObject *limit) {
  GCSweeping::deletelist(L, p, limit);
}


/*
** Call all finalizers of the objects in the given Lua state, and
** then free all objects, except for the main thread.
*/
void luaC_freeallobjects (lua_State *L) {
  global_State *g = G(L);
  g->setGCStp(GCSTPCLS);  /* no extra finalizers after here */
  luaC_changemode(L, GCKind::Incremental);
  separatetobefnz(g, 1);  /* separate all objects with finalizers */
  lua_assert(g->getFinObj() == NULL);
  callallpendingfinalizers(L);
  deletelist(L, g->getAllGC(), obj2gco(mainthread(g)));
  lua_assert(g->getFinObj() == NULL);  /* no new finalizers */
  deletelist(L, g->getFixedGC(), NULL);  /* collect fixed objects */
  lua_assert(g->getStringTable()->getNumElements() == 0);
}


static void atomic (lua_State *L) {
  global_State *g = G(L);
  GCObject *origweak, *origall;
  GCObject *grayagain = g->getGrayAgain();  /* save original list */
  g->setGrayAgain(NULL);
  lua_assert(g->getEphemeron() == NULL && g->getWeak() == NULL);
  lua_assert(!iswhite(mainthread(g)));
  g->setGCState(GCState::Atomic);
  markobject(g, L);  /* mark running thread */
  /* registry and global metatables may be changed by API */
  markvalue(g, g->getRegistry());
  markmt(g);  /* mark global metatables */
  propagateall(g);  /* empties 'gray' list */
  /* remark occasional upvalues of (maybe) dead threads */
  remarkupvals(g);
  propagateall(g);  /* propagate changes */
  g->setGray(grayagain);
  propagateall(g);  /* traverse 'grayagain' list */
  convergeephemerons(g);
  /* at this point, all strongly accessible objects are marked. */
  /* Clear values from weak tables, before checking finalizers */
  clearbyvalues(g, g->getWeak(), NULL);
  clearbyvalues(g, g->getAllWeak(), NULL);
  origweak = g->getWeak(); origall = g->getAllWeak();
  separatetobefnz(g, 0);  /* separate objects to be finalized */
  markbeingfnz(g);  /* mark objects that will be finalized */
  propagateall(g);  /* remark, to propagate 'resurrection' */
  convergeephemerons(g);
  /* at this point, all resurrected objects are marked. */
  /* remove dead objects from weak tables */
  clearbykeys(g, g->getEphemeron());  /* clear keys from all ephemeron */
  clearbykeys(g, g->getAllWeak());  /* clear keys from all 'allweak' */
  /* clear values from resurrected weak tables */
  clearbyvalues(g, g->getWeak(), origweak);
  clearbyvalues(g, g->getAllWeak(), origall);
  luaS_clearcache(g);
  g->setCurrentWhite(cast_byte(otherwhite(g)));  /* flip current white */
  lua_assert(g->getGray() == NULL);
}


/*
** Wrapper for sweepstep - delegates to GCSweeping module.
** See gc_sweeping.cpp for implementation.
*/
static void sweepstep (lua_State *L, global_State *g,
                       GCState nextstate, GCObject **nextlist, int fast) {
  GCSweeping::sweepstep(L, g, nextstate, nextlist, fast);
}


/*
** Performs one incremental "step" in an incremental garbage collection.
** For indivisible work, a step goes to the next state. When marking
** (propagating), a step traverses one object. When sweeping, a step
** sweeps GCSWEEPMAX objects, to avoid a big overhead for sweeping
** objects one by one. (Sweeping is inexpensive, no matter the
** object.) When 'fast' is true, 'singlestep' tries to finish a state
** "as fast as possible". In particular, it skips the propagation
** phase and leaves all objects to be traversed by the atomic phase:
** That avoids traversing twice some objects, such as threads and
** weak tables.
*/

#define step2pause	-3  /* finished collection; entered pause state */
#define atomicstep	-2  /* atomic step */
#define step2minor	-1  /* moved to minor collections */


static l_mem singlestep (lua_State *L, int fast) {
  global_State *g = G(L);
  l_mem stepresult;
  lua_assert(!g->getGCStopEm());  /* collector is not reentrant */
  g->setGCStopEm(1);  /* no emergency collections while collecting */
  switch (g->getGCState()) {
    case GCState::Pause: {
      restartcollection(g);
      g->setGCState(GCState::Propagate);
      stepresult = 1;
      break;
    }
    case GCState::Propagate: {
      if (fast || g->getGray() == NULL) {
        g->setGCState(GCState::EnterAtomic);  /* finish propagate phase */
        stepresult = 1;
      }
      else
        stepresult = propagatemark(g);  /* traverse one gray object */
      break;
    }
    case GCState::EnterAtomic: {
      atomic(L);
      if (checkmajorminor(L, g))
        stepresult = step2minor;
      else {
        entersweep(L);
        stepresult = atomicstep;
      }
      break;
    }
    case GCState::SweepAllGC: {  /* sweep "regular" objects */
      sweepstep(L, g, GCState::SweepFinObj, g->getFinObjPtr(), fast);
      stepresult = GCSWEEPMAX;
      break;
    }
    case GCState::SweepFinObj: {  /* sweep objects with finalizers */
      sweepstep(L, g, GCState::SweepToBeFnz, g->getToBeFnzPtr(), fast);
      stepresult = GCSWEEPMAX;
      break;
    }
    case GCState::SweepToBeFnz: {  /* sweep objects to be finalized */
      sweepstep(L, g, GCState::SweepEnd, NULL, fast);
      stepresult = GCSWEEPMAX;
      break;
    }
    case GCState::SweepEnd: {  /* finish sweeps */
      checkSizes(L, g);
      g->setGCState(GCState::CallFin);
      stepresult = GCSWEEPMAX;
      break;
    }
    case GCState::CallFin: {  /* call finalizers */
      if (g->getToBeFnz() && !g->getGCEmergency()) {
        g->setGCStopEm(0);  /* ok collections during finalizers */
        GCTM(L);  /* call one finalizer */
        stepresult = CWUFIN;
      }
      else {  /* emergency mode or no more finalizers */
        g->setGCState(GCState::Pause);  /* finish collection */
        stepresult = step2pause;
      }
      break;
    }
    default: lua_assert(0); return 0;
  }
  g->setGCStopEm(0);
  return stepresult;
}


/*
** Advances the garbage collector until it reaches the given state.
** (The option 'fast' is only for testing; in normal code, 'fast'
** here is always true.)
*/
void luaC_runtilstate (lua_State *L, GCState state, int fast) {
  global_State *g = G(L);
  lua_assert(g->getGCKind() == GCKind::Incremental);
  while (state != g->getGCState())
    singlestep(L, fast);
}



/*
** Performs a basic incremental step. The step size is
** converted from bytes to "units of work"; then the function loops
** running single steps until adding that many units of work or
** finishing a cycle (pause state). Finally, it sets the debt that
** controls when next step will be performed.
*/
static void incstep (lua_State *L, global_State *g) {
  l_mem stepsize = applygcparam(g, STEPSIZE, 100);
  l_mem work2do = applygcparam(g, STEPMUL, stepsize / cast_int(sizeof(void*)));
  l_mem stres;
  int fast = (work2do == 0);  /* special case: do a full collection */
  do {  /* repeat until enough work */
    stres = singlestep(L, fast);  /* perform one single step */
    if (stres == step2minor)  /* returned to minor collections? */
      return;  /* nothing else to be done here */
    else if (stres == step2pause || (stres == atomicstep && !fast))
      break;  /* end of cycle or atomic */
    else
      work2do -= stres;
  } while (fast || work2do > 0);
  if (g->getGCState() == GCState::Pause)
    setpause(g);  /* pause until next cycle */
  else
    luaE_setdebt(g, stepsize);
}


#if !defined(luai_tracegc)
#define luai_tracegc(L,f)		((void)0)
#endif

/*
** Performs a basic GC step if collector is running. (If collector was
** stopped by the user, set a reasonable debt to avoid it being called
** at every single check.)
*/
void luaC_step (lua_State *L) {
  global_State *g = G(L);
  lua_assert(!g->getGCEmergency());
  if (!g->isGCRunning()) {  /* not running? */
    if (g->getGCStp() & GCSTPUSR)  /* stopped by the user? */
      luaE_setdebt(g, 20000);
  }
  else {
    luai_tracegc(L, 1);  /* for internal debugging */
    switch (g->getGCKind()) {
      case GCKind::Incremental: case GCKind::GenerationalMajor:
        incstep(L, g);
        break;
      case GCKind::GenerationalMinor:
        youngcollection(L, g);
        setminordebt(g);
        break;
    }
    luai_tracegc(L, 0);  /* for internal debugging */
  }
}


/*
** Perform a full collection in incremental mode.
** Before running the collection, check 'keepinvariant'; if it is true,
** there may be some objects marked as black, so the collector has
** to sweep all objects to turn them back to white (as white has not
** changed, nothing will be collected).
*/
static void fullinc (lua_State *L, global_State *g) {
  if (g->keepInvariant())  /* black objects? */
    entersweep(L); /* sweep everything to turn them back to white */
  /* finish any pending sweep phase to start a new cycle */
  luaC_runtilstate(L, GCState::Pause, 1);
  luaC_runtilstate(L, GCState::CallFin, 1);  /* run up to finalizers */
  luaC_runtilstate(L, GCState::Pause, 1);  /* finish collection */
  setpause(g);
}


/*
** Performs a full GC cycle; if 'isemergency', set a flag to avoid
** some operations which could change the interpreter state in some
** unexpected ways (running finalizers and shrinking some structures).
*/
void luaC_fullgc (lua_State *L, int isemergency) {
  global_State *g = G(L);
  lua_assert(!g->getGCEmergency());
  g->setGCEmergency(cast_byte(isemergency));  /* set flag */
  switch (g->getGCKind()) {
    case GCKind::GenerationalMinor: fullgen(L, g); break;
    case GCKind::Incremental: fullinc(L, g); break;
    case GCKind::GenerationalMajor:
      g->setGCKind(GCKind::Incremental);
      fullinc(L, g);
      g->setGCKind(GCKind::GenerationalMajor);
      break;
  }
  g->setGCEmergency(0);
}

/* }====================================================== */


/*
** GCObject method implementations
*/
void GCObject::fix(lua_State* L) {
  global_State *g = G(L);
  lua_assert(g->getAllGC() == this);  /* object must be 1st in 'allgc' list! */
  set2gray(this);  /* they will be gray forever */
  setage(this, GCAge::Old);  /* and old forever */
  g->setAllGC(getNext());  /* remove object from 'allgc' list */
  setNext(g->getFixedGC());  /* link it to 'fixedgc' list */
  g->setFixedGC(this);
}

void GCObject::checkFinalizer(lua_State* L, Table* mt) {
  global_State *g = G(L);
  if (tofinalize(this) ||                 /* obj. is already marked... */
      gfasttm(g, mt, TM_GC) == NULL ||    /* or has no finalizer... */
      (g->getGCStp() & GCSTPCLS))                   /* or closing state? */
    return;  /* nothing to be done */
  else {  /* move 'this' to 'finobj' list */
    GCObject **p;
    if (g->isSweepPhase()) {
      makewhite(g, this);  /* "sweep" object 'this' */
      if (g->getSweepGC() == &this->next)  /* should not remove 'sweepgc' object */
        g->setSweepGC(GCSweeping::sweeptolive(L, g->getSweepGC()));  /* change 'sweepgc' */
    }
    else
      correctpointers(g, this);
    /* search for pointer pointing to 'this' */
    for (p = g->getAllGCPtr(); *p != this; p = (*p)->getNextPtr()) { /* empty */ }
    *p = getNext();  /* remove 'this' from 'allgc' list */
    setNext(g->getFinObj());  /* link it in 'finobj' list */
    g->setFinObj(this);
    setMarkedBit(FINALIZEDBIT);  /* mark it as such */
  }
}


