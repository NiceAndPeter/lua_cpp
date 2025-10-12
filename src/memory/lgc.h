/*
** $Id: lgc.h $
** Garbage Collector
** See Copyright Notice in lua.h
*/

#ifndef lgc_h
#define lgc_h


#include <stddef.h>


#include "lobject.h"
#include "lstate.h"

/*
** Collectable objects may have one of three colors: white, which means
** the object is not marked; gray, which means the object is marked, but
** its references may be not marked; and black, which means that the
** object and all its references are marked.  The main invariant of the
** garbage collector, while marking objects, is that a black object can
** never point to a white one. Moreover, any gray object must be in a
** "gray list" (gray, grayagain, weak, allweak, ephemeron) so that it
** can be visited again before finishing the collection cycle. (Open
** upvalues are an exception to this rule, as they are attached to
** a corresponding thread.)  These lists have no meaning when the
** invariant is not being enforced (e.g., sweep phase).
*/


/*
** Possible states of the Garbage Collector
*/
#define GCSpropagate	0
#define GCSenteratomic	1
#define GCSatomic	2
#define GCSswpallgc	3
#define GCSswpfinobj	4
#define GCSswptobefnz	5
#define GCSswpend	6
#define GCScallfin	7
#define GCSpause	8


#define issweepphase(g)  \
	(GCSswpallgc <= (g)->gcstate && (g)->gcstate <= GCSswpend)


/*
** macro to tell when main invariant (white objects cannot point to black
** ones) must be kept. During a collection, the sweep phase may break
** the invariant, as objects turned white may point to still-black
** objects. The invariant is restored when sweep ends and all objects
** are white again.
*/

#define keepinvariant(g)	((g)->gcstate <= GCSatomic)


/*
** some useful bit tricks
*/

// Bit mask generation
constexpr int bitmask(int b) noexcept {
    return (1 << b);
}

constexpr lu_byte bit2mask(int b1, int b2) noexcept {
    return cast_byte(bitmask(b1) | bitmask(b2));
}

// Bit testing
constexpr lu_byte testbits(lu_byte x, lu_byte m) noexcept {
    return (x & m);
}

constexpr bool testbit(lu_byte x, int b) noexcept {
    return (testbits(x, cast_byte(bitmask(b))) != 0);
}

// Bit manipulation - keep as macros since they modify arguments
#define setbits(x,m)		((x) = cast_byte((x) | (m)))
#define resetbits(x,m)		((x) &= cast_byte(~(m)))
#define l_setbit(x,b)		setbits(x, bitmask(b))
#define resetbit(x,b)		resetbits(x, bitmask(b))


/*
** Layout for bit use in 'marked' field. First three bits are
** used for object "age" in generational mode. Last bit is used
** by tests.
*/
#define WHITE0BIT	3  /* object is white (type 0) */
#define WHITE1BIT	4  /* object is white (type 1) */
#define BLACKBIT	5  /* object is black */
#define FINALIZEDBIT	6  /* object has been marked for finalization */

#define TESTBIT		7



#define WHITEBITS	bit2mask(WHITE0BIT, WHITE1BIT)

/* object age in generational mode */
#define G_NEW		0	/* created in current cycle */
#define G_SURVIVAL	1	/* created in previous cycle */
#define G_OLD0		2	/* marked old by frw. barrier in this cycle */
#define G_OLD1		3	/* first full cycle as old */
#define G_OLD		4	/* really old object (not to be visited) */
#define G_TOUCHED1	5	/* old object touched this cycle */
#define G_TOUCHED2	6	/* old object touched in previous cycle */

#define AGEBITS		7  /* all age bits (111) */

// GCObject color and age inline method implementations

inline bool GCObject::isWhite() const noexcept {
  return testbits(marked, WHITEBITS);
}

inline bool GCObject::isBlack() const noexcept {
  return testbit(marked, BLACKBIT);
}

inline bool GCObject::isGray() const noexcept {
  return !testbits(marked, bitmask(BLACKBIT) | WHITEBITS);
}

inline lu_byte GCObject::getAge() const noexcept {
  return marked & AGEBITS;
}

inline void GCObject::setAge(lu_byte age) noexcept {
  marked = cast_byte((marked & (~AGEBITS)) | age);
}

inline bool GCObject::isOld() const noexcept {
  return getAge() > G_SURVIVAL;
}

template<typename Derived>
inline void GCBase<Derived>::setAge(lu_byte age) noexcept {
  marked = cast_byte((marked & (~AGEBITS)) | age);
}

template<typename Derived>
inline bool GCBase<Derived>::isOld() const noexcept {
  return getAge() > G_SURVIVAL;
}

// Wrapper functions for backward compatibility
// Accept any GC-managed type pointer (uses reinterpret_cast like original macros)
template<typename T>
inline bool iswhite(const T* x) noexcept {
  return reinterpret_cast<const GCObject*>(x)->isWhite();
}

