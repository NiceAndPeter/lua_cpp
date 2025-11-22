/*
** $Id: ltable.h $
** Lua tables (hash)
** See Copyright Notice in lua.h
*/

#ifndef ltable_h
#define ltable_h

#include <span>

#include "lobject_core.h"  /* GCBase, TValue, GCObject */

/* Forward declarations */
class lua_State;
class TString;
typedef union StackValue *StkId;


/*
** {==================================================================
** Tables
** ===================================================================
*/

inline constexpr int LUA_VTABLE = makevariant(LUA_TTABLE, 0);

constexpr bool ttistable(const TValue* o) noexcept { return checktag(o, ctb(LUA_VTABLE)); }

constexpr bool TValue::isTable() const noexcept { return checktag(this, ctb(LUA_VTABLE)); }

inline Table* hvalue(const TValue* o) noexcept { return o->tableValue(); }


/*
** Nodes for Hash tables: A pack of two TValue's (key-value pairs)
** plus a 'next' field to link colliding entries. The distribution
** of the key's fields ('key_tt' and 'key_val') not forming a proper
** 'TValue' allows for a smaller size for 'Node' both in 4-byte
** and 8-byte alignments.
** Phase 44.2: Converted from union to class with proper encapsulation
*/
class Node {
private:
  union {
    struct {
      Value value_;  /* value */
      lu_byte tt_;   /* value type tag */
      lu_byte key_tt;  /* key type */
      int next;  /* for chaining */
      Value key_val;  /* key value */
    } u;
    TValue i_val;  /* direct access to node's value as a proper 'TValue' */
  };

public:
  // Default constructor
  constexpr Node() noexcept : u{{0}, LUA_VNIL, LUA_TNIL, 0, {0}} {}

  // Constructor for initializing with explicit values
  constexpr Node(Value val, lu_byte val_tt, lu_byte key_tt, int next_val, Value key_val) noexcept
    : u{val, val_tt, key_tt, next_val, key_val} {}

  // Copy assignment operator (needed because union contains TValue with user-declared operator=)
  Node& operator=(const Node& other) noexcept {
    u = other.u;  // Copy the union
    return *this;
  }

  // Value access
  TValue* getValue() noexcept { return &i_val; }
  const TValue* getValue() const noexcept { return &i_val; }

  // Next chain access
  int& getNext() noexcept { return u.next; }
  int getNext() const noexcept { return u.next; }
  void setNext(int n) noexcept { u.next = n; }

  // Key type access
  lu_byte getKeyType() const noexcept { return u.key_tt; }
  void setKeyType(lu_byte tt) noexcept { u.key_tt = tt; }

  // Key value access
  const Value& getKeyValue() const noexcept { return u.key_val; }
  Value& getKeyValue() noexcept { return u.key_val; }
  void setKeyValue(const Value& v) noexcept { u.key_val = v; }

  // Key type checks
  bool isKeyNil() const noexcept {
    return u.key_tt == LUA_TNIL;
  }

  bool isKeyInteger() const noexcept {
    return u.key_tt == LUA_VNUMINT;
  }

  bool isKeyShrStr() const noexcept {
    return u.key_tt == ctb(LUA_VSHRSTR);
  }

  bool isKeyDead() const noexcept {
    return u.key_tt == LUA_TDEADKEY;
  }

  bool isKeyCollectable() const noexcept {
    return (u.key_tt & BIT_ISCOLLECTABLE) != 0;
  }

  // Key value getters (typed)
  lua_Integer getKeyIntValue() const noexcept {
    return u.key_val.i;
  }

  TString* getKeyStrValue() const noexcept {
    return reinterpret_cast<TString*>(u.key_val.gc);
  }

  GCObject* getKeyGC() const noexcept {
    return u.key_val.gc;
  }

  GCObject* getKeyGCOrNull() const noexcept {
    return isKeyCollectable() ? u.key_val.gc : nullptr;
  }

  // Key setters
  void setKeyNil() noexcept {
    u.key_tt = LUA_TNIL;
  }

  void setKeyDead() noexcept {
    u.key_tt = LUA_TDEADKEY;
  }

  // Copy TValue to key
  void setKey(const TValue* obj) noexcept {
    u.key_val = obj->getValue();
    u.key_tt = obj->getType();
  }

  // Copy key to TValue
  void getKey(lua_State* L, TValue* obj) const noexcept {
    obj->valueField() = u.key_val;
    obj->setType(u.key_tt);
    (void)L; // checkliveness removed to avoid forward declaration issues
  }
};


