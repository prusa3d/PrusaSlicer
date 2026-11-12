INSTALL_ROOT=   $(INSTALL_TOP)

BINDIR=         $(INSTALL_ROOT:\=/)/bin
INCLUDEDIR=     $(INSTALL_ROOT:\=/)/include
LIBDIR=         $(INSTALL_ROOT:\=/)/lib

LUA_NAME=       lua.exe
LUA_LIB_NAME=   lua.lib

LUAC_NAME=      luac.exe

SOURCE_ROOT=    $(MAKEDIR)

!IFNDEF CC
CC = cl.exe
!ENDIF

!IFNDEF LD
LD = link.exe
!ENDIF

!IFNDEF AR
AR = lib.exe
!ENDIF

CFLAGS=/O2 /TC /MD /DLUA_COMPAT_5_3

LUA_LIB_DEPS=lapi.obj lcode.obj lctype.obj ldebug.obj ldo.obj ldump.obj \
    lfunc.obj lgc.obj llex.obj lmem.obj lobject.obj lopcodes.obj \
    lparser.obj lstate.obj lstring.obj ltable.obj ltm.obj lundump.obj \
    lvm.obj lzio.obj lauxlib.obj lbaselib.obj lcorolib.obj ldblib.obj \
    liolib.obj lmathlib.obj loadlib.obj loslib.obj lstrlib.obj ltablib.obj \
    lutf8lib.obj linit.obj

LUA_DEPS=lua.obj

LUAC_DEPS=luac.obj

ALL_DEPS=$(LUA_LIB_DEPS) $(LUA_DEPS) $(LUAC_DEPS)

TO_BIN=$(LUA_NAME) $(LUAC_NAME)
TO_INCLUDE=lua.h luaconf.h lualib.h lauxlib.h lua.hpp
TO_LIB=$(LUA_LIB_NAME)

all: $(LUA_LIB_NAME) $(LUA_NAME) $(LUAC_NAME)

cleanobj:
    @echo Cleaning object files...
    @del /F $(LUA_DEPS) $(LUAC_DEPS) $(LUA_LIB_DEPS)

clean: cleanobj
    @echo Cleaning binaries and libraries...
    @del /F $(LUA_NAME) $(LUAC_NAME) $(LUA_LIB_NAME)

install: all
    @echo Creating destination directory for binaries...
    @if not exist "$(BINDIR)" mkdir "$(BINDIR)"
    @echo Copying binaries...
    @for %%G in ($(TO_BIN)) do copy /Y "%%G" "$(BINDIR)\%%G"
    @echo Creating destination directory for headers...
    @if not exist "$(INCLUDEDIR)" mkdir "$(INCLUDEDIR)"
    @echo Copying headers...
    @for %%G in ($(TO_INCLUDE)) do copy /Y "$(SOURCE_ROOT)\src\%%G" "$(INCLUDEDIR)\%%G"
    @echo Creating destination directory for libraries...
    @if not exist "$(LIBDIR)" mkdir "$(LIBDIR)"
    @echo Copying libraries...
    @for %%G in ($(TO_LIB)) do copy /Y "%%G" "$(LIBDIR)\%%G"

$(LUA_LIB_NAME): $(LUA_LIB_DEPS)
    "$(AR)" /OUT:$(LUA_LIB_NAME) $(LUA_LIB_DEPS)

$(LUA_NAME): $(LUA_DEPS) $(LUA_LIB_DEPS) 
    "$(LD)" /OUT:$(LUA_NAME) $(LUA_DEPS) $(LUA_LIB_DEPS)

$(LUAC_NAME): $(LUAC_DEPS) $(LUA_LIB_DEPS)
    "$(LD)" /OUT:$(LUAC_NAME) $(LUAC_DEPS) $(LUA_LIB_DEPS)

