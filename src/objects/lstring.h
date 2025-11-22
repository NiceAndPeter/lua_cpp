/*
** $Id: lstring.h $
** String table (keep all strings handled by Lua)
** See Copyright Notice in lua.h
*/

#ifndef lstring_h
#define lstring_h

#include <span>

#include "lobject_core.h"  /* GCBase, TValue */

/* Forward declarations */
class lua_State;
class global_State;

/*
** Memory-allocation error message must be preallocated (it cannot
** be created after memory is exhausted)
*/
#define MEMERRMSG       "not enough memory"


/*
** Maximum length for short strings, that is, strings that are
** internalized. (Cannot be smaller than reserved words or tags for
** metamethods, as these strings must be internalized;
** #("function") = 8, #("__newindex") = 10.)
*/
#if !defined(LUAI_MAXSHORTLEN)
#define LUAI_MAXSHORTLEN	40
#endif


/*
** {==================================================================
** Strings
** ===================================================================
*/

/* Variant tags for strings */
inline constexpr int LUA_VSHRSTR = makevariant(LUA_TSTRING, 0);  /* short strings */
inline constexpr int LUA_VLNGSTR = makevariant(LUA_TSTRING, 1);  /* long strings */

constexpr bool ttisstring(const TValue* o) noexcept { return checktype(o, LUA_TSTRING); }
constexpr bool ttisshrstring(const TValue* o) noexcept { return checktag(o, ctb(LUA_VSHRSTR)); }
constexpr bool ttislngstring(const TValue* o) noexcept { return checktag(o, ctb(LUA_VLNGSTR)); }

constexpr bool TValue::isString() const noexcept { return checktype(this, LUA_TSTRING); }
constexpr bool TValue::isShortString() const noexcept { return checktag(this, ctb(LUA_VSHRSTR)); }
constexpr bool TValue::isLongString() const noexcept { return checktag(this, ctb(LUA_VLNGSTR)); }

inline TString* tsvalue(const TValue* o) noexcept { return o->stringValue(); }


/* Kinds of long strings (stored in 'shrlen') */
inline constexpr int LSTRREG = -1;  /* regular long string */
inline constexpr int LSTRFIX = -2;  /* fixed external long string */
inline constexpr int LSTRMEM = -3;  /* external long string with deallocation */


/*
** Header for a string value.
*/
// TString inherits from GCBase (CRTP)
class TString : public GCBase<TString> {
private:
  lu_byte extra;  /* reserved words for short strings; "has hash" for longs */
  ls_byte shrlen;  /* length for short strings, negative for long strings */
  unsigned int hash;
  union {
    size_t lnglen;  /* length for long strings */
    TString *hnext;  /* linked list for hash table */
  } u;
  char *contents;  /* pointer to content in long strings */
  lua_Alloc falloc;  /* deallocation function for external strings */
  void *ud;  /* user data for external strings */

public:
  // Phase 50: Constructor - initializes only fields common to both short and long strings
  // For short strings: only fields up to 'u' exist (contents/falloc/ud are overlay for string data)
  // For long strings: all fields exist
  TString() noexcept {
    extra = 0;
    shrlen = 0;
    hash = 0;
    u.lnglen = 0;  // Zero-initialize union
    // Note: contents, falloc, ud are NOT initialized here!
    // They will be initialized by the caller only for long strings.
  }

  // Phase 50: Destructor - trivial (GC handles deallocation)
  // MUST be empty (not = default) because for short strings, not all fields exist in memory!
  ~TString() noexcept {}

  // Phase 50: Special placement new for variable-size objects
  // This is used when we need exact size control (for short strings)
  static void* operator new(size_t /*size*/, void* ptr) noexcept {
    return ptr;  // Just return the pointer, no allocation
  }

  // Phase 50: Placement new operator - integrates with Lua's GC (implemented in lgc.h)
  // Note: For TString, this may allocate less than sizeof(TString) for short strings!
  static void* operator new(size_t size, lua_State* L, lu_byte tt, size_t extra = 0);

