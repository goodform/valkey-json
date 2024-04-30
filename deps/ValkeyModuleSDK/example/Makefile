#set environment variable VKM_INCLUDE_DIR to the location of valkeymodule.h
ifndef VKM_INCLUDE_DIR
	VKM_INCLUDE_DIR=../
endif

ifndef VKMUTIL_LIBDIR
	VKMUTIL_LIBDIR=../vkmutil
endif

# find the OS
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

# Compile flags for linux / osx
ifeq ($(uname_S),Linux)
	SHOBJ_CFLAGS ?=  -fno-common -g -ggdb
	SHOBJ_LDFLAGS ?= -shared -Bsymbolic
else
	SHOBJ_CFLAGS ?= -dynamic -fno-common -g -ggdb
	SHOBJ_LDFLAGS ?= -bundle -undefined dynamic_lookup
endif
CFLAGS = -I$(VKM_INCLUDE_DIR) -Wall -g -fPIC -lc -lm -Og -std=gnu99  
CC=gcc

all: vkmutil module.so

vkmutil: FORCE
	$(MAKE) -C $(VKMUTIL_LIBDIR)

module.so: module.o
	$(LD) -o $@ module.o $(SHOBJ_LDFLAGS) $(LIBS) -L$(VKMUTIL_LIBDIR) -lvkmutil -lc 

clean:
	rm -rf *.xo *.so *.o

FORCE:
