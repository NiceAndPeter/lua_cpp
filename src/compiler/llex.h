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
