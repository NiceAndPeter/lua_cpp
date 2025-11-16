/*
** $Id: lundump.c $
** load precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#define lundump_c
#define LUA_CORE

#include "lprefix.h"


#include <algorithm>
#include <climits>
#include <cstring>

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstring.h"
#include "ltable.h"
#include "lundump.h"
#include "lzio.h"


#if !defined(luai_verifycode)
#define luai_verifycode(L,f)  /* empty */
#endif


typedef struct {
  lua_State *L;
  ZIO *Z;
  const char *name;
  Table *h;  /* list for string reuse */
  size_t offset;  /* current position relative to beginning of dump */
  lua_Unsigned nstr;  /* number of strings in the list */
  lu_byte fixed;  /* dump is fixed in memory */
} LoadState;


static l_noret error (LoadState *S, const char *why) {
  luaO_pushfstring(S->L, "%s: bad binary format (%s)", S->name, why);
  S->L->doThrow( LUA_ERRSYNTAX);
}


/*
** All high-level loads go through loadVector; you can change it to
** adapt to the endianness of the input
*/
template<typename T>
inline void loadVector(LoadState* S, T* b, size_t n) {
	loadBlock(S, b, cast_sizet(n) * sizeof(T));
}

static void loadBlock (LoadState *S, void *b, size_t size) {
  if (luaZ_read(S->Z, b, size) != 0)
    error(S, "truncated chunk");
  S->offset += size;
}


static void loadAlign (LoadState *S, unsigned align) {
  unsigned padding = align - cast_uint(S->offset % align);
  if (padding < align) {  /* (padding == align) means no padding */
    lua_Integer paddingContent;
    loadBlock(S, &paddingContent, padding);
    lua_assert(S->offset % align == 0);
  }
}


#define getaddr(S,n,t)	cast(t *, getaddr_(S,cast_sizet(n) * sizeof(t)))

static const void *getaddr_ (LoadState *S, size_t size) {
  const void *block = luaZ_getaddr(S->Z, size);
  S->offset += size;
  if (block == NULL)
    error(S, "truncated fixed buffer");
  return block;
}


template<typename T>
inline void loadVar(LoadState* S, T& x) {
	loadVector(S, &x, 1);
}


static lu_byte loadByte (LoadState *S) {
  int b = zgetc(S->Z);
  if (b == EOZ)
    error(S, "truncated chunk");
  S->offset++;
  return cast_byte(b);
}


static lua_Unsigned loadVarint (LoadState *S, lua_Unsigned limit) {
  lua_Unsigned x = 0;
  int b;
  limit >>= 7;
  do {
    b = loadByte(S);
    if (x > limit)
      error(S, "integer overflow");
    x = (x << 7) | (b & 0x7f);
  } while ((b & 0x80) != 0);
  return x;
}


static size_t loadSize (LoadState *S) {
  return cast_sizet(loadVarint(S, MAX_SIZE));
}


static int loadInt (LoadState *S) {
  return cast_int(loadVarint(S, cast_sizet(std::numeric_limits<int>::max())));
}



static lua_Number loadNumber (LoadState *S) {
  lua_Number x;
  loadVar(S, x);
  return x;
}


static lua_Integer loadInteger (LoadState *S) {
  lua_Unsigned cx = loadVarint(S, LUA_MAXUNSIGNED);
  /* decode unsigned to signed */
  if ((cx & 1) != 0)
    return l_castU2S(~(cx >> 1));
  else
    return l_castU2S(cx >> 1);
}


/*
** Load a nullable string into slot 'sl' from prototype 'p'. The
** assignment to the slot and the barrier must be performed before any
** possible GC activity, to anchor the string. (Both 'loadVector' and
** 'luaH_setint' can call the GC.)
*/
static void loadString (LoadState *S, Proto *p, TString **sl) {
  lua_State *L = S->L;
  TString *ts;
  TValue sv;
  size_t size = loadSize(S);
  if (size == 0) {  /* no string? */
    lua_assert(*sl == NULL);  /* must be prefilled */
    return;
  }
  else if (size == 1) {  /* previously saved string? */
    lua_Unsigned idx = loadVarint(S, LUA_MAXUNSIGNED);  /* get its index */
    TValue stv;
    if (novariant(luaH_getint(S->h, l_castU2S(idx), &stv)) != LUA_TSTRING)
      error(S, "invalid string index");
    *sl = ts = tsvalue(&stv);  /* get its value */
    luaC_objbarrier(L, p, ts);
    return;  /* do not save it again */
  }
  else if ((size -= 2) <= LUAI_MAXSHORTLEN) {  /* short string? */
    char buff[LUAI_MAXSHORTLEN + 1];  /* extra space for '\0' */
    loadVector(S, buff, size + 1);  /* load string into buffer */
    *sl = ts = luaS_newlstr(L, buff, size);  /* create string */
    luaC_objbarrier(L, p, ts);
  }
  else if (S->fixed) {  /* for a fixed buffer, use a fixed string */
    const char *s = getaddr(S, size + 1, char);  /* get content address */
    *sl = ts = luaS_newextlstr(L, s, size, NULL, NULL);
    luaC_objbarrier(L, p, ts);
  }
  else {  /* create internal copy */
    *sl = ts = luaS_createlngstrobj(L, size);  /* create string */
    luaC_objbarrier(L, p, ts);
    loadVector(S, getlngstr(ts), size + 1);  /* load directly in final place */
  }
  /* add string to list of saved strings */
  S->nstr++;
  setsvalue(L, &sv, ts);
  luaH_setint(L, S->h, l_castU2S(S->nstr), &sv);
  luaC_objbarrierback(L, obj2gco(S->h), ts);
}


