INCLUDES = $(DEPS_CFLAGS) -I$(top_srcdir)/include

render_la_LDFLAGS = -no-undefined -module -avoid-version $(RENDER_BACKEND_LIBS)
render_la_SOURCES = render.c
render_la_LIBTOOLFLAGS = --tag=disable-static
render_la_CFLAGS = $(RENDER_BACKEND_CFLAGS)

rendering_LTLIBRARIES =	render.la
