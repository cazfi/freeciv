## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = \
	libfcivnetwork.la

AM_CPPFLAGS = \
	-I$(top_srcdir)/utility \
	-I$(top_srcdir)/common \
	-I$(top_srcdir)/common/aicore \
	-I$(top_srcdir)/dependencies/tinycthread \
	-I$(top_srcdir)/gen_headers/enums \
	$(JANSSON_CFLAGS)

libfcivnetwork_la_SOURCES = \
	connection.c	\
	connection.h	\
	conn_types.h	\
	dataio.h	\
	dataio_json.c	\
	dataio_json.h	\
	dataio_raw.c	\
	dataio_raw.h	\
	packets.c	\
	packets.h	\
	packets_json.h	\
	packets_json.c

EXTRA_DIST = \
	packets.def
