/*
** $Id: lstring.c $
** String table (keeps all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#define lstring_c
#define LUA_CORE

#include "lprefix.h"


#include <string.h>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"


// Phase 29: Get offset of falloc field in TString
inline constexpr size_t tstringFallocOffset() noexcept {
  return TString::fallocOffset();
}

/*
** Maximum size for string table.
*/
#define MAXSTRTB	cast_int(luaM_limitN(INT_MAX, TString*))

/*
** Initial size for the string table (must be power of 2).
** The Lua core alone registers ~50 strings (reserved words +
** metaevent keys + a few others). Libraries would typically add
** a few dozens more.
*/
#if !defined(MINSTRTABSIZE)
#define MINSTRTABSIZE   128
#endif


/*
** generic equality for strings
*/
int luaS_eqstr (TString *a, TString *b) {
  return a->equals(b);
}


unsigned luaS_hash (const char *str, size_t l, unsigned seed) {
  unsigned int h = seed ^ cast_uint(l);
  for (; l > 0; l--)
    h ^= ((h<<5) + (h>>2) + cast_byte(str[l - 1]));
  return h;
}


unsigned luaS_hashlongstr (TString *ts) {
  return ts->hashLongStr();
}


static void tablerehash (TString **vect, int osize, int nsize) {
  int i;
  for (i = osize; i < nsize; i++)  /* clear new elements */
    vect[i] = NULL;
  for (i = 0; i < osize; i++) {  /* rehash old part of the array */
    TString *p = vect[i];
    vect[i] = NULL;
    while (p) {  /* for each string in the list */
      TString *hnext = p->getNext();  /* save next */
      unsigned int h = lmod(p->getHash(), nsize);  /* new position */
      p->setNext(vect[h]);  /* chain it into array */
      vect[h] = p;
      p = hnext;
    }
  }
}


/*
** Resize the string table. If allocation fails, keep the current size.
** (This can degrade performance, but any non-zero size should work
** correctly.)
*/
void luaS_resize (lua_State *L, int nsize) {
  stringtable *tb = &G(L)->strt;
  int osize = tb->getSize();
  TString **newvect;
  if (nsize < osize)  /* shrinking table? */
    tablerehash(tb->getHash(), osize, nsize);  /* depopulate shrinking part */
  newvect = luaM_reallocvector(L, tb->getHash(), osize, nsize, TString*);
  if (l_unlikely(newvect == NULL)) {  /* reallocation failed? */
    if (nsize < osize)  /* was it shrinking table? */
      tablerehash(tb->getHash(), nsize, osize);  /* restore to original size */
    /* leave table as it was */
  }
  else {  /* allocation succeeded */
    tb->setHash(newvect);
    tb->setSize(nsize);
    if (nsize > osize)
      tablerehash(newvect, osize, nsize);  /* rehash for new size */
  }
}


/*
** Clear API string cache. (Entries cannot be empty, so fill them with
** a non-collectable string.)
*/
void luaS_clearcache (global_State *g) {
  int i, j;
  for (i = 0; i < STRCACHE_N; i++)
    for (j = 0; j < STRCACHE_M; j++) {
      if (iswhite(g->strcache[i][j]))  /* will entry be collected? */
        g->strcache[i][j] = g->memerrmsg;  /* replace it with something fixed */
    }
}


/*
** Initialize the string table and the string cache
*/
void luaS_init (lua_State *L) {
  global_State *g = G(L);
  int i, j;
  stringtable *tb = &G(L)->strt;
  tb->setHash(luaM_newvector(L, MINSTRTABSIZE, TString*));
  tablerehash(tb->getHash(), 0, MINSTRTABSIZE);  /* clear array */
  tb->setSize(MINSTRTABSIZE);
  /* pre-create memory-error message */
  g->memerrmsg = luaS_newliteral(L, MEMERRMSG);
  obj2gco(g->memerrmsg)->fix(L);  /* Phase 25c: it should never be collected */
  for (i = 0; i < STRCACHE_N; i++)  /* fill cache with valid strings */
    for (j = 0; j < STRCACHE_M; j++)
      g->strcache[i][j] = g->memerrmsg;
}