lapi.obj: {$(SOURCE_ROOT)\src}lapi.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lapi.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}lstate.h \
    {$(SOURCE_ROOT)\src}lobject.h \
    {$(SOURCE_ROOT)\src}ltm.h \
    {$(SOURCE_ROOT)\src}lzio.h \
    {$(SOURCE_ROOT)\src}lmem.h \
    {$(SOURCE_ROOT)\src}ldebug.h \
    {$(SOURCE_ROOT)\src}ldo.h \
    {$(SOURCE_ROOT)\src}lfunc.h \
    {$(SOURCE_ROOT)\src}lgc.h \
    {$(SOURCE_ROOT)\src}lstring.h \
    {$(SOURCE_ROOT)\src}ltable.h \
    {$(SOURCE_ROOT)\src}lundump.h \
    {$(SOURCE_ROOT)\src}lvm.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"
    
lauxlib.obj: {$(SOURCE_ROOT)\src}lauxlib.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lauxlib.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

lbaselib.obj: {$(SOURCE_ROOT)\src}lbaselib.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lauxlib.h \
    {$(SOURCE_ROOT)\src}lualib.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

lcode.obj: {$(SOURCE_ROOT)\src}lcode.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lcode.h \
    {$(SOURCE_ROOT)\src}llex.h \
    {$(SOURCE_ROOT)\src}lobject.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}lzio.h \
    {$(SOURCE_ROOT)\src}lmem.h \
    {$(SOURCE_ROOT)\src}lopcodes.h \
    {$(SOURCE_ROOT)\src}lparser.h \
    {$(SOURCE_ROOT)\src}ldebug.h \
    {$(SOURCE_ROOT)\src}lstate.h \
    {$(SOURCE_ROOT)\src}ltm.h \
    {$(SOURCE_ROOT)\src}ldo.h \
    {$(SOURCE_ROOT)\src}lgc.h \
    {$(SOURCE_ROOT)\src}lstring.h \
    {$(SOURCE_ROOT)\src}ltable.h \
    {$(SOURCE_ROOT)\src}lvm.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

lcorolib.obj: {$(SOURCE_ROOT)\src}lcorolib.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lauxlib.h \
    {$(SOURCE_ROOT)\src}lualib.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

lctype.obj: {$(SOURCE_ROOT)\src}lctype.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lctype.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}llimits.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

ldblib.obj: {$(SOURCE_ROOT)\src}ldblib.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lauxlib.h \
    {$(SOURCE_ROOT)\src}lualib.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

ldebug.obj: {$(SOURCE_ROOT)\src}ldebug.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lapi.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}lstate.h \
    {$(SOURCE_ROOT)\src}lobject.h \
    {$(SOURCE_ROOT)\src}ltm.h \
    {$(SOURCE_ROOT)\src}lzio.h \
    {$(SOURCE_ROOT)\src}lmem.h \
    {$(SOURCE_ROOT)\src}lcode.h \
    {$(SOURCE_ROOT)\src}llex.h \
    {$(SOURCE_ROOT)\src}lopcodes.h \
    {$(SOURCE_ROOT)\src}lparser.h \
    {$(SOURCE_ROOT)\src}ldebug.h \
    {$(SOURCE_ROOT)\src}ldo.h \
    {$(SOURCE_ROOT)\src}lfunc.h \
    {$(SOURCE_ROOT)\src}lstring.h \
    {$(SOURCE_ROOT)\src}lgc.h \
    {$(SOURCE_ROOT)\src}ltable.h \
    {$(SOURCE_ROOT)\src}lvm.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

ldo.obj: {$(SOURCE_ROOT)\src}ldo.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lapi.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}lstate.h \
    {$(SOURCE_ROOT)\src}lobject.h \
    {$(SOURCE_ROOT)\src}ltm.h \
    {$(SOURCE_ROOT)\src}lzio.h \
    {$(SOURCE_ROOT)\src}lmem.h \
    {$(SOURCE_ROOT)\src}ldebug.h \
    {$(SOURCE_ROOT)\src}ldo.h \
    {$(SOURCE_ROOT)\src}lfunc.h \
    {$(SOURCE_ROOT)\src}lgc.h \
    {$(SOURCE_ROOT)\src}lopcodes.h \
    {$(SOURCE_ROOT)\src}lparser.h \
    {$(SOURCE_ROOT)\src}lstring.h \
    {$(SOURCE_ROOT)\src}ltable.h \
    {$(SOURCE_ROOT)\src}lundump.h \
    {$(SOURCE_ROOT)\src}lvm.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

