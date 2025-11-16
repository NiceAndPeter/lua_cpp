/*
** $Id: llex.h $
** Lexical Analyzer
** See Copyright Notice in lua.h
*/

#ifndef llex_h
#define llex_h

#include <climits>

#include "lobject.h"
#include "lzio.h"

/* Forward declarations */
struct expdesc;
struct Labeldesc;
struct Labellist;
struct ConsControl;
struct LHS_assign;
struct BlockCnt;

/*
** grep "ORDER OPR" if you change these enums  (ORDER OP)
*/
typedef enum BinOpr {
  /* arithmetic operators */
  OPR_ADD, OPR_SUB, OPR_MUL, OPR_MOD, OPR_POW,
  OPR_DIV, OPR_IDIV,
  /* bitwise operators */
  OPR_BAND, OPR_BOR, OPR_BXOR,
  OPR_SHL, OPR_SHR,
  /* string operator */
  OPR_CONCAT,
  /* comparison operators */
  OPR_EQ, OPR_LT, OPR_LE,
  OPR_NE, OPR_GT, OPR_GE,
  /* logical operators */
  OPR_AND, OPR_OR,
  OPR_NOBINOPR
} BinOpr;

typedef enum UnOpr { OPR_MINUS, OPR_BNOT, OPR_NOT, OPR_LEN, OPR_NOUNOPR } UnOpr;

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
inline constexpr int NUM_RESERVED = (cast_int(TK_WHILE-FIRST_RESERVED + 1));


typedef union {
  lua_Number r;
  lua_Integer i;
  TString *ts;
} SemInfo;  /* semantics information */


typedef struct Token {
  int token;
  SemInfo seminfo;
} Token;


/* Phase 94: Subsystem for input character stream handling */
class InputScanner {
private:
  int current;      /* current character (charint) */
  int linenumber;   /* input line counter */
  int lastline;     /* line of last token 'consumed' */
  ZIO *z;           /* input stream */
  TString *source;  /* current source name */

public:
  // Accessors
  inline int getCurrent() const noexcept { return current; }
  inline int getLineNumber() const noexcept { return linenumber; }
  inline int getLastLine() const noexcept { return lastline; }
  inline ZIO* getZIO() const noexcept { return z; }
  inline TString* getSource() const noexcept { return source; }

  inline void setCurrent(int c) noexcept { current = c; }
  inline void setLineNumber(int line) noexcept { linenumber = line; }
  inline void setLastLine(int line) noexcept { lastline = line; }
  inline void setZIO(ZIO* zio) noexcept { z = zio; }
  inline void setSource(TString* src) noexcept { source = src; }

  inline int& getLineNumberRef() noexcept { return linenumber; }

  // Operations
  inline void next() noexcept { current = zgetc(z); }
  inline bool currIsNewline() const noexcept { return current == '\n' || current == '\r'; }
};

/* Phase 94: Subsystem for token state management */
class TokenState {
private:
  Token current;    /* current token */
  Token lookahead;  /* look ahead token */

public:
  // Accessors
  inline const Token& getCurrent() const noexcept { return current; }
  inline Token& getCurrentRef() noexcept { return current; }
  inline const Token& getLookahead() const noexcept { return lookahead; }
  inline Token& getLookaheadRef() noexcept { return lookahead; }
};

/* Phase 94: Subsystem for string interning and buffer management */
class StringInterner {
private:
  Mbuffer *buff;  /* buffer for tokens */
  Table *h;       /* to avoid collection/reuse strings */
  TString *envn;  /* environment variable name */
  TString *brkn;  /* "break" name (used as a label) */
  TString *glbn;  /* "global" name (when not a reserved word) */

public:
  // Accessors
  inline Mbuffer* getBuffer() const noexcept { return buff; }
  inline Table* getTable() const noexcept { return h; }
  inline TString* getEnvName() const noexcept { return envn; }
  inline TString* getBreakName() const noexcept { return brkn; }
  inline TString* getGlobalName() const noexcept { return glbn; }

  inline void setBuffer(Mbuffer* b) noexcept { buff = b; }
  inline void setTable(Table* table) noexcept { h = table; }
  inline void setEnvName(TString* env) noexcept { envn = env; }
  inline void setBreakName(TString* brk) noexcept { brkn = brk; }
  inline void setGlobalName(TString* gbl) noexcept { glbn = gbl; }
};

/* state of the scanner plus state of the parser when shared by all
   functions */
class LexState {
private:
  // Phase 94: SRP decomposition - organized subsystems
  InputScanner scanner;
  TokenState tokens;
  StringInterner strings;

  // Parser context (kept as-is)
  struct FuncState *fs;  /* current function (parser) */
  struct lua_State *L;
  struct Dyndata *dyd;  /* dynamic structures used by the parser */

public:
  // Phase 94: Accessors delegating to subsystems

  // InputScanner accessors
  int getCurrentChar() const noexcept { return scanner.getCurrent(); }
  int getLineNumber() const noexcept { return scanner.getLineNumber(); }
  int getLastLine() const noexcept { return scanner.getLastLine(); }
  ZIO* getZIO() const noexcept { return scanner.getZIO(); }
  TString* getSource() const noexcept { return scanner.getSource(); }

  void setCurrent(int c) noexcept { scanner.setCurrent(c); }
  void setLineNumber(int line) noexcept { scanner.setLineNumber(line); }
  void setLastLine(int line) noexcept { scanner.setLastLine(line); }
  void setZIO(ZIO* zio) noexcept { scanner.setZIO(zio); }
  void setSource(TString* src) noexcept { scanner.setSource(src); }

