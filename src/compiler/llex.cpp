/*
** $Id: llex.c $
** Lexical Analyzer
** See Copyright Notice in lua.h
*/

#define llex_c
#define LUA_CORE

#include "lprefix.h"


#include <locale.h>
#include <string.h>

#include "lua.h"

#include "lctype.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "llex.h"
#include "lobject.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lzio.h"



/* minimum size for string buffer */
#if !defined(LUA_MINBUFFER)
#define LUA_MINBUFFER   32
#endif


/* ORDER RESERVED */
static const char *const luaX_tokens [] = {
    "and", "break", "do", "else", "elseif",
    "end", "false", "for", "function", "global", "goto", "if",
    "in", "local", "nil", "not", "or", "repeat",
    "return", "then", "true", "until", "while",
    "//", "..", "...", "==", ">=", "<=", "~=",
    "<<", ">>", "::", "<eof>",
    "<number>", "<integer>", "<name>", "<string>"
};




static l_noret lexerror (LexState *ls, const char *msg, int token);


static void save (LexState *ls, int c) {
  Mbuffer *b = ls->getBuffer();
  if (luaZ_bufflen(b) + 1 > luaZ_sizebuffer(b)) {
    size_t newsize = luaZ_sizebuffer(b);  /* get old size */;
    if (newsize >= (MAX_SIZE/3 * 2))  /* larger than MAX_SIZE/1.5 ? */
      lexerror(ls, "lexical element too long", 0);
    newsize += (newsize >> 1);  /* new size is 1.5 times the old one */
    luaZ_resizebuffer(ls->getLuaState(), b, newsize);
  }
  size_t len = luaZ_bufflen(b);
  b->buffer[len] = cast_char(c);
  b->n++;
}


void LexState::saveAndNext() {
  save(this, getCurrentChar());
  next();
}


void luaX_init (lua_State *L) {
  int i;
  TString *e = luaS_newliteral(L, LUA_ENV);  /* create env name */
  obj2gco(e)->fix(L);  /* Phase 25c: never collect this name */
  for (i=0; i<NUM_RESERVED; i++) {
    TString *ts = luaS_new(L, luaX_tokens[i]);
    obj2gco(ts)->fix(L);  /* Phase 25c: reserved words are never collected */
    ts->setExtra(cast_byte(i+1));  /* reserved word */
  }
}


const char *luaX_token2str (LexState *ls, int token) {
  if (token < FIRST_RESERVED) {  /* single-byte symbols? */
    if (lisprint(token))
      return luaO_pushfstring(ls->getLuaState(), "'%c'", token);
    else  /* control character */
      return luaO_pushfstring(ls->getLuaState(), "'<\\%d>'", token);
  }
  else {
    const char *s = luaX_tokens[token - FIRST_RESERVED];
    if (token < TK_EOS)  /* fixed format (symbols and reserved words)? */
      return luaO_pushfstring(ls->getLuaState(), "'%s'", s);
    else  /* names, strings, and numerals */
      return s;
  }
}


static const char *txtToken (LexState *ls, int token) {
  switch (token) {
    case TK_NAME: case TK_STRING:
    case TK_FLT: case TK_INT:
      save(ls, '\0');
      return luaO_pushfstring(ls->getLuaState(), "'%s'", luaZ_buffer(ls->getBuffer()));
    default:
      return luaX_token2str(ls, token);
  }
}


static l_noret lexerror (LexState *ls, const char *msg, int token) {
  msg = luaG_addinfo(ls->getLuaState(), msg, ls->getSource(), ls->getLineNumber());
  if (token)
    luaO_pushfstring(ls->getLuaState(), "%s near %s", msg, txtToken(ls, token));
  ls->getLuaState()->doThrow( LUA_ERRSYNTAX);
}


l_noret luaX_syntaxerror (LexState *ls, const char *msg) {
  lexerror(ls, msg, ls->getCurrentToken().token);
}