static void loadCode (LoadState *S, Proto *f) {
  int n = loadInt(S);
  loadAlign(S, sizeof(f->getCode()[0]));
  if (S->fixed) {
    f->setCode(getaddr(S, n, Instruction));
    f->setCodeSize(n);
  }
  else {
    f->setCode(luaM_newvectorchecked(S->L, n, Instruction));
    f->setCodeSize(n);
    loadVector(S, f->getCode(), n);
  }
}


static void loadFunction(LoadState *S, Proto *f);


static void loadConstants (LoadState *S, Proto *f) {
  int i;
  int n = loadInt(S);
  f->setConstants(luaM_newvectorchecked(S->L, n, TValue));
  f->setConstantsSize(n);
  std::for_each_n(f->getConstants(), n, [](TValue& v) {
    setnilvalue(&v);
  });
  for (i = 0; i < n; i++) {
    TValue *o = &f->getConstants()[i];
    int t = loadByte(S);
    switch (t) {
      case LUA_VNIL:
        setnilvalue(o);
        break;
      case LUA_VFALSE:
        setbfvalue(o);
        break;
      case LUA_VTRUE:
        setbtvalue(o);
        break;
      case LUA_VNUMFLT:
        setfltvalue(o, loadNumber(S));
        break;
      case LUA_VNUMINT:
        setivalue(o, loadInteger(S));
        break;
      case LUA_VSHRSTR:
      case LUA_VLNGSTR: {
        lua_assert(f->getSource() == NULL);
        loadString(S, f, f->getSourcePtr());  /* use 'source' to anchor string */
        if (f->getSource() == NULL)
          error(S, "bad format for constant string");
        setsvalue2n(S->L, o, f->getSource());  /* save it in the right place */
        f->setSource(NULL);
        break;
      }
      default: error(S, "invalid constant");
    }
  }
}


static void loadProtos (LoadState *S, Proto *f) {
  int i;
  int n = loadInt(S);
  f->setProtos(luaM_newvectorchecked(S->L, n, Proto *));
  f->setProtosSize(n);
  std::fill_n(f->getProtos(), n, nullptr);
  for (i = 0; i < n; i++) {
    f->getProtos()[i] = luaF_newproto(S->L);
    luaC_objbarrier(S->L, f, f->getProtos()[i]);
    loadFunction(S, f->getProtos()[i]);
  }
}


/*
** Load the upvalues for a function. The names must be filled first,
** because the filling of the other fields can raise read errors and
** the creation of the error message can call an emergency collection;
** in that case all prototypes must be consistent for the GC.
*/
static void loadUpvalues (LoadState *S, Proto *f) {
  int i;
  int n = loadInt(S);
  f->setUpvalues(luaM_newvectorchecked(S->L, n, Upvaldesc));
  f->setUpvaluesSize(n);
  /* make array valid for GC */
  std::for_each_n(f->getUpvalues(), n, [](Upvaldesc& uv) {
    uv.setName(nullptr);
  });
  for (i = 0; i < n; i++) {  /* following calls can raise errors */
    f->getUpvalues()[i].setInStack(loadByte(S));
    f->getUpvalues()[i].setIndex(loadByte(S));
    f->getUpvalues()[i].setKind(loadByte(S));
  }
}