  int& getLineNumberRef() noexcept { return scanner.getLineNumberRef(); }
  void next() noexcept { scanner.next(); }
  bool currIsNewline() const noexcept { return scanner.currIsNewline(); }

  // TokenState accessors
  const Token& getCurrentToken() const noexcept { return tokens.getCurrent(); }
  Token& getCurrentTokenRef() noexcept { return tokens.getCurrentRef(); }
  const Token& getLookahead() const noexcept { return tokens.getLookahead(); }
  Token& getLookaheadRef() noexcept { return tokens.getLookaheadRef(); }

  // StringInterner accessors
  Mbuffer* getBuffer() const noexcept { return strings.getBuffer(); }
  Table* getTable() const noexcept { return strings.getTable(); }
  TString* getEnvName() const noexcept { return strings.getEnvName(); }
  TString* getBreakName() const noexcept { return strings.getBreakName(); }
  TString* getGlobalName() const noexcept { return strings.getGlobalName(); }

  void setBuffer(Mbuffer* b) noexcept { strings.setBuffer(b); }
  void setTable(Table* table) noexcept { strings.setTable(table); }
  void setEnvName(TString* env) noexcept { strings.setEnvName(env); }
  void setBreakName(TString* brk) noexcept { strings.setBreakName(brk); }
  void setGlobalName(TString* gbl) noexcept { strings.setGlobalName(gbl); }

  // Parser context accessors (kept as-is)
  struct FuncState* getFuncState() const noexcept { return fs; }
  struct lua_State* getLuaState() const noexcept { return L; }
  struct Dyndata* getDyndata() const noexcept { return dyd; }

  void setFuncState(struct FuncState* f) noexcept { fs = f; }
  void setLuaState(struct lua_State* state) noexcept { L = state; }
  void setDyndata(struct Dyndata* d) noexcept { dyd = d; }

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
  // Phase 86: Variable building and assignment
  void buildglobal(TString *varname, expdesc *var);
  void buildvar(TString *varname, expdesc *var);
  void singlevar(expdesc *var);
  void adjust_assign(int nvars, int nexps, expdesc *e);
  // Phase 87: Label and goto management
  l_noret jumpscopeerror(Labeldesc *gt);
  void closegoto(int g, Labeldesc *label, int bup);
  Labeldesc *findlabel(TString *name, int ilb);
  int newlabelentry(Labellist *l, TString *name, int line, int pc);
  int newgotoentry(TString *name, int line);
  void createlabel(TString *name, int line, int last);
  l_noret undefgoto(Labeldesc *gt);
  // Phase 88: Parser infrastructure (convert from static)
  Proto *addprototype();
  void mainfunc(FuncState *fs);

private:
  // Phase 93: Lexer helper methods (converted from static functions)
  // Batch 1: Trivial functions
  void save(int c);
  void incLineNumber();
  int checkNext1(int c);
  int checkNext2(const char *set);
  void escCheck(int c, const char *msg);

  // Batch 2: Simple functions
  int getHexa();
  int readHexaEsc();
  int readDecEsc();
  const char* txtToken(int token);
  size_t skipSep();
  TString* anchorStr(TString *ts);

  // Batch 3: Medium functions
  l_noret lexError(const char *msg, int token);
  l_uint32 readUtf8Esc();
  void utf8Esc();
  int readNumeral(SemInfo *seminfo);

  // Batch 4: Complex functions
  void readLongString(SemInfo *seminfo, size_t sep);
  void readString(int del, SemInfo *seminfo);
  int lex(SemInfo *seminfo);

  // Phase 88: Parser implementation methods (static → private)
  void statement();
  void expr(expdesc *v);
  int block_follow(int withuntil);
  void statlist();
  void fieldsel(expdesc *v);
  void yindex(expdesc *v);
  void recfield(ConsControl *cc);
  void listfield(ConsControl *cc);
  void field(ConsControl *cc);
  void constructor(expdesc *t);
  void parlist();
  void body(expdesc *e, int ismethod, int line);
  int explist(expdesc *v);
  void funcargs(expdesc *f);
  void primaryexp(expdesc *v);
  void suffixedexp(expdesc *v);
  void simpleexp(expdesc *v);
  BinOpr subexpr(expdesc *v, int limit);
  void block();
  void restassign(struct LHS_assign *lh, int nvars);
  int cond();
  void gotostat(int line);
  void breakstat(int line);
  void checkrepeated(TString *name);
  void labelstat(TString *name, int line);
  void whilestat(int line);
  void repeatstat(int line);
  void exp1();
  void forbody(int base, int line, int nvars, int isgen);
  void fornum(TString *varname, int line);
  void forlist(TString *indexname);
  void forstat(int line);
  void test_then_block(int *escapelist);
  void ifstat(int line);
  void localfunc();
  lu_byte getvarattribute(lu_byte df);
  void localstat();
  lu_byte getglobalattribute(lu_byte df);
  void globalnames(lu_byte defkind);
  void globalstat();
  void globalfunc(int line);
  void globalstatfunc(int line);
  int funcname(expdesc *v);
  void funcstat(int line);
  void exprstat();
  void retstat();
  void codeclosure(expdesc *v);
  void open_func(FuncState *fs, BlockCnt *bl);
  void close_func();
  void check_conflict(struct LHS_assign *lh, expdesc *v);
};


LUAI_FUNC void luaX_init (lua_State *L);


#endif
