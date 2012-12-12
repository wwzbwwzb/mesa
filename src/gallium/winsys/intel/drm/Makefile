TOP = ../../../../..
include $(TOP)/configs/current

LIBNAME = intelwinsys

# get C_SOURCES
include Makefile.sources

LIBRARY_INCLUDES = -I$(TOP)/include \
		   $(shell $(PKG_CONFIG) libdrm_intel --cflags-only-I)

include ../../../Makefile.template

symlinks:
