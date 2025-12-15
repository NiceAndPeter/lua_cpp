/*
** $Id: ldump.c $
** save precompiled Lua chunks
** See Copyright Notice in lua.h
*/

#define ldump_c
#define LUA_CORE

#include "lprefix.h"


#include <climits>
#include <cstddef>
#include <span>

#include "lua.h"

#include "lapi.h"
#include "lgc.h"
#include "lobject.h"
#include "lstate.h"
#include "ltable.h"
#include "lundump.h"


class DumpState {
private:
  lua_State *L;
  lua_Writer writer;
  void *data;
  size_t offset;  /* current position relative to beginning of dump */
  int strip;
  int status;
  Table *h;  /* table to track saved strings */
  lua_Unsigned nstr;  /* counter for counting saved strings */

  // Private methods (formerly static functions)
  void dumpBlock(const void *b, size_t size);
  void dumpAlign(unsigned align);
  void dumpByte(int y);
  void dumpVarint(lua_Unsigned x);
  void dumpSize(size_t sz);
  void dumpInt(int x);
  void dumpNumber(lua_Number x);
  void dumpInteger(lua_Integer x);
  void dumpString(TString *ts);
  void dumpCode(const Proto& f);
  void dumpFunction(const Proto& f);
  void dumpConstants(const Proto& f);
  void dumpProtos(const Proto& f);
  void dumpUpvalues(const Proto& f);
  void dumpDebug(const Proto& f);
  void dumpHeader();

  // Friend template functions for dumping
  template<typename T> friend void dumpVector(DumpState* D, const T* v, size_t n);
  template<typename T> friend void dumpVar(DumpState* D, const T& x);

public:
  // Public interface for dumping
  static int dump(lua_State *L, const Proto *f, lua_Writer w, void *data, int strip);
};


/*
** All high-level dumps go through dumpVector; you can change it to
** change the endianness of the result
*/
template<typename T>
inline void dumpVector(DumpState* D, const T* v, size_t n) {
	D->dumpBlock(v, n * sizeof(T));
}

#define dumpLiteral(D, s)	(D)->dumpBlock(s,sizeof(s) - sizeof(char))


/*
** Dump the block of memory pointed by 'b' with given 'size'.
** 'b' should not be nullptr, except for the last call signaling the end
** of the dump.
*/
void DumpState::dumpBlock(const void *b, size_t size) {
  if (status == 0) {  /* do not write anything after an error */
    lua_unlock(L);
    status = (*writer)(L, b, size, data);
    lua_lock(L);
    offset += size;
  }
}


/*
** Dump enough zeros to ensure that current position is a multiple of
** 'align'.
*/
void DumpState::dumpAlign(unsigned align) {
  unsigned padding = align - cast_uint(offset % align);
  if (padding < align) {  /* padding == align means no padding */
    static lua_Integer paddingContent = 0;
    lua_assert(align <= sizeof(lua_Integer));
    dumpBlock(&paddingContent, padding);
  }
  lua_assert(offset % align == 0);
}


template<typename T>
inline void dumpVar(DumpState* D, const T& x) {
	dumpVector(D, &x, 1);
}


void DumpState::dumpByte(int y) {
  lu_byte x = (lu_byte)y;
  dumpVar(this, x);
}


/*
** size for 'dumpVarint' buffer: each byte can store up to 7 bits.
** (The "+6" rounds up the division.)
*/
inline constexpr int DIBS = (l_numbits<lua_Unsigned>() + 6) / 7;

/*
** Dumps an unsigned integer using the MSB Varint encoding
*/
void DumpState::dumpVarint(lua_Unsigned x) {
  lu_byte buff[DIBS];
  unsigned n = 1;
  buff[DIBS - 1] = x & 0x7f;  /* fill least-significant byte */
  while ((x >>= 7) != 0)  /* fill other bytes in reverse order */
    buff[DIBS - (++n)] = cast_byte((x & 0x7f) | 0x80);
  dumpVector(this, buff + DIBS - n, n);
}


void DumpState::dumpSize(size_t sz) {
  dumpVarint(static_cast<lua_Unsigned>(sz));
}


void DumpState::dumpInt(int x) {
  lua_assert(x >= 0);
  dumpVarint(cast_uint(x));
}


void DumpState::dumpNumber(lua_Number x) {
  dumpVar(this, x);
}


/*
** Signed integers are coded to keep small values small. (Coding -1 as
** 0xfff...fff would use too many bytes to save a quite common value.)
** A non-negative x is coded as 2x; a negative x is coded as -2x - 1.
** (0 => 0; -1 => 1; 1 => 2; -2 => 3; 2 => 4; ...)
*/
void DumpState::dumpInteger(lua_Integer x) {
  lua_Unsigned cx = (x >= 0) ? 2u * l_castS2U(x)
                             : (2u * ~l_castS2U(x)) + 1;
  dumpVarint(cx);
}


/*
** Dump a String. First dump its "size": size==0 means nullptr;
** size==1 is followed by an index and means "reuse saved string with
** that index"; size>=2 is followed by the string contents with real
** size==size-2 and means that string, which will be saved with
** the next available index.
*/
void DumpState::dumpString(TString *ts) {
  if (ts == nullptr)
    dumpSize(0);
  else {
    TValue idx;
    LuaT tag = h->getStr(ts, &idx);
    if (!tagisempty(tag)) {  /* string already saved? */
      dumpVarint(1);  /* reuse a saved string */
      dumpVarint(l_castS2U(ivalue(&idx)));  /* index of saved string */
    }
    else {  /* must write and save the string */
      TValue key, value;  /* to save the string in the hash */
      size_t size;
      const char *s = getStringWithLength(ts, size);
      dumpSize(size + 2);
      dumpVector(this, s, size + 1);  /* include ending '\0' */
      nstr++;  /* one more saved string */
      setsvalue(L, &key, ts);  /* the string is the key */
      value.setInt(l_castU2S(nstr));  /* its index is the value */
      h->set(L, &key, &value);  /* h[ts] = nstr */
      /* integer value does not need barrier */
    }
  }
}