  // Disable regular new/delete (must use placement new with GC)
  static void* operator new(size_t) = delete;
  static void operator delete(void*) = delete;

  // Type checks
  bool isShort() const noexcept { return shrlen >= 0; }
  bool isLong() const noexcept { return shrlen < 0; }
  bool isExternal() const noexcept { return isLong() && shrlen != LSTRREG; }

  // Accessors
  size_t length() const noexcept {
    return isShort() ? static_cast<size_t>(shrlen) : u.lnglen;
  }
  ls_byte getShrlen() const noexcept { return shrlen; }
  size_t getLnglen() const noexcept { return u.lnglen; }
  unsigned int getHash() const noexcept { return hash; }
  lu_byte getExtra() const noexcept { return extra; }
  const char* c_str() const noexcept {
    return isShort() ? getContentsAddr() : contents;
  }
  char* getContentsPtr() noexcept { return isShort() ? getContentsAddr() : contents; }
  char* getContentsField() noexcept { return contents; }
  const char* getContentsField() const noexcept { return contents; }
  // For short strings: return address where inline string data starts (after 'u' union)
  // For long strings: would return same address (where contents pointer is stored)
  char* getContentsAddr() noexcept { return cast_charp(this) + contentsOffset(); }
  const char* getContentsAddr() const noexcept { return cast_charp(this) + contentsOffset(); }
  lua_Alloc getFalloc() const noexcept { return falloc; }
  void* getUserData() const noexcept { return ud; }

  // Setters
  void setExtra(lu_byte e) noexcept { extra = e; }
  void setShrlen(ls_byte len) noexcept { shrlen = len; }
  void setHash(unsigned int h) noexcept { hash = h; }
  void setLnglen(size_t len) noexcept { u.lnglen = len; }
  void setContents(char* c) noexcept { contents = c; }
  void setFalloc(lua_Alloc f) noexcept { falloc = f; }
  void setUserData(void* data) noexcept { ud = data; }

  // Hash table operations
  TString* getNext() const noexcept { return u.hnext; }
  void setNext(TString* next_str) noexcept { u.hnext = next_str; }

  // Helper for offset calculations
  static constexpr size_t fallocOffset() noexcept {
    // Offset of falloc field accounting for alignment
    // Must include GCObject base!
    struct OffsetHelper {
      GCObject base;
      lu_byte extra;
      ls_byte shrlen;
      unsigned int hash;
      union { size_t lnglen; TString* hnext; } u;
      char* contents;
    };
    return sizeof(OffsetHelper);
  }

  static constexpr size_t contentsOffset() noexcept {
    // Offset of contents field accounting for GCObject base and alignment
    struct OffsetHelper {
      GCObject base;
      lu_byte extra;
      ls_byte shrlen;
      unsigned int hash;
      union { size_t lnglen; TString* hnext; } u;
    };
    return sizeof(OffsetHelper);
  }

  // Instance methods (implemented in lstring.cpp)
  unsigned hashLongStr();
  bool equals(const TString* other) const;
  void remove(lua_State* L);           // Phase 25a: from luaS_remove
  TString* normalize(lua_State* L);    // Phase 25a: from luaS_normstr

  // Phase 122: Static helpers and factory methods (from luaS_*)
  static unsigned computeHash(const char* str, size_t l, unsigned seed);
  static unsigned computeHash(std::span<const char> str, unsigned seed);
  static size_t calculateLongStringSize(size_t len, int kind);
  static TString* create(lua_State* L, const char* str, size_t l);
  static TString* create(lua_State* L, std::span<const char> str);
  static TString* create(lua_State* L, const char* str);  // null-terminated
  static TString* createLongString(lua_State* L, size_t l);
  static TString* createExternal(lua_State* L, const char* s, size_t len,
                                  lua_Alloc falloc, void* ud);

  // Phase 122: Global string table management
  static void init(lua_State* L);
  static void resize(lua_State* L, unsigned int newsize);
  static void clearCache(global_State* g);