/* Phase 44.2: setnodekey and getnodekey macros replaced with Node::setKey() and Node::getKey() methods */


// Table inherits from GCBase (CRTP)
class Table : public GCBase<Table> {
private:
  mutable lu_byte flags;  /* 1<<p means tagmethod(p) is not present (mutable for metamethod caching) */
  lu_byte lsizenode;  /* log2 of number of slots of 'node' array */
  unsigned int asize;  /* number of slots in 'array' array */
  Value *array;  /* array part */
  Node *node;
  Table *metatable;
  GCObject *gclist;

public:
  // Phase 50: Constructor - initializes all fields to safe defaults
  Table() noexcept {
    flags = 0;
    lsizenode = 0;
    asize = 0;
    array = nullptr;
    node = nullptr;
    metatable = nullptr;
    gclist = nullptr;
  }

  // Phase 50: Destructor - trivial (GC handles deallocation)
  ~Table() noexcept = default;

  // Phase 50: Placement new operator - integrates with Lua's GC (implemented in lgc.h)
  static void* operator new(size_t size, lua_State* L, lu_byte tt);

  // Disable regular new/delete (must use placement new with GC)
  static void* operator new(size_t) = delete;
  static void operator delete(void*) = delete;

  // Inline accessors
  lu_byte getFlags() const noexcept { return flags; }
  void setFlags(lu_byte f) noexcept { flags = f; }

  // Flags field bit manipulation methods (const - flags is mutable)
  void setFlagBits(int mask) const noexcept { flags |= cast_byte(mask); }
  void clearFlagBits(int mask) const noexcept { flags &= cast_byte(~mask); }

  // Flags field reference accessor (for backward compatibility)
  lu_byte& getFlagsRef() noexcept { return flags; }

  lu_byte getLsizenode() const noexcept { return lsizenode; }
  void setLsizenode(lu_byte ls) noexcept { lsizenode = ls; }

  unsigned int arraySize() const noexcept { return asize; }
  void setArraySize(unsigned int sz) noexcept { asize = sz; }

  Value* getArray() noexcept { return array; }
  const Value* getArray() const noexcept { return array; }
  void setArray(Value* arr) noexcept { array = arr; }

  // Phase 115.3: std::span accessors for array part
  std::span<Value> getArraySpan() noexcept {
    return std::span(array, asize);
  }
  std::span<const Value> getArraySpan() const noexcept {
    return std::span(array, asize);
  }

  Node* getNodeArray() noexcept { return node; }
  const Node* getNodeArray() const noexcept { return node; }
  void setNodeArray(Node* n) noexcept { node = n; }

  unsigned int nodeSize() const noexcept { return (1u << lsizenode); }
  Table* getMetatable() const noexcept { return metatable; }
  void setMetatable(Table* mt) noexcept { metatable = mt; }

  GCObject* getGclist() noexcept { return gclist; }
  void setGclist(GCObject* gc) noexcept { gclist = gc; }
  // For GC gray list traversal - allows efficient list manipulation
  GCObject** getGclistPtr() noexcept { return &gclist; }

  // Flag operations (inline for performance)
  // Note: BITDUMMY = (1 << 6), defined in ltable.h
  bool isDummy() const noexcept { return (flags & (1 << 6)) != 0; }
  void setDummy() noexcept { flags |= (1 << 6); }
  void setNoDummy() noexcept { flags &= cast_byte(~(1 << 6)); }
  // invalidateTMCache uses maskflags from ltm.h, so can't inline here - use macro instead

  // Phase 44.1: Additional table helper methods
  unsigned int allocatedNodeSize() const noexcept {
    return isDummy() ? 0 : nodeSize();
  }

  unsigned int* getLenHint() noexcept {
    return static_cast<unsigned int*>(static_cast<void*>(array));
  }

  const unsigned int* getLenHint() const noexcept {
    return static_cast<const unsigned int*>(static_cast<const void*>(array));
  }

  lu_byte* getArrayTag(lua_Unsigned k) noexcept {
    return static_cast<lu_byte*>(static_cast<void*>(array)) + sizeof(unsigned) + k;
  }

  const lu_byte* getArrayTag(lua_Unsigned k) const noexcept {
    return static_cast<const lu_byte*>(static_cast<const void*>(array)) + sizeof(unsigned) + k;
  }

  Value* getArrayVal(lua_Unsigned k) noexcept {
    return array - 1 - k;
  }

  const Value* getArrayVal(lua_Unsigned k) const noexcept {
    return array - 1 - k;
  }

