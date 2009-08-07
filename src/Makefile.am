INCLUDES = $(DEPS_CFLAGS) -I$(top_srcdir)/include
pcompmgr_LDADD = $(DEPS_LIBS) -ldl
pcompmgr_LDFLAGS = -rdynamic
bin_PROGRAMS = pcompmgr

pcompmgr_SOURCES = display.c 	\
	event.c 		\
	window.c 		\
	atoms.c 		\
	util.c 			\
	key.c 			\
	plugin.c		\
	plugin_common.c		\
	rendering.c		\
	pcompmgr.c
