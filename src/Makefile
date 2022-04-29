.PHONY: default ljack
default: ljack

BUILD_DATE  := $(shell date "+%Y-%m-%dT%H:%M:%S")

LNX_GCC_RUN := gcc -shared -fPIC -O2 -g
WIN_GCC_RUN := gcc -shared -fPIC -O2
MAC_GCC_RUN := MACOSX_DEPLOYMENT_TARGET=10.8 gcc -O2 -bundle -undefined dynamic_lookup -all_load

LNX_COPTS   :=
WIN_COPTS   := -I/mingw64/include/lua5.1 
MAC_COPTS   := -I/usr/local/opt/lua/include/lua5.3 

LNX_LOPTS   := -lpthread -ljack
WIN_LOPTS   := -lkernel32
MAC_LOPTS   := -lpthread

LNX_SO_EXT  := so
WIN_SO_EXT  := dll
MAC_SO_EXT  := so

GCC_RUN     :=
SO_EXT      :=
COPTS       :=
LOPTS       :=

# platforms: LNX, WIN, MAC
# (may be set in sandbox.mk)

PLATFORM    := LNX
LUA_VERSION := 5.4

-include sandbox.mk

GCC_RUN       := $(or $(GCC_RUN),       $($(PLATFORM)_GCC_RUN))
SO_EXT        := $(or $(SO_EXT),        $($(PLATFORM)_SO_EXT))
COPTS         := $(or $(COPTS),         $($(PLATFORM)_COPTS))
LOPTS         := $(or $(LOPTS),         $($(PLATFORM)_LOPTS))

ljack:
	@mkdir -p build/lua$(LUA_VERSION)/
	$(GCC_RUN) $(COPTS) \
	    -D LJACK_VERSION=Makefile"-$(BUILD_DATE)" \
	    main.c client.c client_intern.c port.c \
	    ljack_capi_impl.c \
	    util.c error.c async_util.c   ljack_compat.c  \
	    midi_receiver.c midi_sender.c \
	    $(LOPTS) \
	    -o build/lua$(LUA_VERSION)/ljack.$(SO_EXT)
	    