  static unsigned int powerOfTwo(unsigned int x) noexcept {
    return (1u << x);
  }

  // Node accessors (Phase 19: Table macro reduction)
  Node* getNode(unsigned int i) noexcept { return &node[i]; }
  const Node* getNode(unsigned int i) const noexcept { return &node[i]; }

  // Method declarations (implemented in ltable.cpp)
  lu_byte get(const TValue* key, TValue* res);
  lu_byte getInt(lua_Integer key, TValue* res);
  lu_byte getShortStr(TString* key, TValue* res);
  lu_byte getStr(TString* key, TValue* res);
  TValue* HgetShortStr(TString* key);

  int pset(const TValue* key, TValue* val);
  int psetInt(lua_Integer key, TValue* val);
  int psetShortStr(TString* key, TValue* val);
  int psetStr(TString* key, TValue* val);

  void set(lua_State* L, const TValue* key, TValue* value);
  void setInt(lua_State* L, lua_Integer key, TValue* value);
  void finishSet(lua_State* L, const TValue* key, TValue* value, int hres);

  void resize(lua_State* L, unsigned nasize, unsigned nhsize);
  void resizeArray(lua_State* L, unsigned nasize);
  lu_mem size() const;
  int tableNext(lua_State* L, StkId key);  // renamed from next() to avoid conflict with GC field
  lua_Unsigned getn(lua_State* L);

  // Phase 33: Factory and helper methods
  static Table* create(lua_State* L);  // Factory method (replaces luaH_new)
  void destroy(lua_State* L);  // Explicit destructor (replaces luaH_free)
  Node* mainPosition(const TValue* key) const;  // replaces luaH_mainposition
};


/*
** Phase 44.2: Node key macros replaced with Node class methods:
** - keytt(node) → node->getKeyType()
** - keyval(node) → node->getKeyValue()
** - keyisnil(node) → node->isKeyNil()
** - keyisinteger(node) → node->isKeyInteger()
** - keyival(node) → node->getKeyIntValue()
** - keyisshrstr(node) → node->isKeyShrStr()
** - keystrval(node) → node->getKeyStrValue()
** - setnilkey(node) → node->setKeyNil()
** - keyiscollectable(n) → n->isKeyCollectable()
** - gckey(n) → n->getKeyGC()
** - gckeyN(n) → n->getKeyGCOrNull()
** - setdeadkey(node) → node->setKeyDead()
** - keyisdead(node) → node->isKeyDead()
*/

/* }================================================================== */


// Phase 19: Table accessor macros converted to inline functions
// Note: Returns Node* even for const Table* to support Lua's read/write lookup semantics
inline Node* gnode(Table* t, unsigned int i) noexcept { return t->getNode(i); }
inline Node* gnode(const Table* t, unsigned int i) noexcept {
  return const_cast<Table*>(t)->getNode(i);  /* Lookup functions need mutable access */
}
inline TValue* gval(Node* n) noexcept { return n->getValue(); }
inline const TValue* gval(const Node* n) noexcept { return n->getValue(); }
// gnext returns reference to allow modification
inline int& gnext(Node* n) noexcept { return n->getNext(); }
inline int gnext(const Node* n) noexcept { return n->getNext(); }


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



// Phase 88: luaH_fastgeti and luaH_fastseti converted to inline functions
// Definitions moved after farr2val and fval2arr are defined (see below)


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
/* Const overload for metamethod lookup (casts away const for lookup semantics) */
inline const TValue *luaH_Hgetshortstr (const Table *t, TString *key) {
  return luaH_Hgetshortstr(const_cast<Table*>(t), key);
}

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
LUAI_FUNC lu_mem luaH_size (const Table *t);
LUAI_FUNC void luaH_free (lua_State *L, Table *t);
LUAI_FUNC int luaH_next (lua_State *L, Table *t, StkId key);
LUAI_FUNC lua_Unsigned luaH_getn (lua_State *L, Table *t);

// Phase 88: Convert luaH_fastgeti and luaH_fastseti macros to inline functions
// These are hot-path table access functions used throughout the VM
// Phase 121: Moved definitions to lobject.h (after ltm.h include) to avoid circular dependencies
// Declarations only here
inline void luaH_fastgeti(Table* t, lua_Integer k, TValue* res, lu_byte& tag) noexcept;
inline void luaH_fastseti(Table* t, lua_Integer k, TValue* val, int& hres) noexcept;


#if defined(LUA_DEBUG)
LUAI_FUNC Node *luaH_mainposition (const Table *t, const TValue *key);
#endif


#endif
