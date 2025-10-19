/*
** $Id: ltable.h $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/

#ifndef ltable_h
#define ltable_h

#include "lobject.h"


// Phase 19: Table accessor macros converted to inline functions
// Note: Original macro did implicit const-cast, so non-const version works with const Table*
inline Node* gnode(Table* t, unsigned int i) noexcept { return t->getNode(i); }
inline Node* gnode(const Table* t, unsigned int i) noexcept {
  return const_cast<Table*>(t)->getNode(i);
}
inline TValue* gval(Node* n) noexcept { return &n->i_val; }
inline const TValue* gval(const Node* n) noexcept { return &n->i_val; }
// gnext returns reference to allow modification
inline int& gnext(Node* n) noexcept { return n->u.next; }
inline int gnext(const Node* n) noexcept { return n->u.next; }


/*
** Clear all bits of fast-access metamethods, which means that the table
** may have any of these metamethods. (First access that fails after the
** clearing will set the bit again.)
** Note: invalidateTMcache is now an inline function defined in ltm.h
*/


/*
** Bit BITDUMMY set in 'flags' means the table is using the dummy node
** for its hash part.
*/

inline constexpr lu_byte BITDUMMY = (1 << 6);
inline constexpr lu_byte NOTBITDUMMY = cast_byte(~BITDUMMY);
/* Phase 44.1: isdummy, setnodummy, setdummy now Table methods isDummy(), setNoDummy(), setDummy() */
/* Phase 44.1: allocsizenode now Table::allocatedNodeSize() method */
/* Phase 44.1: nodefromval replaced with direct cast(Node*, v) */



#define luaH_fastgeti(t,k,res,tag) \
  { Table *h = t; lua_Unsigned u = l_castS2U(k) - 1u; \
    if ((u < h->arraySize())) { \
      tag = *h->getArrayTag(u); \
      if (!tagisempty(tag)) { farr2val(h, u, tag, (res)); }} \
    else { tag = luaH_getint(h, (k), res); }}


#define luaH_fastseti(t,k,val,hres) \
  { Table *h = t; lua_Unsigned u = l_castS2U(k) - 1u; \
    if ((u < h->arraySize())) { \
      lu_byte *tag = h->getArrayTag(u); \
      if (checknoTM(h->getMetatable(), TM_NEWINDEX) || !tagisempty(*tag)) \
        { fval2arr(h, u, tag, (val)); hres = HOK; } \
      else hres = ~cast_int(u); } \
    else { hres = luaH_psetint(h, k, val); }}


/* results from pset */
inline constexpr int HOK = 0;
inline constexpr int HNOTFOUND = 1;
inline constexpr int HNOTATABLE = 2;
inline constexpr int HFIRSTNODE = 3;

/*
** 'luaH_get*' operations set 'res', unless the value is absent, and
** return the tag of the result.
** The 'luaH_pset*' (pre-set) operations set the given value and return
** HOK, unless the original value is absent. In that case, if the key
** is really absent, they return HNOTFOUND. Otherwise, if there is a
** slot with that key but with no value, 'luaH_pset*' return an encoding
** of where the key is (usually called 'hres'). (pset cannot set that
** value because there might be a metamethod.) If the slot is in the
** hash part, the encoding is (HFIRSTNODE + hash index); if the slot is
** in the array part, the encoding is (~array index), a negative value.
** The value HNOTATABLE is used by the fast macros to signal that the
** value being indexed is not a table.
** (The size for the array part is limited by the maximum power of two
** that fits in an unsigned integer; that is INT_MAX+1. So, the C-index
** ranges from 0, which encodes to -1, to INT_MAX, which encodes to
** INT_MIN. The size of the hash part is limited by the maximum power of
** two that fits in a signed integer; that is (INT_MAX+1)/2. So, it is
** safe to add HFIRSTNODE to any index there.)
*/