ldump.obj: {$(SOURCE_ROOT)\src}ldump.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lobject.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}lstate.h \
    {$(SOURCE_ROOT)\src}ltm.h \
    {$(SOURCE_ROOT)\src}lzio.h \
    {$(SOURCE_ROOT)\src}lmem.h \
    {$(SOURCE_ROOT)\src}lundump.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

lfunc.obj: {$(SOURCE_ROOT)\src}lfunc.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lfunc.h \
    {$(SOURCE_ROOT)\src}lobject.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}lgc.h \
    {$(SOURCE_ROOT)\src}lstate.h \
    {$(SOURCE_ROOT)\src}ltm.h \
    {$(SOURCE_ROOT)\src}lzio.h \
    {$(SOURCE_ROOT)\src}lmem.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

lgc.obj: {$(SOURCE_ROOT)\src}lgc.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}ldebug.h \
    {$(SOURCE_ROOT)\src}lstate.h \
    {$(SOURCE_ROOT)\src}lobject.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}ltm.h \
    {$(SOURCE_ROOT)\src}lzio.h \
    {$(SOURCE_ROOT)\src}lmem.h \
    {$(SOURCE_ROOT)\src}ldo.h \
    {$(SOURCE_ROOT)\src}lfunc.h \
    {$(SOURCE_ROOT)\src}lgc.h \
    {$(SOURCE_ROOT)\src}lstring.h \
    {$(SOURCE_ROOT)\src}ltable.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

linit.obj: {$(SOURCE_ROOT)\src}linit.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lualib.h \
    {$(SOURCE_ROOT)\src}lauxlib.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

liolib.obj: {$(SOURCE_ROOT)\src}liolib.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lauxlib.h \
    {$(SOURCE_ROOT)\src}lualib.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

llex.obj: {$(SOURCE_ROOT)\src}llex.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lctype.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}ldebug.h \
    {$(SOURCE_ROOT)\src}lstate.h \
    {$(SOURCE_ROOT)\src}lobject.h \
    {$(SOURCE_ROOT)\src}ltm.h \
    {$(SOURCE_ROOT)\src}lzio.h \
    {$(SOURCE_ROOT)\src}lmem.h \
    {$(SOURCE_ROOT)\src}ldo.h \
    {$(SOURCE_ROOT)\src}lgc.h \
    {$(SOURCE_ROOT)\src}llex.h \
    {$(SOURCE_ROOT)\src}lparser.h \
    {$(SOURCE_ROOT)\src}lstring.h \
    {$(SOURCE_ROOT)\src}ltable.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

lmathlib.obj: {$(SOURCE_ROOT)\src}lmathlib.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lauxlib.h \
    {$(SOURCE_ROOT)\src}lualib.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

lmem.obj: {$(SOURCE_ROOT)\src}lmem.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}ldebug.h \
    {$(SOURCE_ROOT)\src}lstate.h \
    {$(SOURCE_ROOT)\src}lobject.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}ltm.h \
    {$(SOURCE_ROOT)\src}lzio.h \
    {$(SOURCE_ROOT)\src}lmem.h \
    {$(SOURCE_ROOT)\src}ldo.h \
    {$(SOURCE_ROOT)\src}lgc.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

loadlib.obj: {$(SOURCE_ROOT)\src}loadlib.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lauxlib.h \
    {$(SOURCE_ROOT)\src}lualib.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