/*
** Anchors a string in scanner's table so that it will not be collected
** until the end of the compilation; by that time it should be anchored
** somewhere. It also internalizes long strings, ensuring there is only
** one copy of each unique string.
*/
static TString *anchorstr (LexState *ls, TString *ts) {
  lua_State *L = ls->getLuaState();
  TValue oldts;
  int tag = luaH_getstr(ls->getTable(), ts, &oldts);
  if (!tagisempty(tag))  /* string already present? */
    return tsvalue(&oldts);  /* use stored value */
  else {  /* create a new entry */
    TValue *stv = s2v(L->getTop().p++);  /* reserve stack space for string */
    setsvalue(L, stv, ts);  /* push (anchor) the string on the stack */
    luaH_set(L, ls->getTable(), stv, stv);  /* t[string] = string */
    /* table is not a metatable, so it does not need to invalidate cache */
    luaC_checkGC(L);
    L->getTop().p--;  /* remove string from stack */
    return ts;
  }
}


/*
** Creates a new string and anchors it in scanner's table.
*/
TString *luaX_newstring (LexState *ls, const char *str, size_t l) {
  return anchorstr(ls, luaS_newlstr(ls->getLuaState(), str, l));
}


/*
** increment line number and skips newline sequence (any of
** \n, \r, \n\r, or \r\n)
*/
static void inclinenumber (LexState *ls) {
  int old = ls->getCurrentChar();
  lua_assert(ls->currIsNewline());
  ls->next();  /* skip '\n' or '\r' */
  if (ls->currIsNewline() && ls->getCurrentChar() != old)
    ls->next();  /* skip '\n\r' or '\r\n' */
  if (++ls->getLineNumberRef() >= INT_MAX)
    lexerror(ls, "chunk has too many lines", 0);
}


void luaX_setinput (lua_State *L, LexState *ls, ZIO *z, TString *source,
                    int firstchar) {
  ls->getCurrentTokenRef().token = 0;
  ls->setLuaState(L);
  ls->setCurrent(firstchar);
  ls->getLookaheadRef().token = TK_EOS;  /* no look-ahead token */
  ls->setZIO(z);
  ls->setFuncState(NULL);
  ls->setLineNumber(1);
  ls->setLastLine(1);
  ls->setSource(source);
  /* all three strings here ("_ENV", "break", "global") were fixed,
     so they cannot be collected */
  ls->setEnvName(luaS_newliteral(L, LUA_ENV));  /* get env string */
  ls->setBreakName(luaS_newliteral(L, "break"));  /* get "break" string */
#if defined(LUA_COMPAT_GLOBAL)
  /* compatibility mode: "global" is not a reserved word */
  ls->setGlobalName(luaS_newliteral(L, "global"));  /* get "global" string */
  ls->getGlobalName()->setExtra(0);  /* mark it as not reserved */
#endif
  luaZ_resizebuffer(ls->getLuaState(), ls->getBuffer(), LUA_MINBUFFER);  /* initialize buffer */
}



/*
** =======================================================
** LEXICAL ANALYZER
** =======================================================
*/


static int check_next1 (LexState *ls, int c) {
  if (ls->getCurrentChar() == c) {
    ls->next();
    return 1;
  }
  else return 0;
}


/*
** Check whether current char is in set 'set' (with two chars) and
** saves it
*/
static int check_next2 (LexState *ls, const char *set) {
  lua_assert(set[2] == '\0');
  if (ls->getCurrentChar() == set[0] || ls->getCurrentChar() == set[1]) {
    ls->saveAndNext();
    return 1;
  }
  else return 0;
}


/* LUA_NUMBER */
/*
** This function is quite liberal in what it accepts, as 'luaO_str2num'
** will reject ill-formed numerals. Roughly, it accepts the following
** pattern:
**
**   %d(%x|%.|([Ee][+-]?))* | 0[Xx](%x|%.|([Pp][+-]?))*
**
** The only tricky part is to accept [+-] only after a valid exponent
** mark, to avoid reading '3-4' or '0xe+1' as a single number.
**
** The caller might have already read an initial dot.
*/
static int read_numeral (LexState *ls, SemInfo *seminfo) {
  TValue obj;
  const char *expo = "Ee";
  int first = ls->getCurrentChar();
  lua_assert(lisdigit(ls->getCurrentChar()));
  ls->saveAndNext();
  if (first == '0' && check_next2(ls, "xX"))  /* hexadecimal? */
    expo = "Pp";
  for (;;) {
    if (check_next2(ls, expo))  /* exponent mark? */
      check_next2(ls, "-+");  /* optional exponent sign */
    else if (lisxdigit(ls->getCurrentChar()) || ls->getCurrentChar() == '.')  /* '%x|%.' */
      ls->saveAndNext();
    else break;
  }
  if (lislalpha(ls->getCurrentChar()))  /* is numeral touching a letter? */
    ls->saveAndNext();  /* force an error */
  save(ls, '\0');
  if (luaO_str2num(luaZ_buffer(ls->getBuffer()), &obj) == 0)  /* format error? */
    lexerror(ls, "malformed number", TK_FLT);
  if (ttisinteger(&obj)) {
    seminfo->i = ivalue(&obj);
    return TK_INT;
  }
  else {
    lua_assert(ttisfloat(&obj));
    seminfo->r = fltvalue(&obj);
    return TK_FLT;
  }
}


