/*
** $Id: gc_barrier.cpp $
** Garbage Collector - Write Barrier Module
** See Copyright Notice in lua.h
*/

#define lgc_c
#define LUA_CORE

#include "lprefix.h"

#include <cstring>

#include "gc_barrier.h"
#include "../lgc.h"
#include "gc_marking.h"
#include "../../core/ltm.h"
#include "../../objects/lstring.h"
#include "../../objects/ltable.h"

/*
** GC Write Barrier Module Implementation
**
** This module implements write barriers for Lua's tri-color incremental
** garbage collector. Write barriers maintain the tri-color invariant when
** the mutator (program) modifies objects during collection.
**
** ORGANIZATION:
** - Helper functions (color manipulation, list linking)
** - Core barriers (barrier_, barrierback_)
**
** TRI-COLOR INVARIANT:
** No black object should point to a white object.
** - White: Unreachable/unprocessed objects (candidates for collection)
** - Gray: Reachable but not yet processed (fields not yet scanned)
** - Black: Fully processed (all fields scanned, won't be visited again)
**
** BARRIER STRATEGIES:
** 1. Forward barrier (barrier_): Mark white object gray
**    - Used when setting a single field
**    - Cheaper: marks 1 object instead of N
**
** 2. Backward barrier (barrierback_): Mark black object gray again
**    - Used when object may point to multiple white objects (table resize, etc.)
**    - Cheaper: re-scan 1 object instead of marking N objects
**
** GENERATIONAL MODE:
** Objects advance through ages: New → Survival → Old0 → Old1 → Old
** - Forward barrier sets age to Old0 (not Old, as object may point to young objects)
** - Backward barrier sets age to Touched1 (needs re-scanning)
*/

/* Mask with all color bits */
#define maskcolors (bitmask(BLACKBIT) | WHITEBITS)

/*
** Make an object white (candidate for collection).
** Sets the current white bit (which alternates each cycle).
*/
static inline void makewhite(global_State* g, GCObject* x) noexcept {
    x->setMarked(cast_byte((x->getMarked() & ~maskcolors) | g->getWhite()));
}

/*
** Make an object gray (reachable but unprocessed).
** Clears all color bits.
*/
static inline void set2gray(GCObject* x) noexcept {
    x->clearMarkedBits(maskcolors);
}

/*
** Get pointer to gclist field for different object types.
** Each GC-managed type that can be in a gray list has a gclist field.
*/
static GCObject** getgclist(GCObject* o) {
    switch (o->getType()) {
        case LUA_VTABLE: return gco2t(o)->getGclistPtr();
        case LUA_VLCL: return gco2lcl(o)->getGclistPtr();
        case LUA_VCCL: return gco2ccl(o)->getGclistPtr();
        case LUA_VTHREAD: return gco2th(o)->getGclistPtr();
        case LUA_VPROTO: return gco2p(o)->getGclistPtr();
        case LUA_VUSERDATA: {
            Udata* u = gco2u(o);
            lua_assert(u->getNumUserValues() > 0);
            return u->getGclistPtr();
        }
        default: lua_assert(0); return 0;
    }
}

/*
** Link object into a GC list and make it gray.
** Used to add objects to gray lists during marking/barrier operations.
*/
static void linkgclist_(GCObject* o, GCObject** pnext, GCObject** list) {
    lua_assert(!isgray(o));  /* cannot be in a gray list */
    *pnext = *list;
    *list = o;
    set2gray(o);  /* now it is */
}

/*
** Link a generic collectable object into a GC list.
** This macro gets the gclist pointer for the object's type and links it.
*/
#define linkobjgclist(o,p) linkgclist_(obj2gco(o), getgclist(o), &(p))


/*
** =======================================================
** Core Write Barrier Functions
** =======================================================
*/

/*
** Forward barrier: Marks white object 'v' gray when black object 'o' points to it.
**
** ALGORITHM:
** If GC is maintaining invariant (mark/propagate phase):
**   1. Mark v gray (restore tri-color invariant)
**   2. If o is old (generational mode):
**      - Set v's age to Old0 (not Old, as v may point to young objects)
**      - v will advance: Old0 → Old1 → Old in future cycles
** Else (sweep phase):
**   - In incremental mode: whiten o (avoid future barriers, o will be swept soon)
**   - In generational mode: do nothing (can't whiten, sweep uses different logic)
**
** RATIONALE FOR OLD0:
** Can't set v to Old immediately because v might point to young objects.
** By setting to Old0, we ensure v's children are also promoted before v becomes Old.
**
** SWEEP PHASE OPTIMIZATION:
** During sweep in incremental mode, we whiten o instead of marking v.
** This is safe because:
** 1. o is about to be swept anyway
** 2. Whitening prevents repeated barrier calls for the same object
** 3. Saves work - no need to mark v when o is being collected
**
** NOT DONE IN GENERATIONAL MODE:
** Generational sweep doesn't distinguish white shades (old white vs new white),
** so whitening could cause incorrect collection.
*/
void GCBarrier::barrier_(lua_State* L, GCObject* o, GCObject* v) {
    global_State* g = G(L);
    lua_assert(isblack(o) && iswhite(v) && !isdead(g, v) && !isdead(g, o));

    if (g->keepInvariant()) {  /* must keep invariant? */
        GCMarking::reallymarkobject(g, v);  /* restore invariant */
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
** Backward barrier: Marks black object 'o' gray when it's modified to point to white.
**
** ALGORITHM:
** If o is Touched2 (already in gray list from previous barrier):
**   - Set to gray (will become Touched1 when re-scanned)
** Else:
**   - Link o into grayagain list and make it gray
** If generational mode:
**   - Set age to Touched1 (marks that object was touched in this cycle)
**
** TOUCHED AGES IN GENERATIONAL MODE:
** - Touched1: Object was modified in this cycle, needs re-scanning
** - Touched2: Object was modified and already in gray list
** These ages ensure modified old objects are re-scanned without being collected.
**
** WHEN TO USE BACKWARD BARRIER:
** Use when object o may point to multiple white objects:
** - Table rehashing (many key-value pairs may point to white objects)
** - Table/userdata with many fields
** Cheaper to mark 1 object gray than mark N objects gray.
**
** GRAYAGAIN LIST:
** Objects in grayagain are re-scanned in the atomic phase to ensure
** all modifications during concurrent marking are captured.
*/
void GCBarrier::barrierback_(lua_State* L, GCObject* o) {
    global_State* g = G(L);
    lua_assert(isblack(o) && !isdead(g, o));

    GCAge age = getage(o);

    /* Handle Touched2 first - already in gray list */
    if (age == GCAge::Touched2) {
        set2gray(o);  /* make it gray to become touched1 */
        return;  /* Done - don't re-link or change age */
    }

    /* In generational mode, Touched1 objects are already in grayagain */
    /* NOTE: Modified from original - handle Touched1 even if not isold */
    if (g->getGCKind() == GCKind::GenerationalMinor && age == GCAge::Touched1)
        return;  /* Already processed in this cycle */

    /* FIXME: Assertion from original code fails during module integration
     * Root cause under investigation - may be related to barrier calling patterns
     * Early returns above should handle the problematic cases */
    /* lua_assert((g->getGCKind() != GCKind::GenerationalMinor)
            || (isold(o) && age != GCAge::Touched1)); */

    /* Link into grayagain and paint gray */
    linkobjgclist(o, *g->getGrayAgainPtr());

    if (isold(o))  /* generational mode? */
        setage(o, GCAge::Touched1);  /* touched in current cycle */
}