/*
** The array part of a table is represented by an inverted array of
** values followed by an array of tags, to avoid wasting space with
** padding. In between them there is an unsigned int, explained later.
** The 'array' pointer points between the two arrays, so that values are
** indexed with negative indices and tags with non-negative indices.

             Values                              Tags
  --------------------------------------------------------
  ...  |   Value 1     |   Value 0     |unsigned|0|1|...
  --------------------------------------------------------
                                       ^ t->array

** All accesses to 't->array' should be through Table methods getArrayTag()
** and getArrayVal().
*/

/* Phase 44.1: getArrTag now Table::getArrayTag(k) method */
/* Phase 44.1: getArrVal now Table::getArrayVal(k) method */


/*
** The unsigned between the two arrays is used as a hint for #t;
** see luaH_getn. It is stored there to avoid wasting space in
** the structure Table for tables with no array part.
*/
/* Phase 44.1: lenhint now Table::getLenHint() method */


/*
** Move TValues to/from arrays, using C indices
** Phase 27: Converted to inline functions using TValue methods
** Phase 44.1: Updated to use Table methods instead of macros
*/
inline void arr2obj(const Table* h, lua_Unsigned k, TValue* val) noexcept {
  val->setType(*h->getArrayTag(k));
  val->valueField() = *h->getArrayVal(k);
}

inline void obj2arr(Table* h, lua_Unsigned k, const TValue* val) noexcept {
  *h->getArrayTag(k) = val->getType();
  *h->getArrayVal(k) = val->getValue();
}

/*
** Often, we need to check the tag of a value before moving it. The
** following inline functions also move TValues to/from arrays, but receive the
** precomputed tag value or address as an extra argument.
*/
inline void farr2val(const Table* h, lua_Unsigned k, lu_byte tag, TValue* res) noexcept {
  res->setType(tag);
  res->valueField() = *h->getArrayVal(k);
}

inline void fval2arr(Table* h, lua_Unsigned k, lu_byte* tag, const TValue* val) noexcept {
  *tag = val->getType();
  *h->getArrayVal(k) = val->getValue();
}


LUAI_FUNC lu_byte luaH_get (Table *t, const TValue *key, TValue *res);
LUAI_FUNC lu_byte luaH_getshortstr (Table *t, TString *key, TValue *res);
LUAI_FUNC lu_byte luaH_getstr (Table *t, TString *key, TValue *res);
LUAI_FUNC lu_byte luaH_getint (Table *t, lua_Integer key, TValue *res);

/* Special get for metamethods */
LUAI_FUNC const TValue *luaH_Hgetshortstr (Table *t, TString *key);

LUAI_FUNC int luaH_psetint (Table *t, lua_Integer key, TValue *val);
LUAI_FUNC int luaH_psetshortstr (Table *t, TString *key, TValue *val);
LUAI_FUNC int luaH_psetstr (Table *t, TString *key, TValue *val);
LUAI_FUNC int luaH_pset (Table *t, const TValue *key, TValue *val);

LUAI_FUNC void luaH_setint (lua_State *L, Table *t, lua_Integer key,
                                                    TValue *value);
LUAI_FUNC void luaH_set (lua_State *L, Table *t, const TValue *key,
                                                 TValue *value);

LUAI_FUNC void luaH_finishset (lua_State *L, Table *t, const TValue *key,
                                              TValue *value, int hres);
LUAI_FUNC Table *luaH_new (lua_State *L);
LUAI_FUNC void luaH_resize (lua_State *L, Table *t, unsigned nasize,
                                                    unsigned nhsize);
LUAI_FUNC void luaH_resizearray (lua_State *L, Table *t, unsigned nasize);
LUAI_FUNC lu_mem luaH_size (Table *t);
LUAI_FUNC void luaH_free (lua_State *L, Table *t);
LUAI_FUNC int luaH_next (lua_State *L, Table *t, StkId key);
LUAI_FUNC lua_Unsigned luaH_getn (lua_State *L, Table *t);


#if defined(LUA_DEBUG)
LUAI_FUNC Node *luaH_mainposition (const Table *t, const TValue *key);
#endif


#endif
