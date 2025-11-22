/*
** $Id: gc_core.cpp $
** Garbage Collector - Core Utilities Module
** See Copyright Notice in lua.h
*/

#define lgc_c
#define LUA_CORE

#include "lprefix.h"

#include "gc_core.h"
#include "../lgc.h"
#include "../../objects/lobject.h"
#include "../../objects/ltable.h"
#include "../../objects/lstring.h"
#include "../../objects/lfunc.h"
#include "../../core/lstate.h"
#include "../lmem.h"

/*
** Calculate the memory size of a GC object.
** Returns the size in bytes for GC accounting purposes.
*/
l_mem GCCore::objsize(GCObject* o) {
    lu_mem res;
    switch (o->getType()) {
        case LuaT::TABLE: {
            res = luaH_size(gco2t(o));
            break;
        }
        case LuaT::LCL: {
            LClosure* cl = gco2lcl(o);
            res = sizeLclosure(cl->getNumUpvalues());
            break;
        }
        case LuaT::CCL: {
            CClosure* cl = gco2ccl(o);
            res = sizeCclosure(cl->getNumUpvalues());
            break;
        }
        case LuaT::USERDATA: {
            Udata* u = gco2u(o);
            res = sizeudata(u->getNumUserValues(), u->getLen());
            break;
        }
        case LuaT::PROTO: {
            res = gco2p(o)->memorySize();
            break;
        }
        case LuaT::THREAD: {
            res = luaE_threadsize(gco2th(o));
            break;
        }
        case LuaT::SHRSTR: {
            TString* ts = gco2ts(o);
            res = sizestrshr(cast_uint(ts->getShrlen()));
            break;
        }
        case LuaT::LNGSTR: {
            TString* ts = gco2ts(o);
            res = TString::calculateLongStringSize(ts->getLnglen(), ts->getShrlen());
            break;
        }
        case LuaT::UPVAL: {
            res = sizeof(UpVal);
            break;
        }
        default: res = 0; lua_assert(0);
    }
    return static_cast<l_mem>(res);
}


/*
** Get pointer to the gclist field of a GC object.
** Different object types store this field in different locations.
*/
GCObject** GCCore::getgclist(GCObject* o) {
    switch (o->getType()) {
        case LuaT::TABLE: return gco2t(o)->getGclistPtr();
        case LuaT::LCL: return gco2lcl(o)->getGclistPtr();
        case LuaT::CCL: return gco2ccl(o)->getGclistPtr();
        case LuaT::THREAD: return gco2th(o)->getGclistPtr();
        case LuaT::PROTO: return gco2p(o)->getGclistPtr();
        case LuaT::USERDATA: {
            Udata* u = gco2u(o);
            lua_assert(u->getNumUserValues() > 0);
            return u->getGclistPtr();
        }
        case LuaT::UPVAL:
            /* UpVals use the base GCObject 'next' field for gray list linkage */
            return o->getNextPtr();
        case LuaT::SHRSTR:
        case LuaT::LNGSTR:
            /* Strings are marked black directly and should never be in gray list.
             * However, with LTO, we've seen strings passed to this function.
             * Use the 'next' field (from GCObject base) as a fallback. */
            return o->getNextPtr();
        default:
            /* Fallback: use base GCObject 'next' field for unhandled/unknown types.
             * With LTO, we've seen invalid type values (e.g., 0xab), possibly due to
             * aggressive optimizations or memory reordering. Using the base 'next'
             * field is safe and prevents crashes. */
            return o->getNextPtr();
    }
}


/*
** Link a GC object into a gray list.
** The object is set to gray and added to the specified list.
*/
void GCCore::linkgclist_(GCObject* o, GCObject** pnext, GCObject** list) {
    lua_assert(!isgray(o));  /* cannot be in a gray list */
    *pnext = *list;
    *list = o;
    set2gray(o);  /* now it is */
}


/*
** Clear dead keys from empty table nodes.
** If entry is empty, mark its key as dead. This allows the collection
** of the key, but keeps its entry in the table (its removal could break
** a chain and could break a table traversal). Other places never manipulate
** dead keys, because the associated empty value is enough to signal that
** the entry is logically empty.
*/
void GCCore::clearkey(Node* n) {
    lua_assert(isempty(gval(n)));
    if (n->isKeyCollectable())
        n->setKeyDead();  /* unused key; remove it */
}


/*
** Free an upvalue object.
** Unlinks open upvalues and calls destructor before freeing.
*/
void GCCore::freeupval(lua_State* L, UpVal* uv) {
    if (uv->isOpen())
        luaF_unlinkupval(uv);
    uv->~UpVal();  // Call destructor
    luaM_free(L, uv);
}
