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
#include "../memory/lgc.h"


#if !defined(luai_verifycode)
#define luai_verifycode(L,f)  /* empty */
#endif


class LoadState {
private:
  lua_State *L;
  ZIO *Z;
  const char *name;
  Table *h;  /* list for string reuse */
  size_t offset;  /* current position relative to beginning of dump */
  lua_Unsigned nstr;  /* number of strings in the list */
  lu_byte fixed;  /* dump is fixed in memory */

  // Private methods (formerly static functions)
  [[noreturn]] void error(const char *why) {
    luaO_pushfstring(L, "%s: bad binary format (%s)", name, why);
    L->doThrow(LUA_ERRSYNTAX);
  }

  void loadBlock(void *b, size_t size);
  void loadAlign(unsigned align);
  const void *getaddr_(size_t size);
  lu_byte loadByte();
  lua_Unsigned loadVarint(lua_Unsigned limit);
  size_t loadSize();
  int loadInt();
  lua_Number loadNumber();
  lua_Integer loadInteger();
  void loadString(Proto& p, TString **sl);
  void loadCode(Proto& f);
  void loadFunction(Proto& f);
  void loadConstants(Proto& f);
  void loadProtos(Proto& f);
  void loadUpvalues(Proto& f);
  void loadDebug(Proto& f);
  void checkliteral(const char *s, const char *msg);
  [[noreturn]] void numerror(const char *what, const char *tname);
  void checknumsize(int size, const char *tname);
  void checknumformat(int eq, const char *tname);
  void checkHeader();

  // Friend template functions for loading
  template<typename T> friend void loadVector(LoadState* S, T* b, size_t n);
  template<typename T> friend T* getaddr(LoadState* S, size_t n);
  template<typename T> friend void loadVar(LoadState* S, T& x);

public:
  // Public interface for initialization
  static LClosure* undump(lua_State *L, ZIO *Z, const char *name, int fixed);
};


/*
** All high-level loads go through loadVector; you can change it to
** adapt to the endianness of the input
*/
template<typename T>
inline void loadVector(LoadState* S, T* b, size_t n) {
	S->loadBlock(b, cast_sizet(n) * sizeof(T));
}

void LoadState::loadBlock(void *b, size_t size) {
  if (luaZ_read(Z, b, size) != 0)
    error("truncated chunk");
  offset += size;
}


void LoadState::loadAlign(unsigned align) {
  unsigned padding = align - cast_uint(offset % align);
  if (padding < align) {  /* (padding == align) means no padding */
    lua_Integer paddingContent;
    loadBlock(&paddingContent, padding);
    lua_assert(offset % align == 0);
  }
}


const void *LoadState::getaddr_(size_t size) {
  const void *block = luaZ_getaddr(Z, size);
  offset += size;
  if (block == nullptr)
    error("truncated fixed buffer");
  return block;
}

/* Phase 126.2: Convert getaddr macro to template function
** Note: Returns non-const pointer for compatibility with existing code
** that stores pointers to fixed buffer data. The buffer is read-only but
** the type system doesn't enforce this for historical reasons.
*/
template<typename T>
inline T* getaddr(LoadState* S, size_t n) {
	return const_cast<T*>(cast(const T*, S->getaddr_(cast_sizet(n * sizeof(T)))));
}


template<typename T>
inline void loadVar(LoadState* S, T& x) {
	loadVector(S, &x, 1);
}


lu_byte LoadState::loadByte() {
  int b = zgetc(Z);
  if (b == EOZ)
    error("truncated chunk");
  offset++;
  return cast_byte(b);
}


lua_Unsigned LoadState::loadVarint(lua_Unsigned limit) {
  lua_Unsigned x = 0;
  int b;
  limit >>= 7;
  do {
    b = loadByte();
    if (x > limit)
      error("integer overflow");
    x = (x << 7) | (b & 0x7f);
  } while ((b & 0x80) != 0);
  return x;
}


size_t LoadState::loadSize() {
  return cast_sizet(loadVarint(MAX_SIZE));
}


int LoadState::loadInt() {
  return cast_int(loadVarint(cast_sizet(std::numeric_limits<int>::max())));
}



lua_Number LoadState::loadNumber() {
  lua_Number x;
  loadVar(this, x);
  return x;
}


