/*
** $Id: gc_marking.cpp $
** Garbage Collector - Marking Module
** See Copyright Notice in lua.h
*/

#define lgc_c
#define LUA_CORE

#include "lprefix.h"

#include <cstring>

#include "gc_marking.h"
#include "../lgc.h"
#include "../../core/ldo.h"
#include "../../objects/lfunc.h"
#include "../../objects/lstring.h"
#include "../../objects/ltable.h"
#include "../../core/ltm.h"

/*
** GC Marking Module Implementation
**
** This module contains all the mark-phase logic for Lua's tri-color
** incremental garbage collector. The marking phase identifies reachable
** objects by traversing the object graph from roots.
**
** ORGANIZATION:
** - Helper functions (objsize, gnodelast, getgclist, linkgclist, genlink)
** - Type-specific traversal functions (one per GC-able type)
** - Core marking functions (reallymarkobject, propagatemark, propagateall)
** - Utility functions (markmt, markbeingfnz, remarkupvals, cleargraylists)
*/


/*
** =======================================================
** Helper Functions
** =======================================================
*/

/* Mask with all color bits */
#define maskcolors (bitmask(BLACKBIT) | WHITEBITS)

/*
** Color manipulation functions
*/

/* Make an object white (candidate for collection) */
static inline void makewhite(global_State* g, GCObject* x) noexcept {
    x->setMarked(cast_byte((x->getMarked() & ~maskcolors) | g->getWhite()));
}

/* Make an object gray (in work queue) */
static inline void set2gray(GCObject* x) noexcept {
    x->clearMarkedBits(maskcolors);
}

/* Make an object black (fully processed) */
static inline void set2black(GCObject* x) noexcept {
    x->setMarked(cast_byte((x->getMarked() & ~WHITEBITS) | bitmask(BLACKBIT)));
}

/*
** Clear key for empty table entry
** Allows collection of the key while keeping entry in table
*/
static void clearkey(Node* n) {
    lua_assert(isempty(gval(n)));
    if (n->isKeyCollectable())
        n->setKeyDead();
}

/*
** Get last node in hash array (one past the end)
*/
static inline Node* gnodelast(Table* h) noexcept {
    return gnode(h, cast_sizet(h->nodeSize()));
}

static inline Node* gnodelast(const Table* h) noexcept {
    return gnode(h, cast_sizet(h->nodeSize()));
}

/*
** Calculate size of a GC object (used for GC accounting)
*/
static l_mem objsize(GCObject* o) {
    lu_mem res;
    switch (o->getType()) {
        case LUA_VTABLE: {
            res = luaH_size(gco2t(o));
            break;
        }
        case LUA_VLCL: {
            LClosure* cl = gco2lcl(o);
            res = sizeLclosure(cl->getNumUpvalues());
            break;
        }
        case LUA_VCCL: {
            CClosure* cl = gco2ccl(o);
            res = sizeCclosure(cl->getNumUpvalues());
            break;
        }
        case LUA_VUSERDATA: {
            Udata* u = gco2u(o);
            res = sizeudata(u->getNumUserValues(), u->getLen());
            break;
        }
        case LUA_VPROTO: {
            res = gco2p(o)->memorySize();
            break;
        }
        case LUA_VTHREAD: {
            res = luaE_threadsize(gco2th(o));
            break;
        }
        case LUA_VSHRSTR: {
            TString* ts = gco2ts(o);
            res = sizestrshr(cast_uint(ts->getShrlen()));
            break;
        }
        case LUA_VLNGSTR: {
            TString* ts = gco2ts(o);
            res = luaS_sizelngstr(ts->getLnglen(), ts->getShrlen());
            break;
        }
        case LUA_VUPVAL: {
            res = sizeof(UpVal);
            break;
        }
        default:
            res = 0;
            lua_assert(0);
    }
    return cast(l_mem, res);
}

