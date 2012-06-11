#-*- Autoconf -*-

AC_PREREQ([2.61])
AC_INIT([unagi],
	[0.3.3],
	[Arnaud Fontaine <arnau@mini-dweeb.org])

AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE
AM_MAINTAINER_MODE

# Checks for programs
AC_PROG_CC
AC_PROG_CC_C99

if test "x$ac_cv_prog_cc_c99" = "xno"
then
  AC_MSG_ERROR("C99 compiler is required", 1)
fi

AC_PROG_INSTALL
AC_LIBTOOL_DLOPEN
AC_PROG_LIBTOOL

# xcb >= 1.8 because of xcb_poll_for_queued_event
UNAGI_REQUIRES="libxdg-basedir >= 1.0.0 \
 libconfuse \
 xcb >= 1.8 \
 xcb-proto >= 1.6 \
 xcb-composite \
 xcb-xfixes \
 xcb-damage \
 xcb-randr \
 xcb-ewmh \
 xcb-event \
 xcb-aux \
 xcb-keysyms"

AC_SUBST(UNAGI_REQUIRES)

# Checks for libraries
PKG_CHECK_MODULES(UNAGI, $UNAGI_REQUIRES)

AC_SUBST(UNAGI_CFLAGS)
AC_SUBST(UNAGI_LIBS)

# Check for libev (no pkg-config)
AC_SEARCH_LIBS([ev_default_loop], [ev], [], [AC_MSG_ERROR([Require libev])])

PKG_CHECK_MODULES(RENDER_BACKEND, [
	  xcb-renderutil
	  xcb-render
])

AC_SUBST(RENDER_BACKEND_CFLAGS)
AC_SUBST(RENDER_BACKEND_LIBS)

PKG_CHECK_MODULES(EXPOSE_PLUGIN, [
	  xcb-image
])

AC_SUBST(EXPOSE_PLUGIN_CFLAGS)
AC_SUBST(EXPOSE_PLUGIN_LIBS)

# Checks for header files
AC_CHECK_HEADERS([X11/keysym.h X11/XF86keysym.h], [],
		 [AC_MSG_ERROR([Require Xlib headers related to keyboard])], [])

# Checks for typedefs, structures, and compiler characteristics
AC_HEADER_STDBOOL
AC_TYPE_SSIZE_T

# Rendering backends
renderingdir=$libdir/$PACKAGE_NAME/rendering/
AC_SUBST(renderingdir)
CFLAGS="$CFLAGS -DRENDERING_DIR='\"$renderingdir\"'"

# Plugins
pluginsdir=$libdir/$PACKAGE_NAME/plugins/
AC_SUBST(pluginsdir)
CFLAGS="$CFLAGS -DPLUGINS_DIR='\"$pluginsdir\"'"

# Support   XDG   base   directory   specification  to   install   the
# configuration file
if test -z "$XDG_CONFIG_DIR"
then
	XDG_CONFIG_DIR=$sysconfdir/xdg
fi

AC_SUBST(XDG_CONFIG_DIR)

# Configuration file directory
CFLAGS="$CFLAGS -DXDG_CONFIG_DIR='\"$XDG_CONFIG_DIR\"'"

# Customise tests
CFLAGS="$CFLAGS -Wformat-nonliteral -Wall -Wextra -Wunused-parameter \
	       -Wconversion -Wshadow -Wmissing-prototypes -Wchar-subscripts \
	       -Wundef -Wshadow -Wcast-align -Wwrite-strings -Wsign-compare \
	       -Wunused -Wno-unused-parameter -Wuninitialized -Winit-self \
	       -Wpointer-arith -Wredundant-decls -Wno-format-zero-length \
	       -Wmissing-format-attribute -Wstrict-prototypes"

AC_ARG_ENABLE([debug],
	[  --enable-debug          enter debugging mode],
	[ case "${enableval}" in
	  yes) debug=true ;;
	  no)  debug=false ;;
	  *) AC_MSG_ERROR([bad value ${enableval} for --enable-debug]) ;;
	esac],[debug=false])

AM_CONDITIONAL([DEBUG], [ test "x$debug" = "xtrue" ])

AM_COND_IF([DEBUG], [ CFLAGS="$CFLAGS -ggdb -O0 -D__DEBUG__" ])

AC_SUBST(CFLAGS)

# Doxygen support (nothing enabled by default), require autoconf-archive when
# calling autogen.sh.
#
# TODO: This is a bit ugly to define AM_CONDITIONAL but is there really a
# better way?
AC_MSG_CHECKING([for Doxygen macro (from autoconf-archive)])
m4_ifdef([DX_INIT_DOXYGEN],
         [AC_MSG_RESULT([yes])
          DX_HTML_FEATURE(ON)
          DX_PDF_FEATURE(OFF)
          DX_PS_FEATURE(OFF)
          DX_INIT_DOXYGEN([$PACKAGE_NAME], [doxygen.cfg], [api])],
         [AC_MSG_RESULT([no])
          AM_CONDITIONAL([DX_COND_doc], [false])
          AM_CONDITIONAL([DX_COND_html], [false])
          AM_CONDITIONAL([DX_COND_chm], [false])
          AM_CONDITIONAL([DX_COND_chi], [false])
          AM_CONDITIONAL([DX_COND_man], [false])
          AM_CONDITIONAL([DX_COND_rtf], [false])
          AM_CONDITIONAL([DX_COND_xml], [false])
          AM_CONDITIONAL([DX_COND_ps], [false])
          AM_CONDITIONAL([DX_COND_pdf], [false])
          AM_CONDITIONAL([DX_COND_latex], [false])])

AC_CONFIG_FILES([Makefile
	include/Makefile
	src/Makefile
	rendering/Makefile
	plugins/Makefile
	doc/Makefile
	unagi.pc])

AC_OUTPUT