size_t luaS_sizelngstr (size_t len, int kind) {
  switch (kind) {
    case LSTRREG:  /* regular long string */
      /* don't need 'falloc'/'ud', but need space for content */
      return tstringFallocOffset() + (len + 1) * sizeof(char);
    case LSTRFIX:  /* fixed external long string */
      /* don't need 'falloc'/'ud' */
      return tstringFallocOffset();
    default:  /* external long string with deallocation */
      lua_assert(kind == LSTRMEM);
      return sizeof(TString);
  }
}


/*
** creates a new string object
*/
static TString *createstrobj (lua_State *L, size_t totalsize, lu_byte tag,
                              unsigned h) {
  TString *ts;
  GCObject *o;
  o = luaC_newobj(L, tag, totalsize);
  ts = gco2ts(o);
  ts->setHash(h);
  ts->setExtra(0);
  return ts;
}


TString *luaS_createlngstrobj (lua_State *L, size_t l) {
  size_t totalsize = luaS_sizelngstr(l, LSTRREG);
  TString *ts = createstrobj(L, totalsize, LUA_VLNGSTR, G(L)->seed);
  ts->setLnglen(l);
  ts->setShrlen(LSTRREG);  /* signals that it is a regular long string */
  ts->setContents(cast_charp(ts) + tstringFallocOffset());
  ts->getContentsField()[l] = '\0';  /* ending 0 */
  return ts;
}


// Phase 26: Removed luaS_remove - now TString::remove() method


static void growstrtab (lua_State *L, stringtable *tb) {
  if (l_unlikely(tb->getNumElements() == INT_MAX)) {  /* too many strings? */
    luaC_fullgc(L, 1);  /* try to free some... */
    if (tb->getNumElements() == INT_MAX)  /* still too many? */
      luaM_error(L);  /* cannot even create a message... */
  }
  if (tb->getSize() <= MAXSTRTB / 2)  /* can grow string table? */
    luaS_resize(L, tb->getSize() * 2);
}


/*
** Checks whether short string exists and reuses it or creates a new one.
*/
static TString *internshrstr (lua_State *L, const char *str, size_t l) {
  TString *ts;
  global_State *g = G(L);
  stringtable *tb = &g->strt;
  unsigned int h = luaS_hash(str, l, g->seed);
  TString **list = &tb->getHash()[lmod(h, tb->getSize())];
  lua_assert(str != NULL);  /* otherwise 'memcmp'/'memcpy' are undefined */
  for (ts = *list; ts != NULL; ts = ts->getNext()) {
    if (l == cast_uint(ts->getShrlen()) &&
        (memcmp(str, getshrstr(ts), l * sizeof(char)) == 0)) {
      /* found! */
      if (isdead(g, ts))  /* dead (but not collected yet)? */
        changewhite(ts);  /* resurrect it */
      return ts;
    }
  }
  /* else must create a new string */
  if (tb->getNumElements() >= tb->getSize()) {  /* need to grow string table? */
    growstrtab(L, tb);
    list = &tb->getHash()[lmod(h, tb->getSize())];  /* rehash with new size */
  }
  ts = createstrobj(L, sizestrshr(l), LUA_VSHRSTR, h);
  ts->setShrlen(cast(ls_byte, l));
  getshrstr(ts)[l] = '\0';  /* ending 0 */
  memcpy(getshrstr(ts), str, l * sizeof(char));
  ts->setNext(*list);
  *list = ts;
  tb->incrementNumElements();
  return ts;
}


/*
** new string (with explicit length)
*/
TString *luaS_newlstr (lua_State *L, const char *str, size_t l) {
  if (l <= LUAI_MAXSHORTLEN)  /* short string? */
    return internshrstr(L, str, l);
  else {
    TString *ts;
    if (l_unlikely(l * sizeof(char) >= (MAX_SIZE - sizeof(TString))))
      luaM_toobig(L);
    ts = luaS_createlngstrobj(L, l);
    memcpy(getlngstr(ts), str, l * sizeof(char));
    return ts;
  }
}