static void loadDebug (LoadState *S, Proto *f) {
  int i;
  int n = loadInt(S);
  if (S->fixed) {
    f->setLineInfo(getaddr(S, n, ls_byte));
    f->setLineInfoSize(n);
  }
  else {
    f->setLineInfo(luaM_newvectorchecked(S->L, n, ls_byte));
    f->setLineInfoSize(n);
    loadVector(S, f->getLineInfo(), n);
  }
  n = loadInt(S);
  if (n > 0) {
    loadAlign(S, sizeof(int));
    if (S->fixed) {
      f->setAbsLineInfo(getaddr(S, n, AbsLineInfo));
      f->setAbsLineInfoSize(n);
    }
    else {
      f->setAbsLineInfo(luaM_newvectorchecked(S->L, n, AbsLineInfo));
      f->setAbsLineInfoSize(n);
      loadVector(S, f->getAbsLineInfo(), n);
    }
  }
  n = loadInt(S);
  f->setLocVars(luaM_newvectorchecked(S->L, n, LocVar));
  f->setLocVarsSize(n);
  std::for_each_n(f->getLocVars(), n, [](LocVar& lv) {
    lv.setVarName(nullptr);
  });
  for (i = 0; i < n; i++) {
    loadString(S, f, f->getLocVars()[i].getVarNamePtr());
    f->getLocVars()[i].setStartPC(loadInt(S));
    f->getLocVars()[i].setEndPC(loadInt(S));
  }
  n = loadInt(S);
  if (n != 0)  /* does it have debug information? */
    n = f->getUpvaluesSize();  /* must be this many */
  for (i = 0; i < n; i++)
    loadString(S, f, f->getUpvalues()[i].getNamePtr());
}


static void loadFunction (LoadState *S, Proto *f) {
  f->setLineDefined(loadInt(S));
  f->setLastLineDefined(loadInt(S));
  f->setNumParams(loadByte(S));
  f->setFlag(loadByte(S) & PF_ISVARARG);  /* get only the meaningful flags */
  if (S->fixed)
    f->setFlag(f->getFlag() | PF_FIXED);  /* signal that code is fixed */
  f->setMaxStackSize(loadByte(S));
  loadCode(S, f);
  loadConstants(S, f);
  loadUpvalues(S, f);
  loadProtos(S, f);
  loadString(S, f, f->getSourcePtr());
  loadDebug(S, f);
}


static void checkliteral (LoadState *S, const char *s, const char *msg) {
  char buff[sizeof(LUA_SIGNATURE) + sizeof(LUAC_DATA)]; /* larger than both */
  size_t len = strlen(s);
  loadVector(S, buff, len);
  if (memcmp(s, buff, len) != 0)
    error(S, msg);
}


static l_noret numerror (LoadState *S, const char *what, const char *tname) {
  const char *msg = luaO_pushfstring(S->L, "%s %s mismatch", tname, what);
  error(S, msg);
}


static void checknumsize (LoadState *S, int size, const char *tname) {
  if (size != loadByte(S))
    numerror(S, "size", tname);
}


static void checknumformat (LoadState *S, int eq, const char *tname) {
  if (!eq)
    numerror(S, "format", tname);
}


#define checknum(S,tvar,value,tname)  \
  { tvar i; checknumsize(S, sizeof(i), tname); \
    loadVar(S, i); \
    checknumformat(S, i == value, tname); }


static void checkHeader (LoadState *S) {
  /* skip 1st char (already read and checked) */
  checkliteral(S, &LUA_SIGNATURE[1], "not a binary chunk");
  if (loadByte(S) != LUAC_VERSION)
    error(S, "version mismatch");
  if (loadByte(S) != LUAC_FORMAT)
    error(S, "format mismatch");
  checkliteral(S, LUAC_DATA, "corrupted chunk");
  checknum(S, int, LUAC_INT, "int");
  checknum(S, Instruction, LUAC_INST, "instruction");
  checknum(S, lua_Integer, LUAC_INT, "Lua integer");
  checknum(S, lua_Number, LUAC_NUM, "Lua number");
}


/*
** Load precompiled chunk.
*/
LClosure *luaU_undump (lua_State *L, ZIO *Z, const char *name, int fixed) {
  LoadState S;
  LClosure *cl;
  if (*name == '@' || *name == '=')
    name = name + 1;
  else if (*name == LUA_SIGNATURE[0])
    name = "binary string";
  S.name = name;
  S.L = L;
  S.Z = Z;
  S.fixed = cast_byte(fixed);
  S.offset = 1;  /* fist byte was already read */
  checkHeader(&S);
  cl = LClosure::create(L, loadByte(&S));
  setclLvalue2s(L, L->getTop().p, cl);
  L->inctop();  /* Phase 25e */
  S.h = luaH_new(L);  /* create list of saved strings */
  S.nstr = 0;
  sethvalue2s(L, L->getTop().p, S.h);  /* anchor it */
  L->inctop();  /* Phase 25e */
  cl->setProto(luaF_newproto(L));
  luaC_objbarrier(L, cl, cl->getProto());
  loadFunction(&S, cl->getProto());
  if (cl->getNumUpvalues() != cl->getProto()->getUpvaluesSize())
    error(&S, "corrupted chunk");
  luai_verifycode(L, cl->getProto());
  L->getTop().p--;  /* pop table */
  return cl;
}