/*
** read a sequence '[=*[' or ']=*]', leaving the last bracket. If
** sequence is well formed, return its number of '='s + 2; otherwise,
** return 1 if it is a single bracket (no '='s and no 2nd bracket);
** otherwise (an unfinished '[==...') return 0.
*/
static size_t skip_sep (LexState *ls) {
  size_t count = 0;
  int s = ls->getCurrentChar();
  lua_assert(s == '[' || s == ']');
  ls->saveAndNext();
  while (ls->getCurrentChar() == '=') {
    ls->saveAndNext();
    count++;
  }
  return (ls->getCurrentChar() == s) ? count + 2
         : (count == 0) ? 1
         : 0;
}


static void read_long_string (LexState *ls, SemInfo *seminfo, size_t sep) {
  int line = ls->getLineNumber();  /* initial line (for error message) */
  ls->saveAndNext();  /* skip 2nd '[' */
  if (ls->currIsNewline())  /* string starts with a newline? */
    inclinenumber(ls);  /* skip it */
  for (;;) {
    switch (ls->getCurrentChar()) {
      case EOZ: {  /* error */
        const char *what = (seminfo ? "string" : "comment");
        const char *msg = luaO_pushfstring(ls->getLuaState(),
                     "unfinished long %s (starting at line %d)", what, line);
        lexerror(ls, msg, TK_EOS);
        break;  /* to avoid warnings */
      }
      case ']': {
        if (skip_sep(ls) == sep) {
          ls->saveAndNext();  /* skip 2nd ']' */
          goto endloop;
        }
        break;
      }
      case '\n': case '\r': {
        save(ls, '\n');
        inclinenumber(ls);
        if (!seminfo) luaZ_resetbuffer(ls->getBuffer());  /* avoid wasting space */
        break;
      }
      default: {
        if (seminfo) ls->saveAndNext();
        else ls->next();
      }
    }
  } endloop:
  if (seminfo)
    seminfo->ts = luaX_newstring(ls, luaZ_buffer(ls->getBuffer()) + sep,
                                     luaZ_bufflen(ls->getBuffer()) - 2 * sep);
}


static void esccheck (LexState *ls, int c, const char *msg) {
  if (!c) {
    if (ls->getCurrentChar() != EOZ)
      ls->saveAndNext();  /* add current to buffer for error message */
    lexerror(ls, msg, TK_STRING);
  }
}


static int gethexa (LexState *ls) {
  ls->saveAndNext();
  esccheck (ls, lisxdigit(ls->getCurrentChar()), "hexadecimal digit expected");
  return luaO_hexavalue(ls->getCurrentChar());
}


static int readhexaesc (LexState *ls) {
  int r = gethexa(ls);
  r = (r << 4) + gethexa(ls);
  luaZ_buffremove(ls->getBuffer(), 2);  /* remove saved chars from buffer */
  return r;
}


/*
** When reading a UTF-8 escape sequence, save everything to the buffer
** for error reporting in case of errors; 'i' counts the number of
** saved characters, so that they can be removed if case of success.
*/
static l_uint32 readutf8esc (LexState *ls) {
  l_uint32 r;
  int i = 4;  /* number of chars to be removed: start with #"\u{X" */
  ls->saveAndNext();  /* skip 'u' */
  esccheck(ls, ls->getCurrentChar() == '{', "missing '{'");
  r = cast_uint(gethexa(ls));  /* must have at least one digit */
  while (cast_void(ls->saveAndNext()), lisxdigit(ls->getCurrentChar())) {
    i++;
    esccheck(ls, r <= (0x7FFFFFFFu >> 4), "UTF-8 value too large");
    r = (r << 4) + luaO_hexavalue(ls->getCurrentChar());
  }
  esccheck(ls, ls->getCurrentChar() == '}', "missing '}'");
  ls->next();  /* skip '}' */
  luaZ_buffremove(ls->getBuffer(), i);  /* remove saved chars from buffer */
  return r;
}


