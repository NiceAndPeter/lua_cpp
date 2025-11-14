/*
** $Id: lzio.h $
** Buffered streams
** See Copyright Notice in lua.h
*/


#ifndef lzio_h
#define lzio_h

#include "lua.h"

#include "lmem.h"


#define EOZ	(-1)			/* end of stream */

typedef struct Zio ZIO;

/* Forward declaration for inline function */
LUAI_FUNC int luaZ_fill (ZIO *z);


typedef struct Mbuffer {
  char *buffer;
  size_t n;
  size_t buffsize;
} Mbuffer;

inline void luaZ_initbuffer([[maybe_unused]] lua_State *L, Mbuffer *buff) {
    buff->buffer = NULL;
    buff->buffsize = 0;
}

inline char* luaZ_buffer(Mbuffer *buff) {
    return buff->buffer;
}

inline size_t luaZ_sizebuffer(Mbuffer *buff) {
    return buff->buffsize;
}

inline size_t luaZ_bufflen(Mbuffer *buff) {
    return buff->n;
}

inline void luaZ_buffremove(Mbuffer *buff, int i) {
    buff->n -= cast_sizet(i);
}

inline void luaZ_resetbuffer(Mbuffer *buff) {
    buff->n = 0;
}


inline void luaZ_resizebuffer(lua_State *L, Mbuffer *buff, size_t size) {
    buff->buffer = luaM_reallocvchar(L, buff->buffer, buff->buffsize, size);
    buff->buffsize = size;
}

inline void luaZ_freebuffer(lua_State *L, Mbuffer *buff) {
    luaZ_resizebuffer(L, buff, 0);
}


LUAI_FUNC void luaZ_init (lua_State *L, ZIO *z, lua_Reader reader,
                                        void *data);
LUAI_FUNC size_t luaZ_read (ZIO* z, void *b, size_t n);	/* read next n bytes */

LUAI_FUNC const void *luaZ_getaddr (ZIO* z, size_t n);


/* --------- Private Part ------------------ */

struct Zio {
  size_t n;			/* bytes still unread */
  const char *p;		/* current position in buffer */
  lua_Reader reader;		/* reader function */
  void *data;			/* additional data */
  lua_State *L;			/* Lua state (for reader) */
};

inline int zgetc(ZIO *z) {
    return ((z->n--) > 0) ? cast_uchar(*(z->p++)) : luaZ_fill(z);
}

#endif
