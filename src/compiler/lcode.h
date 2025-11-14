/*
** $Id: lcode.h $
** Code generator for Lua
** See Copyright Notice in lua.h
*/

#ifndef lcode_h
#define lcode_h

#include "llex.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"


/*
** Marks the end of a patch list. It is an invalid value both as an absolute
** address, and as a list link (would link an element to itself).
*/
inline constexpr int NO_JUMP = -1;


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


/* true if operation is foldable (that is, it is arithmetic or bitwise) */
inline constexpr bool foldbinop(BinOpr op) noexcept {
	return op <= OPR_SHR;
}


typedef enum UnOpr { OPR_MINUS, OPR_BNOT, OPR_NOT, OPR_LEN, OPR_NOUNOPR } UnOpr;


/* get (pointer to) instruction of given 'expdesc' */
inline Instruction& getinstruction(FuncState* fs, expdesc* e) noexcept {
	return fs->getProto()->getCode()[e->getInfo()];
}

LUAI_FUNC l_noret luaK_semerror (LexState *ls, const char *fmt, ...);

/* Inline function definitions (after forward declarations) */

inline int luaK_codeABC(FuncState* fs, OpCode o, int a, int b, int c) noexcept {
	return fs->codeABCk(o, a, b, c, 0);
}

inline void luaK_setmultret(FuncState* fs, expdesc* e) noexcept {
	fs->setreturns(e, LUA_MULTRET);
}

inline void luaK_jumpto(FuncState* fs, int t) noexcept {
	fs->patchlist(fs->jump(), t);
}


#endif
