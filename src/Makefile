# Set environment variable VKM_INCLUDE_DIR to the location of valkeymodule.h
ifndef VKM_INCLUDE_DIRk
	VKM_INCLUDE_DIR=$(shell pwd)
endif

DEPS_DIR="$(VKM_INCLUDE_DIR)/../deps"

# Setting the DEBUG env variable to 1 will cause us to build with -O0
ifndef DEBUG
	DEBUG = 0
endif
DEBUGFLAGS = -g -ggdb -O2
ifeq ($(DEBUG), 1)
	DEBUGFLAGS = -g -ggdb -O0
endif

# Find the OS
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')
INCLUDE_DIRS = -I"$(VKM_INCLUDE_DIR)" -I"$(DEPS_DIR)/jsonsl"  -I"$(DEPS_DIR)/ValkeyModuleSDK/vkmutil"
CFLAGS = $(INCLUDE_DIRS) -Wall $(DEBUGFLAGS) -fPIC -std=gnu99  -D_GNU_SOURCE
CC:=$(shell sh -c 'type $(CC) >/dev/null 2>/dev/null && echo $(CC) || echo gcc')

# Compile flags for linux / osx
ifeq ($(uname_S),Linux)
	SHOBJ_CFLAGS ?=  -fno-common -g -ggdb
	SHOBJ_LDFLAGS ?= -shared -Bsymbolic -Bsymbolic-functions
else
	CFLAGS += -mmacosx-version-min=10.6
	SHOBJ_CFLAGS ?= -dynamic -fno-common -g -ggdb
	SHOBJ_LDFLAGS ?= -dylib -exported_symbol _ValkeyModule_OnLoad -macosx_version_min 10.6
endif
export CFLAGS
export DEBUGFLAGS

SOURCEDIR=$(shell pwd -P)
CC_SOURCES = $(wildcard $(SOURCEDIR)/*.c) $(wildcard $(SOURCEDIR)/dep/*.c) 
CC_OBJECTS = $(patsubst $(SOURCEDIR)/%.c, $(SOURCEDIR)/%.o, $(CC_SOURCES))
LIBS = $(DEPS_DIR)/jsonsl/jsonsl.o $(DEPS_DIR)/ValkeyModuleSDK/vkmutil/libvkmutil.a
MODULE=valkeyjson.so

all: $(MODULE) libvalkeyjson.a

jsonsl:
	$(MAKE) -C $(DEPS_DIR)/jsonsl static
.PHONY: jsonsl

vkmutil:
	$(MAKE) -C $(DEPS_DIR)/ValkeyModuleSDK/vkmutil all

.PHONY: vkmutil

$(MODULE): jsonsl vkmutil $(CC_OBJECTS)
	$(LD) -o $@ $(CC_OBJECTS) $(LIBS) $(SHOBJ_LDFLAGS) -lc -lm

libvalkeyjson.a: jsonsl vkmutil $(CC_OBJECTS)
	ar rcs $@ $(LIBS) $(CC_OBJECTS)

clean:
	rm -rvf *.xo *.so *.o *.a

deepclean:
	find ./ -name "*.[oa]" -exec rm {} \; -print
	find ./ -name "*.so" -exec rm {} \; -print

PACKAGE_NAME=valkeyjson
BUILD_DIR=../build

# Compile an executable that prints the current version
print_version: version.h print_version.c
	@$(CC) -o $@ -DPRINT_VERSION_TARGET $@.c
