INCLUDES = $(UNAGI_CFLAGS) -I$(top_srcdir)/include
unagi_LDADD = $(UNAGI_LIBS) -ldl -lm
unagi_LDFLAGS = -rdynamic
bin_PROGRAMS = unagi

## For clock_gettime() and sqrtl() used to measure painting performance
if DEBUG
unagi_LDADD += -lrt
endif

unagi_SOURCES = display.c 	\
	event.c 		\
	window.c 		\
	atoms.c 		\
	util.c 			\
	key.c 			\
	plugin.c		\
	plugin_common.c		\
	rendering.c		\
	unagi.c
