# Developer's makefile for building Lua
# see luaconf.h for further customization

# == CHANGE THE SETTINGS BELOW TO SUIT YOUR ENVIRONMENT =======================

# Warnings valid for both C and C++
CWARNSCPP= \
	-Wfatal-errors \
	-Wextra \
	-Wshadow \
	-Wundef \
	-Wwrite-strings \
	-Wredundant-decls \
	-Wdisabled-optimization \
	-Wdouble-promotion \
	-Wmissing-declarations \
	-Wconversion \
	-Wstrict-overflow=2 \
        # the next warnings might be useful sometimes,
	# but usually they generate too much noise
	# -Werror \
	# -pedantic   # warns if we use jump tables \
	# -Wformat=2 \
	# -Wcast-qual \


# Warnings for gcc, not valid for clang
CWARNGCC= \
	-Wlogical-op \
	-Wno-aggressive-loop-optimizations \
	-Wno-invalid-offsetof \


# The next warnings are neither valid nor needed for C++
CWARNSC= -Wdeclaration-after-statement \
	-Wmissing-prototypes \
	-Wnested-externs \
	-Wstrict-prototypes \
	-Wc++-compat \
	-Wold-style-definition \


# For C++ compilation, use only C++-compatible warnings
CWARNS= $(CWARNSCPP) $(CWARNGCC)

# Some useful compiler options for internal tests:
# -DLUAI_ASSERT turns on all assertions inside Lua.
# -DHARDSTACKTESTS forces a reallocation of the stack at every point where
# the stack can be reallocated.
# -DHARDMEMTESTS forces a full collection at all points where the collector
# can run.
# -DEMERGENCYGCTESTS forces an emergency collection at every single allocation.
# -DEXTERNMEMCHECK removes internal consistency checking of blocks being
# deallocated (useful when an external tool like valgrind does the check).
# -DMAXINDEXRK=k limits range of constants in RK instruction operands.
# -DLUA_COMPAT_5_3

# -pg -malign-double
# -DLUA_USE_CTYPE -DLUA_USE_APICHECK

# The following options help detect "undefined behavior"s that seldom
# create problems; some are only available in newer gcc versions. To
# use some of them, we also have to define an environment variable
# ASAN_OPTIONS="detect_invalid_pointer_pairs=2".
# -fsanitize=undefined
# -fsanitize=pointer-subtract -fsanitize=address -fsanitize=pointer-compare
TESTS= -DLUA_USER_H='"ltests.h"' -DLUAI_ASSERT


LOCAL = $(TESTS) $(CWARNS)


# To enable Linux goodies, -DLUA_USE_LINUX
# For C++, using C++23 standard
MYCFLAGS= $(LOCAL) -std=c++23 -DLUA_USE_LINUX
MYLDFLAGS= $(LOCAL) -Wl,-E
MYLIBS= -ldl


CC= g++
CFLAGS= -Wall -O2 $(MYCFLAGS) -fno-stack-protector -fno-common -march=native
AR= ar rc
RANLIB= ranlib
RM= rm -f



# == END OF USER SETTINGS. NO NEED TO CHANGE ANYTHING BELOW THIS LINE =========


LIBS = -lm

CORE_T=	liblua.a
CORE_O=	lapi.o lcode.o lctype.o ldebug.o ldo.o ldump.o lfunc.o lgc.o llex.o \
	lmem.o lobject.o lopcodes.o lparser.o lstate.o lstring.o ltable.o \
	ltm.o lundump.o lvm.o lzio.o ltests.o
AUX_O=	lauxlib.o
LIB_O=	lbaselib.o ldblib.o liolib.o lmathlib.o loslib.o ltablib.o lstrlib.o \
	lutf8lib.o loadlib.o lcorolib.o linit.o

LUA_T=	lua
LUA_O=	lua.o


ALL_T= $(CORE_T) $(LUA_T)
ALL_O= $(CORE_O) $(LUA_O) $(AUX_O) $(LIB_O)
ALL_A= $(CORE_T)

all:	$(ALL_T)
	touch all

o:	$(ALL_O)

a:	$(ALL_A)

$(CORE_T): $(CORE_O) $(AUX_O) $(LIB_O)
	$(AR) $@ $?
	$(RANLIB) $@

$(LUA_T): $(LUA_O) $(CORE_T)
	$(CC) -o $@ $(MYLDFLAGS) $(LUA_O) $(CORE_T) $(LIBS) $(MYLIBS) $(DL)

# Pattern rule for C++ compilation
%.o: %.cpp
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	$(RM) $(ALL_T) $(ALL_O)

depend:
	@$(CC) $(CFLAGS) -MM *.cpp

echo:
	@echo "CC = $(CC)"
	@echo "CFLAGS = $(CFLAGS)"
	@echo "AR = $(AR)"
	@echo "RANLIB = $(RANLIB)"
	@echo "RM = $(RM)"
	@echo "MYCFLAGS = $(MYCFLAGS)"
	@echo "MYLDFLAGS = $(MYLDFLAGS)"
	@echo "MYLIBS = $(MYLIBS)"
	@echo "DL = $(DL)"

$(ALL_O): makefile ltests.h