lobject.obj: {$(SOURCE_ROOT)\src}lobject.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lctype.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}ldebug.h \
    {$(SOURCE_ROOT)\src}lstate.h \
    {$(SOURCE_ROOT)\src}lobject.h \
    {$(SOURCE_ROOT)\src}ltm.h \
    {$(SOURCE_ROOT)\src}lzio.h \
    {$(SOURCE_ROOT)\src}lmem.h \
    {$(SOURCE_ROOT)\src}ldo.h \
    {$(SOURCE_ROOT)\src}lstring.h \
    {$(SOURCE_ROOT)\src}lgc.h \
    {$(SOURCE_ROOT)\src}lvm.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

lopcodes.obj: {$(SOURCE_ROOT)\src}lopcodes.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lopcodes.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

loslib.obj: {$(SOURCE_ROOT)\src}loslib.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lauxlib.h \
    {$(SOURCE_ROOT)\src}lualib.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

lparser.obj: {$(SOURCE_ROOT)\src}lparser.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lcode.h \
    {$(SOURCE_ROOT)\src}llex.h \
    {$(SOURCE_ROOT)\src}lobject.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}lzio.h \
    {$(SOURCE_ROOT)\src}lmem.h \
    {$(SOURCE_ROOT)\src}lopcodes.h \
    {$(SOURCE_ROOT)\src}lparser.h \
    {$(SOURCE_ROOT)\src}ldebug.h \
    {$(SOURCE_ROOT)\src}lstate.h \
    {$(SOURCE_ROOT)\src}ltm.h \
    {$(SOURCE_ROOT)\src}ldo.h \
    {$(SOURCE_ROOT)\src}lfunc.h \
    {$(SOURCE_ROOT)\src}lstring.h \
    {$(SOURCE_ROOT)\src}lgc.h \
    {$(SOURCE_ROOT)\src}ltable.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

lstate.obj: {$(SOURCE_ROOT)\src}lstate.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lapi.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}lstate.h \
    {$(SOURCE_ROOT)\src}lobject.h \
    {$(SOURCE_ROOT)\src}ltm.h \
    {$(SOURCE_ROOT)\src}lzio.h \
    {$(SOURCE_ROOT)\src}lmem.h \
    {$(SOURCE_ROOT)\src}ldebug.h \
    {$(SOURCE_ROOT)\src}ldo.h \
    {$(SOURCE_ROOT)\src}lfunc.h \
    {$(SOURCE_ROOT)\src}lgc.h \
    {$(SOURCE_ROOT)\src}llex.h \
    {$(SOURCE_ROOT)\src}lstring.h \
    {$(SOURCE_ROOT)\src}ltable.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

lstring.obj: {$(SOURCE_ROOT)\src}lstring.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}ldebug.h \
    {$(SOURCE_ROOT)\src}lstate.h \
    {$(SOURCE_ROOT)\src}lobject.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}ltm.h \
    {$(SOURCE_ROOT)\src}lzio.h \
    {$(SOURCE_ROOT)\src}lmem.h \
    {$(SOURCE_ROOT)\src}ldo.h \
    {$(SOURCE_ROOT)\src}lstring.h \
    {$(SOURCE_ROOT)\src}lgc.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

lstrlib.obj: {$(SOURCE_ROOT)\src}lstrlib.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lauxlib.h \
    {$(SOURCE_ROOT)\src}lualib.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

ltable.obj: {$(SOURCE_ROOT)\src}ltable.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}ldebug.h \
    {$(SOURCE_ROOT)\src}lstate.h \
    {$(SOURCE_ROOT)\src}lobject.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}ltm.h \
    {$(SOURCE_ROOT)\src}lzio.h \
    {$(SOURCE_ROOT)\src}lmem.h \
    {$(SOURCE_ROOT)\src}ldo.h \
    {$(SOURCE_ROOT)\src}lgc.h \
    {$(SOURCE_ROOT)\src}lstring.h \
    {$(SOURCE_ROOT)\src}ltable.h \
    {$(SOURCE_ROOT)\src}lvm.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

ltablib.obj: {$(SOURCE_ROOT)\src}ltablib.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lauxlib.h \
    {$(SOURCE_ROOT)\src}lualib.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