/*
** Get pointer to gclist field for different object types
*/
static GCObject** getgclist(GCObject* o) {
    switch (o->getType()) {
        case LUA_VTABLE:
            return gco2t(o)->getGclistPtr();
        case LUA_VLCL:
            return gco2lcl(o)->getGclistPtr();
        case LUA_VCCL:
            return gco2ccl(o)->getGclistPtr();
        case LUA_VTHREAD:
            return gco2th(o)->getGclistPtr();
        case LUA_VPROTO:
            return gco2p(o)->getGclistPtr();
        case LUA_VUSERDATA: {
            Udata* u = gco2u(o);
            lua_assert(u->getNumUserValues() > 0);
            return u->getGclistPtr();
        }
        default:
            lua_assert(0);
            return 0;
    }
}

/*
** Link object into a gray list and mark it gray
*/
static void linkgclist_(GCObject* o, GCObject** pnext, GCObject** list) {
    lua_assert(!isgray(o));  /* cannot be in a gray list */
    *pnext = *list;
    *list = o;
    set2gray(o);  /* now it is */
}

/* Link a generic object using its gclist pointer */
#define linkobjgclist(o, p) linkgclist_(obj2gco(o), getgclist(o), &(p))

/* Specialized versions for encapsulated types */
static inline void linkgclistTable(Table* h, GCObject*& p) {
    linkgclist_(obj2gco(h), h->getGclistPtr(), &p);
}

static inline void linkgclistThread(lua_State* th, GCObject*& p) {
    linkgclist_(obj2gco(th), th->getGclistPtr(), &p);
}

/*
** Link object for generational collection
** In generational mode, some objects go back to a gray list if they're
** old or if we're in the propagation phase.
*/
static void genlink(global_State* g, GCObject* o) {
    lua_assert(isblack(o));
    if (g->getGCKind() == GCKind::GenerationalMinor) {  /* generational mode? */
        if (isold(o))
            return;  /* old objects don't need to be linked */
        /* else link in appropriate list for next minor collection */
        linkobjgclist(o, *g->getSurvivalPtr());
        setage(o, GCAge::Survival);  /* survived this cycle */
    }
    /* incremental mode: keep object in black state */
}

/*
** Access to collectable value in TValue, or NULL if non-collectable
*/
static inline GCObject* gcvalueN(const TValue* o) noexcept {
    return iscollectable(o) ? gcvalue(o) : NULL;
}

/*
** Access to collectable objects in table array part
*/
#define gcvalarr(t, i)  \
    ((*(t)->getArrayTag(i) & BIT_ISCOLLECTABLE) ? (t)->getArrayVal(i)->gc : NULL)


/*
** =======================================================
** Type-Specific Traversal Functions
** =======================================================
*/

/* Functions getmode, traverseweakvalue, traverseephemeron are in lgc.cpp
   and will be moved to gc_weak module in Phase 4 */

/*
** Traverse array part of a table
** Returns true if any object was marked
*/
static inline int traversearray(global_State* g, Table* h) {
    unsigned asize = h->arraySize();
    int marked = 0;  /* true if some object is marked in this traversal */
    unsigned i;
    for (i = 0; i < asize; i++) {
        GCObject* o = gcvalarr(h, i);
        if (o != NULL && iswhite(o)) {
            marked = 1;
            GCMarking::reallymarkobject(g, o);
        }
    }
    return marked;
}