void DumpState::dumpCode(const Proto& f) {
  auto code = f.getCodeSpan();
  dumpInt(static_cast<int>(code.size()));
  dumpAlign(sizeof(code[0]));
  lua_assert(code.data() != nullptr);
  dumpVector(this, code.data(), cast_uint(code.size()));
}


void DumpState::dumpConstants(const Proto& f) {
  auto constants = f.getConstantsSpan();
  dumpInt(static_cast<int>(constants.size()));
  for (const auto& constant : constants) {
    LuaT tt = ttypetag(&constant);
    dumpByte(static_cast<lu_byte>(tt));
    switch (tt) {
      case LuaT::NUMFLT:
        dumpNumber(fltvalue(&constant));
        break;
      case LuaT::NUMINT:
        dumpInteger(ivalue(&constant));
        break;
      case LuaT::SHRSTR:
      case LuaT::LNGSTR:
        dumpString(tsvalue(&constant));
        break;
      default:
        lua_assert(tt == LuaT::NIL || tt == LuaT::VFALSE || tt == LuaT::VTRUE);
    }
  }
}


void DumpState::dumpProtos(const Proto& f) {
  auto protos = f.getProtosSpan();
  dumpInt(static_cast<int>(protos.size()));
  for (Proto* proto : protos) {
    dumpFunction(*proto);
  }
}


void DumpState::dumpUpvalues(const Proto& f) {
  auto upvalues = f.getUpvaluesSpan();
  dumpInt(static_cast<int>(upvalues.size()));
  for (const auto& uv : upvalues) {
    dumpByte(uv.getInStackRaw());
    dumpByte(uv.getIndex());
    dumpByte(uv.getKind());
  }
}


void DumpState::dumpDebug(const Proto& f) {
  int n;
  auto lineinfo = f.getDebugInfo().getLineInfoSpan();
  n = (strip) ? 0 : static_cast<int>(lineinfo.size());
  dumpInt(n);
  if (lineinfo.data() != nullptr)
    dumpVector(this, lineinfo.data(), cast_uint(n));
  auto abslineinfo = f.getDebugInfo().getAbsLineInfoSpan();
  n = (strip) ? 0 : static_cast<int>(abslineinfo.size());
  dumpInt(n);
  if (n > 0) {
    /* 'abslineinfo' is an array of structures of int's */
    dumpAlign(sizeof(int));
    dumpVector(this, abslineinfo.data(), cast_uint(n));
  }
  auto locvars = f.getDebugInfo().getLocVarsSpan();
  n = (strip) ? 0 : static_cast<int>(locvars.size());
  dumpInt(n);
  for (const auto& lv : locvars.subspan(0, static_cast<size_t>(n))) {
    dumpString(lv.getVarName());
    dumpInt(lv.getStartPC());
    dumpInt(lv.getEndPC());
  }
  auto upvalues = f.getUpvaluesSpan();
  n = (strip) ? 0 : static_cast<int>(upvalues.size());
  dumpInt(n);
  for (const auto& uv : upvalues.subspan(0, static_cast<size_t>(n))) {
    dumpString(uv.getName());
  }
}


void DumpState::dumpFunction(const Proto& f) {
  dumpInt(f.getLineDefined());
  dumpInt(f.getLastLineDefined());
  dumpByte(f.getNumParams());
  dumpByte(f.getFlag());
  dumpByte(f.getMaxStackSize());
  dumpCode(f);
  dumpConstants(f);
  dumpUpvalues(f);
  dumpProtos(f);
  dumpString(strip ? nullptr : f.getSource());
  dumpDebug(f);
}


#define dumpNumInfo(tvar, value)  \
  { tvar i = value; dumpByte(sizeof(tvar)); dumpVar(this, i); }


void DumpState::dumpHeader() {
  dumpLiteral(this, LUA_SIGNATURE);
  dumpByte(LUAC_VERSION);
  dumpByte(LUAC_FORMAT);
  dumpLiteral(this, LUAC_DATA);
  dumpNumInfo(int, LUAC_INT);
  dumpNumInfo(Instruction, LUAC_INST);
  dumpNumInfo(lua_Integer, LUAC_INT);
  dumpNumInfo(lua_Number, LUAC_NUM);
}


/*
** dump Lua function as precompiled chunk
*/
int DumpState::dump(lua_State *L, const Proto *f, lua_Writer w, void *data, int strip) {
  DumpState D;
  D.h = Table::create(L);  /* aux. table to keep strings already dumped */
  sethvalue2s(L, L->getTop().p, D.h);  /* anchor it */
  L->getStackSubsystem().push();
  D.L = L;
  D.writer = w;
  D.offset = 0;
  D.data = data;
  D.strip = strip;
  D.status = 0;
  D.nstr = 0;
  D.dumpHeader();
  D.dumpByte(f->getUpvaluesSize());
  D.dumpFunction(*f);
  D.dumpBlock(nullptr, 0);  /* signal end of dump */
  return D.status;
}

// C API wrapper
int luaU_dump(lua_State *L, const Proto *f, lua_Writer w, void *data, int strip) {
  return DumpState::dump(L, f, w, data, strip);
}

