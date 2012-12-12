include Makefile.sources

noinst_LIBRARIES = libi965.a

AM_CFLAGS = \
	-Iinclude \
	-I$(top_srcdir)/src/gallium/winsys/intel/drm \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_srcdir)/src/gallium/auxiliary \
	-I$(top_srcdir)/include \
	$(INTEL_CFLAGS) \
	$(DEFINES) \
	$(PIC_FLAGS) \
	$(VISIBILITY_CFLAGS)

libi965_a_SOURCES = \
	$(C_SOURCES)
