include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/lib

noinst_PROGRAMS = spl
sbin_PROGRAMS = splat

spl_SOURCES = $(top_srcdir)/cmd/spl.c

splat_SOURCES = $(top_srcdir)/cmd/splat.c
splat_LDFLAGS = $(top_builddir)/lib/libcommon.la

EXTRA_DIST = $(top_srcdir)/cmd/splat.h