static void utf8esc (LexState *ls) {
  char buff[UTF8BUFFSZ];
  int n = luaO_utf8esc(buff, readutf8esc(ls));
  for (; n > 0; n--)  /* add 'buff' to string */
    save(ls, buff[UTF8BUFFSZ - n]);
}


static int readdecesc (LexState *ls) {
  int i;
  int r = 0;  /* result accumulator */
  for (i = 0; i < 3 && lisdigit(ls->getCurrentChar()); i++) {  /* read up to 3 digits */
    r = 10*r + ls->getCurrentChar() - '0';
    ls->saveAndNext();
  }
  esccheck(ls, r <= UCHAR_MAX, "decimal escape too large");
  luaZ_buffremove(ls->getBuffer(), i);  /* remove read digits from buffer */
  return r;
}


static void read_string (LexState *ls, int del, SemInfo *seminfo) {
  ls->saveAndNext();  /* keep delimiter (for error messages) */
  while (ls->getCurrentChar() != del) {
    switch (ls->getCurrentChar()) {
      case EOZ:
        lexerror(ls, "unfinished string", TK_EOS);
        break;  /* to avoid warnings */
      case '\n':
      case '\r':
        lexerror(ls, "unfinished string", TK_STRING);
        break;  /* to avoid warnings */
      case '\\': {  /* escape sequences */
        int c;  /* final character to be saved */
        ls->saveAndNext();  /* keep '\\' for error messages */
        switch (ls->getCurrentChar()) {
          case 'a': c = '\a'; goto read_save;
          case 'b': c = '\b'; goto read_save;
          case 'f': c = '\f'; goto read_save;
          case 'n': c = '\n'; goto read_save;
          case 'r': c = '\r'; goto read_save;
          case 't': c = '\t'; goto read_save;
          case 'v': c = '\v'; goto read_save;
          case 'x': c = readhexaesc(ls); goto read_save;
          case 'u': utf8esc(ls);  goto no_save;
          case '\n': case '\r':
            inclinenumber(ls); c = '\n'; goto only_save;
          case '\\': case '\"': case '\'':
            c = ls->getCurrentChar(); goto read_save;
          case EOZ: goto no_save;  /* will raise an error next loop */
          case 'z': {  /* zap following span of spaces */
            luaZ_buffremove(ls->getBuffer(), 1);  /* remove '\\' */
            ls->next();  /* skip the 'z' */
            while (lisspace(ls->getCurrentChar())) {
              if (ls->currIsNewline()) inclinenumber(ls);
              else ls->next();
            }
            goto no_save;
          }
          default: {
            esccheck(ls, lisdigit(ls->getCurrentChar()), "invalid escape sequence");
            c = readdecesc(ls);  /* digital escape '\ddd' */
            goto only_save;
          }
        }
       read_save:
         ls->next();
         /* go through */
       only_save:
         luaZ_buffremove(ls->getBuffer(), 1);  /* remove '\\' */
         save(ls, c);
         /* go through */
       no_save: break;
      }
      default:
        ls->saveAndNext();
    }
  }
  ls->saveAndNext();  /* skip delimiter */
  seminfo->ts = luaX_newstring(ls, luaZ_buffer(ls->getBuffer()) + 1,
                                   luaZ_bufflen(ls->getBuffer()) - 2);
}