/*
** Traverse a strong (non-weak) table
*/
static inline void traversestrongtable(global_State* g, Table* h) {
    Node* n;
    Node* limit = gnodelast(h);
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
** Traverse a table (delegates to weak or strong traversal)
** Returns approximate cost in work units
*/
l_mem GCMarking::traversetable(global_State* g, Table* h) {
    markobjectN(g, h->getMetatable());
    switch (getmode(g, h)) {
        case 0:  /* not weak */
            traversestrongtable(g, h);
            break;
        case 1:  /* weak values */
            traverseweakvalue(g, h);
            break;
        case 2:  /* weak keys (ephemeron) */
            traverseephemeron(g, h, 0);
            break;
        case 3:  /* all weak; nothing to traverse */
            if (g->getGCState() == GCState::Propagate)
                linkgclistTable(h, *g->getGrayAgainPtr());
            else
                linkgclistTable(h, *g->getAllWeakPtr());
            break;
    }
    return cast(l_mem, 1 + 2 * h->nodeSize() + h->arraySize());
}

/*
** Traverse a userdata object
*/
l_mem GCMarking::traverseudata(global_State* g, Udata* u) {
    int i;
    markobjectN(g, u->getMetatable());
    for (i = 0; i < u->getNumUserValues(); i++)
        markvalue(g, &u->getUserValue(i)->uv);
    genlink(g, obj2gco(u));
    return 1 + u->getNumUserValues();
}

/*
** Traverse a prototype (function template)
*/
l_mem GCMarking::traverseproto(global_State* g, Proto* f) {
    int i;
    markobjectN(g, f->getSource());
    for (i = 0; i < f->getConstantsSize(); i++)
        markvalue(g, &f->getConstants()[i]);
    for (i = 0; i < f->getUpvaluesSize(); i++)
        markobjectN(g, f->getUpvalues()[i].getName());
    for (i = 0; i < f->getProtosSize(); i++)
        markobjectN(g, f->getProtos()[i]);
    for (i = 0; i < f->getLocVarsSize(); i++)
        markobjectN(g, f->getLocVars()[i].getVarName());
    return 1 + f->getConstantsSize() + f->getUpvaluesSize() +
           f->getProtosSize() + f->getLocVarsSize();
}

/*
** Traverse a C closure
*/
l_mem GCMarking::traverseCclosure(global_State* g, CClosure* cl) {
    int i;
    for (i = 0; i < cl->getNumUpvalues(); i++)
        markvalue(g, cl->getUpvalue(i));
    return 1 + cl->getNumUpvalues();
}

/*
** Traverse a Lua closure
*/
l_mem GCMarking::traverseLclosure(global_State* g, LClosure* cl) {
    int i;
    markobjectN(g, cl->getProto());
    for (i = 0; i < cl->getNumUpvalues(); i++) {
        UpVal* uv = cl->getUpval(i);
        markobjectN(g, uv);
    }
    return 1 + cl->getNumUpvalues();
}

/*
** Traverse a thread
*/
l_mem GCMarking::traversethread(global_State* g, lua_State* th) {
    UpVal* uv;
    StkId o = th->getStack().p;
    if (isold(th) || g->getGCState() == GCState::Propagate)
        linkgclistThread(th, *g->getGrayAgainPtr());
    if (o == NULL)
        return 0;  /* stack not completely built yet */
    lua_assert(g->getGCState() == GCState::Atomic ||
               th->getOpenUpval() == NULL || th->isInTwups());
    for (; o < th->getTop().p; o++)
        markvalue(g, s2v(o));
    for (uv = th->getOpenUpval(); uv != NULL; uv = uv->getOpenNext())
        markobject(g, uv);
    if (g->getGCState() == GCState::Atomic) {
        if (!g->getGCEmergency())
            th->shrinkStack();
        for (o = th->getTop().p; o < th->getStackLast().p + EXTRA_STACK; o++)
            setnilvalue(s2v(o));
        if (!th->isInTwups() && th->getOpenUpval() != NULL) {
            th->setTwups(g->getTwups());
            g->setTwups(th);
        }
    }
    return 1 + (th->getTop().p - th->getStack().p);
}


/*
** =======================================================
** Core Marking Functions
** =======================================================
*/

/*
** Mark an object as reachable
** This is the entry point for marking - called when we discover a white object
*/
void GCMarking::reallymarkobject(global_State* g, GCObject* o) {
    g->setGCMarked(g->getGCMarked() + objsize(o));
    switch (o->getType()) {
        case LUA_VSHRSTR:
        case LUA_VLNGSTR: {
            set2black(o);  /* strings have no children */
            break;
        }
        case LUA_VUPVAL: {
            UpVal* uv = gco2upv(o);
            if (uv->isOpen())
                set2gray(uv);  /* open upvalues kept gray */
            else
                set2black(uv);  /* closed upvalues visited here */
            markvalue(g, uv->getVP());
            break;
        }
        case LUA_VUSERDATA: {
            Udata* u = gco2u(o);
            if (u->getNumUserValues() == 0) {
                markobjectN(g, u->getMetatable());
                set2black(u);
                break;
            }
            /* else fall through to add to gray list */
        } /* FALLTHROUGH */
        case LUA_VLCL:
        case LUA_VCCL:
        case LUA_VTABLE:
        case LUA_VTHREAD:
        case LUA_VPROTO: {
            linkobjgclist(o, *g->getGrayPtr());  /* to be visited later */
            break;
        }
        default:
            lua_assert(0);
            break;
    }
}

/*
** Process one gray object - traverse its children and mark it black
** Returns the traversal cost (work units)
*/
l_mem GCMarking::propagatemark(global_State* g) {
    GCObject* o = g->getGray();
    nw2black(o);
    g->setGray(*getgclist(o));  /* remove from 'gray' list */
    switch (o->getType()) {
        case LUA_VTABLE:
            return traversetable(g, gco2t(o));
        case LUA_VUSERDATA:
            return traverseudata(g, gco2u(o));
        case LUA_VLCL:
            return traverseLclosure(g, gco2lcl(o));
        case LUA_VCCL:
            return traverseCclosure(g, gco2ccl(o));
        case LUA_VPROTO:
            return traverseproto(g, gco2p(o));
        case LUA_VTHREAD:
            return traversethread(g, gco2th(o));
        default:
            lua_assert(0);
            return 0;
    }
}

/*
** Process all gray objects (used in atomic phase)
*/
void GCMarking::propagateall(global_State* g) {
    while (g->getGray())
        propagatemark(g);
}


/*
** =======================================================
** Utility Marking Functions
** =======================================================
*/

/*
** Mark metamethods for basic types
*/
void GCMarking::markmt(global_State* g) {
    int i;
    for (i = 0; i < LUA_NUMTYPES; i++)
        markobjectN(g, g->getMetatable(i));
}

/*
** Mark all objects in tobefnz list (being finalized)
*/
void GCMarking::markbeingfnz(global_State* g) {
    GCObject* o;
    for (o = g->getToBeFnz(); o != NULL; o = o->getNext())
        markobject(g, o);
}

/*
** Remark upvalues for unmarked threads
** Simulates a barrier between each open upvalue and its value
*/
void GCMarking::remarkupvals(global_State* g) {
    lua_State* thread;
    lua_State** p = g->getTwupsPtr();
    while ((thread = *p) != NULL) {
        if (!iswhite(thread) && thread->getOpenUpval() != NULL)
            p = thread->getTwupsPtr();
        else {
            UpVal* uv;
            lua_assert(!isold(thread) || thread->getOpenUpval() == NULL);
            *p = thread->getTwups();
            thread->setTwups(thread);  /* mark out of list */
            for (uv = thread->getOpenUpval(); uv != NULL; uv = uv->getOpenNext()) {
                lua_assert(getage(uv) <= getage(thread));
                if (!iswhite(uv)) {
                    lua_assert(uv->isOpen() && isgray(uv));
                    markvalue(g, uv->getVP());
                }
            }
        }
    }
}

/*
** Clear all gray lists (called when entering sweep phase)
*/
void GCMarking::cleargraylists(global_State* g) {
    *g->getGrayPtr() = *g->getGrayAgainPtr() = NULL;
    *g->getWeakPtr() = *g->getAllWeakPtr() = *g->getEphemeronPtr() = NULL;
}
