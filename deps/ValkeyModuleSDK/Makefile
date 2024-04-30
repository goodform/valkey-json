#set environment variable RM_INCLUDE_DIR to the location of valkeymodule.h
ifndef VKM_INCLUDE_DIR
	RM_INCLUDE_DIR=./
endif

ifndef VKMUTIL_LIBDIR
	VKMUTIL_LIBDIR=vkmutil
endif

ifndef SRC_DIR
	SRC_DIR=example
endif


all: module.so

module.so:
	$(MAKE) -C ./$(SRC_DIR)
	cp ./$(SRC_DIR)/module.so .

clean: FORCE
	rm -rf *.xo *.so *.o
	rm -rf ./$(SRC_DIR)/*.xo ./$(SRC_DIR)/*.so ./$(SRC_DIR)/*.o
	rm -rf ./$(VKMUTIL_LIBDIR)/*.so ./$(VKMUTIL_LIBDIR)/*.o ./$(VKMUTIL_LIBDIR)/*.a

FORCE:
