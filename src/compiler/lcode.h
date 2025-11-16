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


/* true if operation is foldable (that is, it is arithmetic or bitwise) */
inline constexpr bool foldbinop(BinOpr op) noexcept {
	return op <= OPR_SHR;
}


/* get (pointer to) instruction of given 'expdesc' */
inline Instruction& getinstruction(FuncState* fs, expdesc* e) noexcept {
	return fs->getProto()->getCode()[e->getInfo()];
}


#endif
