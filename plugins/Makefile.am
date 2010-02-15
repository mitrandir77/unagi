INCLUDES = $(UNAGI_CFLAGS) -I$(top_srcdir)/include

opacity_la_LDFLAGS = -no-undefined -module -avoid-version
opacity_la_SOURCES = opacity.c
opacity_la_LIBTOOLFLAGS = --tag=disable-static

expose_la_LDFLAGS = -no-undefined -module -avoid-version -lm $(EXPOSE_PLUGIN_LIBS)
expose_la_CFLAGS = $(EXPOSE_PLUGIN_CFLAGS)
expose_la_SOURCES = expose.c
expose_la_LIBTOOLFLAGS = --tag=disable-static

plugins_LTLIBRARIES = opacity.la expose.la
