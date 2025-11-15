/*
** $Id: llex.h $
** Lexical Analyzer
** See Copyright Notice in lua.h
*/

#ifndef llex_h
#define llex_h

#include <limits.h>

#include "lobject.h"
#include "lzio.h"

/* Forward declarations */
struct expdesc;

/*
** Single-char tokens (terminal symbols) are represented by their own
** numeric code. Other tokens start at the following value.
*/
#define FIRST_RESERVED	(UCHAR_MAX + 1)


#if !defined(LUA_ENV)
#define LUA_ENV		"_ENV"
#endif


/*
* WARNING: if you change the order of this enumeration,
* grep "ORDER RESERVED"
*/
enum RESERVED {
  /* terminal symbols denoted by reserved words */
  TK_AND = FIRST_RESERVED, TK_BREAK,
  TK_DO, TK_ELSE, TK_ELSEIF, TK_END, TK_FALSE, TK_FOR, TK_FUNCTION,
  TK_GLOBAL, TK_GOTO, TK_IF, TK_IN, TK_LOCAL, TK_NIL, TK_NOT, TK_OR,
  TK_REPEAT, TK_RETURN, TK_THEN, TK_TRUE, TK_UNTIL, TK_WHILE,
  /* other terminal symbols */
  TK_IDIV, TK_CONCAT, TK_DOTS, TK_EQ, TK_GE, TK_LE, TK_NE,
  TK_SHL, TK_SHR,
  TK_DBCOLON, TK_EOS,
  TK_FLT, TK_INT, TK_NAME, TK_STRING
};

/* number of reserved words */
#define NUM_RESERVED	(cast_int(TK_WHILE-FIRST_RESERVED + 1))


typedef union {
  lua_Number r;
  lua_Integer i;
  TString *ts;
} SemInfo;  /* semantics information */


typedef struct Token {
  int token;
  SemInfo seminfo;
} Token;


/* state of the scanner plus state of the parser when shared by all
   functions */
class LexState {
private:
  int current;  /* current character (charint) */
  int linenumber;  /* input line counter */
  int lastline;  /* line of last token 'consumed' */
  Token t;  /* current token */
  Token lookahead;  /* look ahead token */
  struct FuncState *fs;  /* current function (parser) */
  struct lua_State *L;
  ZIO *z;  /* input stream */
  Mbuffer *buff;  /* buffer for tokens */
  Table *h;  /* to avoid collection/reuse strings */
  struct Dyndata *dyd;  /* dynamic structures used by the parser */
  TString *source;  /* current source name */
  TString *envn;  /* environment variable name */
  TString *brkn;  /* "break" name (used as a label) */
  TString *glbn;  /* "global" name (when not a reserved word) */

public:
  // Inline read accessors
  int getCurrentChar() const noexcept { return current; }
  int getLineNumber() const noexcept { return linenumber; }
  int getLastLine() const noexcept { return lastline; }
  const Token& getCurrentToken() const noexcept { return t; }
  Token& getCurrentTokenRef() noexcept { return t; }
  const Token& getLookahead() const noexcept { return lookahead; }
  Token& getLookaheadRef() noexcept { return lookahead; }
  struct FuncState* getFuncState() const noexcept { return fs; }
  struct lua_State* getLuaState() const noexcept { return L; }
  ZIO* getZIO() const noexcept { return z; }
  Mbuffer* getBuffer() const noexcept { return buff; }
  Table* getTable() const noexcept { return h; }
  struct Dyndata* getDyndata() const noexcept { return dyd; }
  TString* getSource() const noexcept { return source; }
  TString* getEnvName() const noexcept { return envn; }
  TString* getBreakName() const noexcept { return brkn; }
  TString* getGlobalName() const noexcept { return glbn; }

  // Inline write accessors
  void setCurrent(int c) noexcept { current = c; }
  void setLineNumber(int line) noexcept { linenumber = line; }
  void setLastLine(int line) noexcept { lastline = line; }
  void setFuncState(struct FuncState* f) noexcept { fs = f; }
  void setLuaState(struct lua_State* state) noexcept { L = state; }
  void setZIO(ZIO* zio) noexcept { z = zio; }
  void setBuffer(Mbuffer* b) noexcept { buff = b; }
  void setTable(Table* table) noexcept { h = table; }
  void setDyndata(struct Dyndata* d) noexcept { dyd = d; }
  void setSource(TString* src) noexcept { source = src; }
  void setEnvName(TString* env) noexcept { envn = env; }
  void setBreakName(TString* brk) noexcept { brkn = brk; }
  void setGlobalName(TString* gbl) noexcept { glbn = gbl; }

  // Reference accessors for compound operations
  int& getLineNumberRef() noexcept { return linenumber; }

  // Inline helper methods (converted from macros)
  void next() noexcept { current = zgetc(z); }
  bool currIsNewline() const noexcept { return current == '\n' || current == '\r'; }

  // Method declarations (implemented in llex.cpp)
  void saveAndNext();
  void setInput(lua_State *state, ZIO *zio, TString *src, int firstchar);
  TString *newString(const char *str, size_t l);
  void nextToken();
  int lookaheadToken();
  l_noret syntaxError(const char *s);
  l_noret semerror(const char *fmt, ...);
  const char *tokenToStr(int token);

  // Parser utilities (implemented in lparser.cpp)
  l_noret error_expected(int token);
  int testnext(int c);
  void check(int c);
  void checknext(int c);
  void check_match(int what, int who, int where);
  TString *str_checkname();
  // Phase 83: Variable utilities
  void codename(expdesc *e);
  int new_varkind(TString *name, lu_byte kind);
  int new_localvar(TString *name);
  // Phase 84: Variable checking and scope
  void check_readonly(expdesc *e);
  void adjustlocalvars(int nvars);
};


LUAI_FUNC void luaX_init (lua_State *L);


#endif