ltm.obj: {$(SOURCE_ROOT)\src}ltm.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}ldebug.h \
    {$(SOURCE_ROOT)\src}lstate.h \
    {$(SOURCE_ROOT)\src}lobject.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}ltm.h \
    {$(SOURCE_ROOT)\src}lzio.h \
    {$(SOURCE_ROOT)\src}lmem.h \
    {$(SOURCE_ROOT)\src}ldo.h \
    {$(SOURCE_ROOT)\src}lstring.h \
    {$(SOURCE_ROOT)\src}lgc.h \
    {$(SOURCE_ROOT)\src}ltable.h \
    {$(SOURCE_ROOT)\src}lvm.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

lua.obj: {$(SOURCE_ROOT)\src}lua.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lauxlib.h \
    {$(SOURCE_ROOT)\src}lualib.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

luac.obj: {$(SOURCE_ROOT)\src}luac.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lauxlib.h \
    {$(SOURCE_ROOT)\src}lobject.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}lstate.h \
    {$(SOURCE_ROOT)\src}ltm.h \
    {$(SOURCE_ROOT)\src}lzio.h \
    {$(SOURCE_ROOT)\src}lmem.h \
    {$(SOURCE_ROOT)\src}lundump.h \
    {$(SOURCE_ROOT)\src}ldebug.h \
    {$(SOURCE_ROOT)\src}lopcodes.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

lundump.obj: {$(SOURCE_ROOT)\src}lundump.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}ldebug.h \
    {$(SOURCE_ROOT)\src}lstate.h \
    {$(SOURCE_ROOT)\src}lobject.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}ltm.h \
    {$(SOURCE_ROOT)\src}lzio.h \
    {$(SOURCE_ROOT)\src}lmem.h \
    {$(SOURCE_ROOT)\src}ldo.h \
    {$(SOURCE_ROOT)\src}lfunc.h \
    {$(SOURCE_ROOT)\src}lstring.h \
    {$(SOURCE_ROOT)\src}lgc.h \
    {$(SOURCE_ROOT)\src}lundump.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

lutf8lib.obj: {$(SOURCE_ROOT)\src}lutf8lib.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}lauxlib.h \
    {$(SOURCE_ROOT)\src}lualib.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

lvm.obj: {$(SOURCE_ROOT)\src}lvm.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}ldebug.h \
    {$(SOURCE_ROOT)\src}lstate.h \
    {$(SOURCE_ROOT)\src}lobject.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}ltm.h \
    {$(SOURCE_ROOT)\src}lzio.h \
    {$(SOURCE_ROOT)\src}lmem.h \
    {$(SOURCE_ROOT)\src}ldo.h \
    {$(SOURCE_ROOT)\src}lfunc.h \
    {$(SOURCE_ROOT)\src}lgc.h \
    {$(SOURCE_ROOT)\src}lopcodes.h \
    {$(SOURCE_ROOT)\src}lstring.h \
    {$(SOURCE_ROOT)\src}ltable.h \
    {$(SOURCE_ROOT)\src}lvm.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"

lzio.obj: {$(SOURCE_ROOT)\src}lzio.c \
    {$(SOURCE_ROOT)\src}lprefix.h \
    {$(SOURCE_ROOT)\src}lua.h \
    {$(SOURCE_ROOT)\src}luaconf.h \
    {$(SOURCE_ROOT)\src}llimits.h \
    {$(SOURCE_ROOT)\src}lmem.h \
    {$(SOURCE_ROOT)\src}lstate.h \
    {$(SOURCE_ROOT)\src}lobject.h \
    {$(SOURCE_ROOT)\src}ltm.h \
    {$(SOURCE_ROOT)\src}lzio.h
    @cd "$(SOURCE_ROOT)\src"
    "$(CC)" /c $(CFLAGS) /Fo"$(MAKEDIR)\$*.obj" $*.c
    @cd "$(MAKEDIR)"