template<typename T>
inline bool isblack(const T* x) noexcept {
  return reinterpret_cast<const GCObject*>(x)->isBlack();
}

template<typename T>
inline bool isgray(const T* x) noexcept {
  return reinterpret_cast<const GCObject*>(x)->isGray();
}

template<typename T>
inline lu_byte getage(const T* o) noexcept {
  return reinterpret_cast<const GCObject*>(o)->getAge();
}

template<typename T>
inline void setage(T* o, lu_byte a) noexcept {
  reinterpret_cast<GCObject*>(o)->setAge(a);
}

template<typename T>
inline bool isold(const T* o) noexcept {
  return reinterpret_cast<const GCObject*>(o)->isOld();
}

inline bool tofinalize(const GCObject* x) noexcept {
	return testbit(x->getMarked(), FINALIZEDBIT);
}

/* Get the "other" white color (for dead object detection) */
constexpr lu_byte otherwhite(const global_State* g) noexcept {
	return g->currentwhite ^ WHITEBITS;
}

/* Check if marked value is dead given other-white bits */
constexpr bool isdeadm(lu_byte ow, lu_byte m) noexcept {
	return (m & ow) != 0;
}

/* Check if a GC object is dead */
inline bool isdead(const global_State* g, const GCObject* v) noexcept {
	return isdeadm(otherwhite(g), v->getMarked());
}

/* Template version for any GC-able type (Table, TString, UpVal, etc.) */
template<typename T>
inline bool isdead(const global_State* g, const T* v) noexcept {
	return isdeadm(otherwhite(g), reinterpret_cast<const GCObject*>(v)->getMarked());
}

inline void changewhite(GCObject* x) noexcept {
	x->setMarked(x->getMarked() ^ WHITEBITS);
}

inline void nw2black(GCObject* x) noexcept {
	l_setbit(x->getMarkedRef(), BLACKBIT);
}

#define luaC_white(g)	cast_byte((g)->currentwhite & WHITEBITS)

/* Note: G_NEW, G_SURVIVAL, G_OLD*, G_TOUCHED*, AGEBITS moved above for inline functions */
/* Note: getage, setage, isold are now inline functions defined above */


/*
** In generational mode, objects are created 'new'. After surviving one
** cycle, they become 'survival'. Both 'new' and 'survival' can point
** to any other object, as they are traversed at the end of the cycle.
** We call them both 'young' objects.
** If a survival object survives another cycle, it becomes 'old1'.
** 'old1' objects can still point to survival objects (but not to
** new objects), so they still must be traversed. After another cycle
** (that, being old, 'old1' objects will "survive" no matter what)
** finally the 'old1' object becomes really 'old', and then they
** are no more traversed.
**
** To keep its invariants, the generational mode uses the same barriers
** also used by the incremental mode. If a young object is caught in a
** forward barrier, it cannot become old immediately, because it can
** still point to other young objects. Instead, it becomes 'old0',
** which in the next cycle becomes 'old1'. So, 'old0' objects is
** old but can point to new and survival objects; 'old1' is old
** but cannot point to new objects; and 'old' cannot point to any
** young object.
**
** If any old object ('old0', 'old1', 'old') is caught in a back
** barrier, it becomes 'touched1' and goes into a gray list, to be
** visited at the end of the cycle.  There it evolves to 'touched2',
** which can point to survivals but not to new objects. In yet another
** cycle then it becomes 'old' again.
**
** The generational mode must also control the colors of objects,
** because of the barriers.  While the mutator is running, young objects
** are kept white. 'old', 'old1', and 'touched2' objects are kept black,
** as they cannot point to new objects; exceptions are threads and open
** upvalues, which age to 'old1' and 'old' but are kept gray. 'old0'
** objects may be gray or black, as in the incremental mode. 'touched1'
** objects are kept gray, as they must be visited again at the end of
** the cycle.
*/


/*
** {======================================================
** Default Values for GC parameters
** =======================================================
*/

/*
** Minor collections will shift to major ones after LUAI_MINORMAJOR%
** bytes become old.
*/
#define LUAI_MINORMAJOR         70

/*
** Major collections will shift to minor ones after a collection
** collects at least LUAI_MAJORMINOR% of the new bytes.
*/
#define LUAI_MAJORMINOR         50

/*
** A young (minor) collection will run after creating LUAI_GENMINORMUL%
** new bytes.
*/
#define LUAI_GENMINORMUL         20


/* incremental */

/* Number of bytes must be LUAI_GCPAUSE% before starting new cycle */
#define LUAI_GCPAUSE    250