lua_Integer LoadState::loadInteger() {
  lua_Unsigned cx = loadVarint(LUA_MAXUNSIGNED);
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
void LoadState::loadString(Proto& p, TString **sl) {
  TString *ts;
  TValue sv;
  size_t size = loadSize();
  if (size == 0) {  /* no string? */
    lua_assert(*sl == nullptr);  /* must be prefilled */
    return;
  }
  else if (size == 1) {  /* previously saved string? */
    lua_Unsigned idx = loadVarint(LUA_MAXUNSIGNED);  /* get its index */
    TValue stv;
    if (novariant(h->getInt(l_castU2S(idx), &stv)) != LUA_TSTRING)
      error("invalid string index");
    *sl = ts = tsvalue(&stv);  /* get its value */
    luaC_objbarrier(L, &p, ts);
    return;  /* do not save it again */
  }
  else if ((size -= 2) <= LUAI_MAXSHORTLEN) {  /* short string? */
    char buff[LUAI_MAXSHORTLEN + 1];  /* extra space for '\0' */
    loadVector(this, buff, size + 1);  /* load string into buffer */
    *sl = ts = TString::create(L, buff, size);  /* create string */
    luaC_objbarrier(L, &p, ts);
  }
  else if (fixed) {  /* for a fixed buffer, use a fixed string */
    const char *s = getaddr<char>(this, size + 1);  /* get content address */
    *sl = ts = TString::createExternal(L, s, size, nullptr, nullptr);
    luaC_objbarrier(L, &p, ts);
  }
  else {  /* create internal copy */
    *sl = ts = TString::createLongString(L, size);  /* create string */
    luaC_objbarrier(L, &p, ts);
    loadVector(this, getLongStringContents(ts), size + 1);  /* load directly in final place */
  }
  /* add string to list of saved strings */
  nstr++;
  setsvalue(L, &sv, ts);
  h->setInt(L, l_castU2S(nstr), &sv);
  luaC_objbarrierback(L, obj2gco(h), ts);
}


// Phase 115.2: Use span accessors
void LoadState::loadCode(Proto& f) {
  int n = loadInt();
  loadAlign(sizeof(Instruction));
  if (fixed) {
    f.setCode(getaddr<Instruction>(this, n));
    f.setCodeSize(n);
  }
  else {
    f.setCode(luaM_newvectorchecked<Instruction>(L, n));
    f.setCodeSize(n);
    auto codeSpan = f.getCodeSpan();  // Get span after allocation
    loadVector(this, codeSpan.data(), codeSpan.size());
  }
}


// Phase 115.2: Use span accessors
void LoadState::loadConstants(Proto& f) {
  int n = loadInt();
  f.setConstants(luaM_newvectorchecked<TValue>(L, n));
  f.setConstantsSize(n);
  auto constantsSpan = f.getConstantsSpan();
  for (TValue& v : constantsSpan) {
    setnilvalue(&v);
  }
  for (TValue& o_ref : constantsSpan) {
    TValue *o = &o_ref;
    LuaT t = static_cast<LuaT>(loadByte());
    switch (t) {
      case LuaT::NIL:
        setnilvalue(o);
        break;
      case LuaT::VFALSE:
        setbfvalue(o);
        break;
      case LuaT::VTRUE:
        setbtvalue(o);
        break;
      case LuaT::NUMFLT:
        o->setFloat(loadNumber());
        break;
      case LuaT::NUMINT:
        o->setInt(loadInteger());
        break;
      case LuaT::SHRSTR:
      case LuaT::LNGSTR: {
        lua_assert(f.getSource() == nullptr);
        loadString(f, f.getSourcePtr());  /* use 'source' to anchor string */
        if (f.getSource() == nullptr)
          error("bad format for constant string");
        setsvalue2n(L, o, f.getSource());  /* save it in the right place */
        f.setSource(nullptr);
        break;
      }
      default: error("invalid constant");
    }
  }
}


void LoadState::loadProtos(Proto& f) {
  int n = loadInt();
  f.setProtos(luaM_newvectorchecked<Proto*>(L, n));
  f.setProtosSize(n);
  std::fill_n(f.getProtos(), n, nullptr);
  for (int i = 0; i < n; i++) {
    f.getProtos()[i] = luaF_newproto(L);
    luaC_objbarrier(L, &f, f.getProtos()[i]);
    loadFunction(*f.getProtos()[i]);
  }
}


/*
** Load the upvalues for a function. The names must be filled first,
** because the filling of the other fields can raise read errors and
** the creation of the error message can call an emergency collection;
** in that case all prototypes must be consistent for the GC.
*/
// Phase 115.2: Use span accessors
void LoadState::loadUpvalues(Proto& f) {
  int n = loadInt();
  f.setUpvalues(luaM_newvectorchecked<Upvaldesc>(L, n));
  f.setUpvaluesSize(n);
  auto upvaluesSpan = f.getUpvaluesSpan();
  /* make array valid for GC */
  for (Upvaldesc& uv : upvaluesSpan) {
    uv.setName(nullptr);
  }
  for (Upvaldesc& uv : upvaluesSpan) {  /* following calls can raise errors */
    uv.setInStack(loadByte());
    uv.setIndex(loadByte());
    uv.setKind(loadByte());
  }
}


// Phase 115.2: Use span accessors
void LoadState::loadDebug(Proto& f) {
  int n = loadInt();
  if (fixed) {
    f.setLineInfo(getaddr<ls_byte>(this, n));
    f.setLineInfoSize(n);
  }
  else {
    f.setLineInfo(luaM_newvectorchecked<ls_byte>(L, n));
    f.setLineInfoSize(n);
    auto lineInfoSpan = f.getDebugInfo().getLineInfoSpan();
    loadVector(this, lineInfoSpan.data(), lineInfoSpan.size());
  }
  n = loadInt();
  if (n > 0) {
    loadAlign(sizeof(int));
    if (fixed) {
      f.setAbsLineInfo(getaddr<AbsLineInfo>(this, n));
      f.setAbsLineInfoSize(n);
    }
    else {
      f.setAbsLineInfo(luaM_newvectorchecked<AbsLineInfo>(L, n));
      f.setAbsLineInfoSize(n);
      auto absLineInfoSpan = f.getDebugInfo().getAbsLineInfoSpan();
      loadVector(this, absLineInfoSpan.data(), absLineInfoSpan.size());
    }
  }
  n = loadInt();
  f.setLocVars(luaM_newvectorchecked<LocVar>(L, n));
  f.setLocVarsSize(n);
  auto locVarsSpan = f.getDebugInfo().getLocVarsSpan();
  for (LocVar& lv : locVarsSpan) {
    lv.setVarName(nullptr);
  }
  for (LocVar& lv : locVarsSpan) {
    loadString(f, lv.getVarNamePtr());
    lv.setStartPC(loadInt());
    lv.setEndPC(loadInt());
  }
  n = loadInt();
  if (n != 0) {  /* does it have debug information? */
    n = f.getUpvaluesSize();  /* must be this many */
    auto upvaluesSpan = f.getUpvaluesSpan();
    for (Upvaldesc& uv : upvaluesSpan)
      loadString(f, uv.getNamePtr());
  }
}


void LoadState::loadFunction(Proto& f) {
  f.setLineDefined(loadInt());
  f.setLastLineDefined(loadInt());
  f.setNumParams(loadByte());
  f.setFlag(loadByte() & PF_ISVARARG);  /* get only the meaningful flags */
  if (fixed)
    f.setFlag(f.getFlag() | PF_FIXED);  /* signal that code is fixed */
  f.setMaxStackSize(loadByte());
  loadCode(f);
  loadConstants(f);
  loadUpvalues(f);
  loadProtos(f);
  loadString(f, f.getSourcePtr());
  loadDebug(f);
}


void LoadState::checkliteral(const char *s, const char *msg) {
  char buff[sizeof(LUA_SIGNATURE) + sizeof(LUAC_DATA)]; /* larger than both */
  size_t len = strlen(s);
  loadVector(this, buff, len);
  if (memcmp(s, buff, len) != 0)
    error(msg);
}


void LoadState::numerror(const char *what, const char *tname) {
  const char *msg = luaO_pushfstring(L, "%s %s mismatch", tname, what);
  error(msg);
}


void LoadState::checknumsize(int size, const char *tname) {
  if (size != loadByte())
    numerror("size", tname);
}


void LoadState::checknumformat(int eq, const char *tname) {
  if (!eq)
    numerror("format", tname);
}


#define checknum(tvar,value,tname)  \
  { tvar i; checknumsize(sizeof(i), tname); \
    loadVar(this, i); \
    checknumformat(i == value, tname); }


void LoadState::checkHeader() {
  /* skip 1st char (already read and checked) */
  checkliteral(&LUA_SIGNATURE[1], "not a binary chunk");
  if (loadByte() != LUAC_VERSION)
    error("version mismatch");
  if (loadByte() != LUAC_FORMAT)
    error("format mismatch");
  checkliteral(LUAC_DATA, "corrupted chunk");
  checknum(int, LUAC_INT, "int");
  checknum(Instruction, LUAC_INST, "instruction");
  checknum(lua_Integer, LUAC_INT, "Lua integer");
  checknum(lua_Number, LUAC_NUM, "Lua number");
}


/*
** Load precompiled chunk.
*/
LClosure *LoadState::undump(lua_State *L, ZIO *Z, const char *name, int fixed) {
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
  S.offset = 1;  /* first byte was already read */
  S.checkHeader();
  cl = LClosure::create(L, S.loadByte());
  setclLvalue2s(L, L->getTop().p, cl);
  L->inctop();  /* Phase 25e */
  S.h = Table::create(L);  /* create list of saved strings */
  S.nstr = 0;
  sethvalue2s(L, L->getTop().p, S.h);  /* anchor it */
  L->inctop();  /* Phase 25e */
  cl->setProto(luaF_newproto(L));
  luaC_objbarrier(L, cl, cl->getProto());
  S.loadFunction(*cl->getProto());
  if (cl->getNumUpvalues() != cl->getProto()->getUpvaluesSize())
    S.error("corrupted chunk");
  luai_verifycode(L, cl->getProto());
  L->getStackSubsystem().pop();  /* pop table */
  return cl;
}

// C API wrapper
LClosure *luaU_undump(lua_State *L, ZIO *Z, const char *name, int fixed) {
  return LoadState::undump(L, Z, name, fixed);
}