  // Comparison operator overloads (defined after l_strcmp declaration)
  friend bool operator<(const TString& l, const TString& r) noexcept;
  friend bool operator<=(const TString& l, const TString& r) noexcept;
  friend bool operator==(const TString& l, const TString& r) noexcept;
  friend bool operator!=(const TString& l, const TString& r) noexcept;
};


/* Check if string is short (wrapper for backward compatibility) */
inline bool strisshr(const TString* ts) noexcept { return ts->isShort(); }

/* Check if string is external (fixed or with custom deallocator) */
inline bool isextstr(const TValue* v) noexcept {
	return ttislngstring(v) && tsvalue(v)->isExternal();
}

inline bool TValue::isExtString() const noexcept {
	return isLongString() && stringValue()->isExternal();
}

/*
** Get the actual string (array of bytes) from a 'TString'. (Generic
** version and specialized versions for long and short strings.)
*/
inline char* rawgetshrstr(TString* ts) noexcept {
	return ts->getContentsAddr();
}
inline const char* rawgetshrstr(const TString* ts) noexcept {
	return ts->getContentsAddr();
}

/*
** String accessor functions (Phase 46: converted from macros to inline functions)
** These provide type-safe access to string contents with assertions.
*/

/* Get short string contents (asserts string is short) */
inline char* getshrstr(TString* ts) noexcept {
	lua_assert(ts->isShort());
	return ts->getContentsAddr();
}
inline const char* getshrstr(const TString* ts) noexcept {
	lua_assert(ts->isShort());
	return ts->getContentsAddr();
}

/* Get long string contents (asserts string is long) */
inline char* getlngstr(TString* ts) noexcept {
	lua_assert(ts->isLong());
	return ts->getContentsField();
}
inline const char* getlngstr(const TString* ts) noexcept {
	lua_assert(ts->isLong());
	return ts->getContentsField();
}

/* Get string contents (works for both short and long strings) */
inline char* getstr(TString* ts) noexcept {
	return ts->getContentsPtr();
}
inline const char* getstr(const TString* ts) noexcept {
	return ts->c_str();
}


/* get string length from 'TString *ts' */
inline size_t tsslen(const TString* ts) noexcept {
	return ts->length();
}

/*
** Get string and length */
inline const char* getlstr(const TString* ts, size_t& len) noexcept {
	len = ts->length();
	return ts->c_str();
}

/* }================================================================== */


/*
** Size of a short TString: Size of the header plus space for the string
** itself (including final '\0').
*/
// Phase 29: Size of short string including the struct itself and string data
inline constexpr size_t sizestrshr(size_t l) noexcept {
	return TString::contentsOffset() + ((l) + 1) * sizeof(char);
}


/* Create a new string from a string literal, computing length at compile time */
template<size_t N>
inline TString* luaS_newliteral(lua_State *L, const char (&s)[N]) {
    return TString::create(L, s, N - 1);
}


/*
** test whether a string is a reserved word
*/
inline bool isreserved(const TString* s) noexcept {
	return strisshr(s) && (s)->getExtra() > 0;
}


/*
** equality for short strings, which are always internalized
*/
inline bool eqshrstr(const TString* a, const TString* b) noexcept {
	return check_exp((a)->getType() == LUA_VSHRSTR, (a) == (b));
}


// Phase 122: Non-TString functions
LUAI_FUNC Udata *luaS_newudata (lua_State *L, size_t s, unsigned short nuvalue);

/* Phase 26: Removed luaS_remove - now TString::remove() method */
/* Phase 26: Removed luaS_normstr - now TString::normalize() method */
/* Phase 122: Removed luaS_hash, luaS_newlstr, luaS_new, luaS_createlngstrobj,
              luaS_newextlstr, luaS_sizelngstr, luaS_hashlongstr, luaS_eqstr,
              luaS_init, luaS_resize, luaS_clearcache - now TString:: methods */

#endif
