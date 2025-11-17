/*
** $Id: gc_barrier.h $
** Garbage Collector - Write Barrier Module
** See Copyright Notice in lua.h
*/

#ifndef gc_barrier_h
#define gc_barrier_h

#include "../../core/lstate.h"
#include "../lgc.h"
#include "../../objects/lobject.h"

/*
** GCBarrier - Encapsulates all garbage collector write barrier logic
**
** This module handles write barriers for Lua's tri-color incremental
** garbage collector. Write barriers maintain the tri-color invariant
** when the mutator (running program) modifies objects during collection.
**
** KEY CONCEPTS:
** - Tri-color invariant: No black object points to a white object
** - Forward barrier: Mark the white object gray (move collector forward)
** - Backward barrier: Mark the black object gray again (move collector backward)
**
** WHEN BARRIERS ARE NEEDED:
** When a black object O is modified to point to a white object V:
**   1. Forward barrier (barrier_): Mark V gray, making it reachable
**   2. Backward barrier (barrierback_): Mark O gray, will re-scan O's fields
**
** BARRIER SELECTION:
** - Forward barrier: Used when setting a single field (cheaper - marks 1 object)
** - Backward barrier: Used when setting many fields (cheaper - marks 1 object instead of many)
**
** GENERATIONAL MODE INVARIANTS:
** - Objects advance through ages: New → Survival → Old0 → Old1 → Old
** - Forward barrier sets age to Old0 (not Old immediately, as V may point to young objects)
** - Backward barrier sets age to Touched1 (links into grayagain for re-scanning)
**
** SWEEP PHASE OPTIMIZATION:
** - In incremental mode during sweep, forward barrier whitens O instead of marking V
** - Rationale: O will be swept soon anyway, avoid unnecessary marking work
** - Not done in generational mode (sweep doesn't distinguish white from dead)
**
** PUBLIC API (in lgc.h):
** - luaC_barrier(L, p, v): Forward barrier with type check (macro)
** - luaC_objbarrier(L, p, o): Forward barrier for GCObjects (macro)
** - luaC_barrierback(L, p, v): Backward barrier with type check (macro)
** - luaC_objbarrierback(L, p, o): Backward barrier for GCObjects (macro)
**
** IMPLEMENTATION FUNCTIONS:
** - barrier_(): Core forward barrier implementation
** - barrierback_(): Core backward barrier implementation
*/
class GCBarrier {
public:
    /*
    ** Forward barrier: Mark white object 'v' gray when black object 'o' points to it.
    **
    ** PRECONDITIONS:
    ** - o is black (fully processed)
    ** - v is white (unreachable/unprocessed)
    ** - Neither o nor v is dead
    **
    ** BEHAVIOR:
    ** If GC is maintaining invariant (not in sweep phase):
    **   1. Mark v gray (reallymarkobject) to restore tri-color invariant
    **   2. If o is old in generational mode, set v's age to Old0
    **      (v will advance to Old1, then Old in future cycles)
    ** Else (sweep phase):
    **   - In incremental mode: whiten o (will be swept soon, avoid future barriers)
    **   - In generational mode: do nothing (can't whiten, sweep doesn't use color)
    */
    static void barrier_(lua_State* L, GCObject* o, GCObject* v);

    /*
    ** Backward barrier: Mark black object 'o' gray when it's modified to point to white.
    **
    ** PRECONDITIONS:
    ** - o is black (fully processed)
    ** - o is not dead
    ** - In generational mode: o is old and not already Touched1
    **
    ** BEHAVIOR:
    ** If o is Touched2 (already in gray list):
    **   - Set to gray (will become Touched1 in next scan)
    ** Else:
    **   - Link o into grayagain list and make it gray
    ** If generational mode:
    **   - Set age to Touched1 (marks that it was touched in this cycle)
    **
    ** RATIONALE:
    ** Used when object o may point to multiple white objects (e.g., table resize).
    ** Cheaper to mark 1 black object gray than mark N white objects gray.
    */
    static void barrierback_(lua_State* L, GCObject* o);
};

#endif /* gc_barrier_h */