/*
** Step multiplier: The collector handles LUAI_GCMUL% work units for
** each new allocated word. (Each "work unit" corresponds roughly to
** sweeping one object or traversing one slot.)
*/
#define LUAI_GCMUL      200

/* How many bytes to allocate before next GC step */
#define LUAI_GCSTEPSIZE	(200 * sizeof(Table))


#define setgcparam(g,p,v)  (g->gcparams[LUA_GCP##p] = luaO_codeparam(v))
#define applygcparam(g,p,x)  luaO_applyparam(g->gcparams[LUA_GCP##p], x)

/* }====================================================== */


/*
** Control when GC is running:
*/
#define GCSTPUSR	1  /* bit true when GC stopped by user */
#define GCSTPGC		2  /* bit true when GC stopped by itself */
#define GCSTPCLS	4  /* bit true when closing Lua state */
#define gcrunning(g)	((g)->gcstp == 0)


/*
** Does one step of collection when debt becomes zero. 'pre'/'pos'
** allows some adjustments to be done only when needed. macro
** 'condchangemem' is used only for heavy tests (forcing a full
** GC cycle on every opportunity)
*/

#if !defined(HARDMEMTESTS)
#define condchangemem(L,pre,pos,emg)	((void)0)
#else
#define condchangemem(L,pre,pos,emg)  \
	{ if (gcrunning(G(L))) { pre; luaC_fullgc(L, emg); pos; } }
#endif

#define luaC_condGC(L,pre,pos) \
	{ if (G(L)->GCdebt <= 0) { pre; luaC_step(L); pos;}; \
	  condchangemem(L,pre,pos,0); }

/* more often than not, 'pre'/'pos' are empty */
#define luaC_checkGC(L)		luaC_condGC(L,(void)0,(void)0)


#define luaC_objbarrier(L,p,o) (  \
	(isblack(p) && iswhite(o)) ? \
	luaC_barrier_(L,obj2gco(p),obj2gco(o)) : cast_void(0))

#define luaC_barrier(L,p,v) (  \
	iscollectable(v) ? luaC_objbarrier(L,p,gcvalue(v)) : cast_void(0))

#define luaC_objbarrierback(L,p,o) (  \
	(isblack(p) && iswhite(o)) ? luaC_barrierback_(L,p) : cast_void(0))

#define luaC_barrierback(L,p,v) (  \
	iscollectable(v) ? luaC_objbarrierback(L, p, gcvalue(v)) : cast_void(0))

/* Use GCObject::fix() method instead of luaC_fix */
LUAI_FUNC void luaC_freeallobjects (lua_State *L);
LUAI_FUNC void luaC_step (lua_State *L);
LUAI_FUNC void luaC_runtilstate (lua_State *L, int state, int fast);
LUAI_FUNC void luaC_fullgc (lua_State *L, int isemergency);
LUAI_FUNC GCObject *luaC_newobj (lua_State *L, lu_byte tt, size_t sz);
LUAI_FUNC GCObject *luaC_newobjdt (lua_State *L, lu_byte tt, size_t sz,
                                                 size_t offset);
LUAI_FUNC void luaC_barrier_ (lua_State *L, GCObject *o, GCObject *v);
LUAI_FUNC void luaC_barrierback_ (lua_State *L, GCObject *o);
/* Use GCObject::checkFinalizer() method instead of luaC_checkfinalizer */
LUAI_FUNC void luaC_changemode (lua_State *L, int newmode);


/*
** {==================================================================
** Placement new operator implementations for GC-allocated objects
** ===================================================================
*/

// CClosure placement new operator
inline void* CClosure::operator new(size_t size, lua_State* L, lu_byte tt, size_t extra) {
  return luaC_newobj(L, tt, size + extra);
}

// LClosure placement new operator
inline void* LClosure::operator new(size_t size, lua_State* L, lu_byte tt, size_t extra) {
  return luaC_newobj(L, tt, size + extra);
}

/* }================================================================== */


/*
** {==================================================================
** TValue assignment inline functions
** Defined here (not in lobject.h) because they need:
**   - G() from lstate.h
**   - isdead() from lgc.h
** ===================================================================
*/

/* Main function to copy values (from 'obj2' to 'obj1') */
inline void setobj(lua_State* L, TValue* obj1, const TValue* obj2) noexcept {
	obj1->valueField() = obj2->getValue();
	settt_(obj1, obj2->getType());
	checkliveness(L, obj1);
	lua_assert(!isnonstrictnil(obj1));
}

/* from stack to stack */
inline void setobjs2s(lua_State* L, StackValue* o1, StackValue* o2) noexcept {
	setobj(L, s2v(o1), s2v(o2));
}

/* to stack (not from same stack) */
inline void setobj2s(lua_State* L, StackValue* o1, const TValue* o2) noexcept {
	setobj(L, s2v(o1), o2);
}

/* }================================================================== */


#endif