static int llex (LexState *ls, SemInfo *seminfo) {
  luaZ_resetbuffer(ls->getBuffer());
  for (;;) {
    switch (ls->getCurrentChar()) {
      case '\n': case '\r': {  /* line breaks */
        inclinenumber(ls);
        break;
      }
      case ' ': case '\f': case '\t': case '\v': {  /* spaces */
        ls->next();
        break;
      }
      case '-': {  /* '-' or '--' (comment) */
        ls->next();
        if (ls->getCurrentChar() != '-') return '-';
        /* else is a comment */
        ls->next();
        if (ls->getCurrentChar() == '[') {  /* long comment? */
          size_t sep = skip_sep(ls);
          luaZ_resetbuffer(ls->getBuffer());  /* 'skip_sep' may dirty the buffer */
          if (sep >= 2) {
            read_long_string(ls, NULL, sep);  /* skip long comment */
            luaZ_resetbuffer(ls->getBuffer());  /* previous call may dirty the buff. */
            break;
          }
        }
        /* else short comment */
        while (!ls->currIsNewline() && ls->getCurrentChar() != EOZ)
          ls->next();  /* skip until end of line (or end of file) */
        break;
      }
      case '[': {  /* long string or simply '[' */
        size_t sep = skip_sep(ls);
        if (sep >= 2) {
          read_long_string(ls, seminfo, sep);
          return TK_STRING;
        }
        else if (sep == 0)  /* '[=...' missing second bracket? */
          lexerror(ls, "invalid long string delimiter", TK_STRING);
        return '[';
      }
      case '=': {
        ls->next();
        if (check_next1(ls, '=')) return TK_EQ;  /* '==' */
        else return '=';
      }
      case '<': {
        ls->next();
        if (check_next1(ls, '=')) return TK_LE;  /* '<=' */
        else if (check_next1(ls, '<')) return TK_SHL;  /* '<<' */
        else return '<';
      }
      case '>': {
        ls->next();
        if (check_next1(ls, '=')) return TK_GE;  /* '>=' */
        else if (check_next1(ls, '>')) return TK_SHR;  /* '>>' */
        else return '>';
      }
      case '/': {
        ls->next();
        if (check_next1(ls, '/')) return TK_IDIV;  /* '//' */
        else return '/';
      }
      case '~': {
        ls->next();
        if (check_next1(ls, '=')) return TK_NE;  /* '~=' */
        else return '~';
      }
      case ':': {
        ls->next();
        if (check_next1(ls, ':')) return TK_DBCOLON;  /* '::' */
        else return ':';
      }
      case '"': case '\'': {  /* short literal strings */
        read_string(ls, ls->getCurrentChar(), seminfo);
        return TK_STRING;
      }
      case '.': {  /* '.', '..', '...', or number */
        ls->saveAndNext();
        if (check_next1(ls, '.')) {
          if (check_next1(ls, '.'))
            return TK_DOTS;   /* '...' */
          else return TK_CONCAT;   /* '..' */
        }
        else if (!lisdigit(ls->getCurrentChar())) return '.';
        else return read_numeral(ls, seminfo);
      }
      case '0': case '1': case '2': case '3': case '4':
      case '5': case '6': case '7': case '8': case '9': {
        return read_numeral(ls, seminfo);
      }
      case EOZ: {
        return TK_EOS;
      }
      default: {
        if (lislalpha(ls->getCurrentChar())) {  /* identifier or reserved word? */
          TString *ts;
          do {
            ls->saveAndNext();
          } while (lislalnum(ls->getCurrentChar()));
          /* find or create string */
          ts = luaS_newlstr(ls->getLuaState(), luaZ_buffer(ls->getBuffer()),
                                   luaZ_bufflen(ls->getBuffer()));
          if (isreserved(ts))   /* reserved word? */
            return ts->getExtra() - 1 + FIRST_RESERVED;
          else {
            seminfo->ts = anchorstr(ls, ts);
            return TK_NAME;
          }
        }
        else {  /* single-char tokens ('+', '*', '%', '{', '}', ...) */
          int c = ls->getCurrentChar();
          ls->next();
          return c;
        }
      }
    }
  }
}


void luaX_next (LexState *ls) {
  ls->setLastLine(ls->getLineNumber());
  if (ls->getLookahead().token != TK_EOS) {  /* is there a look-ahead token? */
    ls->getCurrentTokenRef() = ls->getLookahead();  /* use this one */
    ls->getLookaheadRef().token = TK_EOS;  /* and discharge it */
  }
  else
    ls->getCurrentTokenRef().token = llex(ls, &ls->getCurrentTokenRef().seminfo);  /* read next token */
}


int luaX_lookahead (LexState *ls) {
  lua_assert(ls->getLookahead().token == TK_EOS);
  ls->getLookaheadRef().token = llex(ls, &ls->getLookaheadRef().seminfo);
  return ls->getLookahead().token;
}