# DO NOT EDIT
# automatically made with 'gcc -MM l*.c'

lapi.o: lapi.cpp lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h ldebug.h ldo.h lfunc.h lgc.h lstring.h \
 ltable.h lundump.h lvm.h
lauxlib.o: lauxlib.cpp lprefix.h lua.h luaconf.h lauxlib.h llimits.h
lbaselib.o: lbaselib.cpp lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
lcode.o: lcode.cpp lprefix.h lua.h luaconf.h lcode.h llex.h lobject.h \
 llimits.h lzio.h lmem.h lopcodes.h lparser.h ldebug.h lstate.h ltm.h \
 ldo.h lgc.h lstring.h ltable.h lvm.h lopnames.h
lcorolib.o: lcorolib.cpp lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
lctype.o: lctype.cpp lprefix.h lctype.h lua.h luaconf.h llimits.h
ldblib.o: ldblib.cpp lprefix.h lua.h luaconf.h lauxlib.h lualib.h llimits.h
ldebug.o: ldebug.cpp lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h lcode.h llex.h lopcodes.h lparser.h \
 ldebug.h ldo.h lfunc.h lstring.h lgc.h ltable.h lvm.h
ldo.o: ldo.cpp lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h ldebug.h ldo.h lfunc.h lgc.h lopcodes.h \
 lparser.h lstring.h ltable.h lundump.h lvm.h
ldump.o: ldump.cpp lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h lgc.h ltable.h lundump.h
lfunc.o: lfunc.cpp lprefix.h lua.h luaconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lfunc.h lgc.h
lgc.o: lgc.cpp lprefix.h lua.h luaconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lfunc.h lgc.h llex.h lstring.h \
 ltable.h
linit.o: linit.cpp lprefix.h lua.h luaconf.h lualib.h lauxlib.h llimits.h
liolib.o: liolib.cpp lprefix.h lua.h luaconf.h lauxlib.h lualib.h llimits.h
llex.o: llex.cpp lprefix.h lua.h luaconf.h lctype.h llimits.h ldebug.h \
 lstate.h lobject.h ltm.h lzio.h lmem.h ldo.h lgc.h llex.h lparser.h \
 lstring.h ltable.h
lmathlib.o: lmathlib.cpp lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
lmem.o: lmem.cpp lprefix.h lua.h luaconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lgc.h
loadlib.o: loadlib.cpp lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
lobject.o: lobject.cpp lprefix.h lua.h luaconf.h lctype.h llimits.h \
 ldebug.h lstate.h lobject.h ltvalue.h ltm.h lzio.h lmem.h ldo.h lstring.h lgc.h \
 lvm.h
lopcodes.o: lopcodes.cpp lprefix.h lopcodes.h llimits.h lua.h luaconf.h \
 lobject.h
loslib.o: loslib.cpp lprefix.h lua.h luaconf.h lauxlib.h lualib.h llimits.h
lparser.o: lparser.cpp lprefix.h lua.h luaconf.h lcode.h llex.h lobject.h \
 llimits.h lzio.h lmem.h lopcodes.h lparser.h ldebug.h lstate.h ltm.h \
 ldo.h lfunc.h lstring.h lgc.h ltable.h
lstate.o: lstate.cpp lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h ldebug.h ldo.h lfunc.h lgc.h llex.h \
 lstring.h ltable.h
lstring.o: lstring.cpp lprefix.h lua.h luaconf.h ldebug.h lstate.h \
 lobject.h llimits.h ltm.h lzio.h lmem.h ldo.h lstring.h lgc.h
lstrlib.o: lstrlib.cpp lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
ltable.o: ltable.cpp lprefix.h lua.h luaconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lgc.h lstring.h ltable.h lvm.h
ltablib.o: ltablib.cpp lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
ltests.o: ltests.cpp lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h lauxlib.h lcode.h llex.h lopcodes.h \
 lparser.h lctype.h ldebug.h ldo.h lfunc.h lopnames.h lstring.h lgc.h \
 ltable.h lualib.h
ltm.o: ltm.cpp lprefix.h lua.h luaconf.h ldebug.h lstate.h lobject.h \
 llimits.h ltm.h lzio.h lmem.h ldo.h lgc.h lstring.h ltable.h lvm.h
lua.o: lua.cpp lprefix.h lua.h luaconf.h lauxlib.h lualib.h llimits.h
lundump.o: lundump.cpp lprefix.h lua.h luaconf.h ldebug.h lstate.h \
 lobject.h llimits.h ltm.h lzio.h lmem.h ldo.h lfunc.h lstring.h lgc.h \
 ltable.h lundump.h
lutf8lib.o: lutf8lib.cpp lprefix.h lua.h luaconf.h lauxlib.h lualib.h \
 llimits.h
lvm.o: lvm.cpp lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h ldebug.h ldo.h lfunc.h lgc.h lopcodes.h \
 lstring.h ltable.h lvm.h ljumptab.h
lzio.o: lzio.cpp lprefix.h lua.h luaconf.h lapi.h llimits.h lstate.h \
 lobject.h ltm.h lzio.h lmem.h

# (end of Makefile)