/*
** Create or reuse a zero-terminated string, first checking in the
** cache (using the string address as a key). The cache can contain
** only zero-terminated strings, so it is safe to use 'strcmp' to
** check hits.
*/
TString *luaS_new (lua_State *L, const char *str) {
  unsigned int i = point2uint(str) % STRCACHE_N;  /* hash */
  int j;
  TString **p = G(L)->strcache[i];
  for (j = 0; j < STRCACHE_M; j++) {
    if (strcmp(str, getstr(p[j])) == 0)  /* hit? */
      return p[j];  /* that is it */
  }
  /* normal route */
  for (j = STRCACHE_M - 1; j > 0; j--)
    p[j] = p[j - 1];  /* move out last element */
  /* new element is first in the list */
  p[0] = luaS_newlstr(L, str, strlen(str));
  return p[0];
}


Udata *luaS_newudata (lua_State *L, size_t s, unsigned short nuvalue) {
  Udata *u;
  int i;
  GCObject *o;
  if (l_unlikely(s > MAX_SIZE - udatamemoffset(nuvalue)))
    luaM_toobig(L);
  o = luaC_newobj(L, LUA_VUSERDATA, sizeudata(nuvalue, s));
  u = gco2u(o);
  u->setLen(s);
  u->setNumUserValues(nuvalue);
  u->setMetatable(NULL);
  for (i = 0; i < nuvalue; i++)
    setnilvalue(&u->getUserValue(i)->uv);
  return u;
}


struct NewExt {
  ls_byte kind;
  const char *s;
   size_t len;
  TString *ts;  /* output */
};


static void f_newext (lua_State *L, void *ud) {
  struct NewExt *ne = cast(struct NewExt *, ud);
  size_t size = luaS_sizelngstr(0, ne->kind);
  ne->ts = createstrobj(L, size, LUA_VLNGSTR, G(L)->seed);
}


TString *luaS_newextlstr (lua_State *L,
	          const char *s, size_t len, lua_Alloc falloc, void *ud) {
  struct NewExt ne;
  if (!falloc) {
    ne.kind = LSTRFIX;
    f_newext(L, &ne);  /* just create header */
  }
  else {
    ne.kind = LSTRMEM;
    if (L->rawRunProtected( f_newext, &ne) != LUA_OK) {  /* mem. error? */
      (*falloc)(ud, cast_voidp(s), len + 1, 0);  /* free external string */
      luaM_error(L);  /* re-raise memory error */
    }
    ne.ts->setFalloc(falloc);
    ne.ts->setUserData(ud);
  }
  ne.ts->setShrlen(ne.kind);
  ne.ts->setLnglen(len);
  ne.ts->setContents(cast_charp(s));
  return ne.ts;
}


/*
** TString method implementations
*/

unsigned TString::hashLongStr() {
  lua_assert(this->getType() == LUA_VLNGSTR);
  if (this->getExtra() == 0) {  /* no hash? */
    size_t len = this->getLnglen();
    this->setHash(luaS_hash(getlngstr(this), len, this->getHash()));
    this->setExtra(1);  /* now it has its hash */
  }
  return this->getHash();
}

bool TString::equals(TString* other) {
  size_t len1, len2;
  const char *s1 = getlstr(this, len1);
  const char *s2 = getlstr(other, len2);
  return ((len1 == len2) &&  /* equal length and ... */
          (memcmp(s1, s2, len1) == 0));  /* equal contents */
}

// Phase 25a: Convert luaS_remove to TString method
void TString::remove(lua_State* L) {
  stringtable *tb = &G(L)->strt;
  TString **p = &tb->getHash()[lmod(this->getHash(), tb->getSize())];
  while (*p != this)  /* find previous element */
    p = &(*p)->u.hnext;
  *p = (*p)->u.hnext;  /* remove element from its list */
  tb->decrementNumElements();
}

// Phase 25a: Convert luaS_normstr to TString method
TString* TString::normalize(lua_State* L) {
  size_t len = this->u.lnglen;
  if (len > LUAI_MAXSHORTLEN)
    return this;  /* long string; keep the original */
  else {
    const char *str = getlngstr(this);
    return internshrstr(L, str, len);
  }
}

