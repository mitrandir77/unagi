#-*- Autoconf -*-

AC_PREREQ([2.63])
AC_INIT([pcompmgr],
	[0.1],
	[Arnaud Fontaine <arnau@mini-dweeb.org])

AM_INIT_AUTOMAKE
AM_MAINTAINER_MODE

# Checks for programs
AC_PROG_CC
AC_PROG_CC_C99
AC_PROG_INSTALL
AC_LIBTOOL_DLOPEN
AC_PROG_LIBTOOL

# Checks for libraries
PKG_CHECK_MODULES(DEPS, [
	  libxdg-basedir >= 1.0.0
	  libconfuse

	  xcb >= 1.4
	  xcb-composite
	  xcb-xfixes
	  xcb-damage
	  xcb-ewmh
	  xcb-event
	  xcb-aux
	  xcb-keysyms
])

AC_SUBST(DEPS_CFLAGS)
AC_SUBST(DEPS_LIBS)

PKG_CHECK_MODULES(RENDER_BACKEND, [
	  xcb-renderutil
	  xcb-render
	  xcb-shape
])

AC_SUBST(RENDER_BACKEND_CFLAGS)
AC_SUBST(RENDER_BACKEND_LIBS)

PKG_CHECK_MODULES(EXPOSE_PLUGIN, [
	  xcb-image
])

AC_SUBST(EXPOSE_PLUGIN_CFLAGS)
AC_SUBST(EXPOSE_PLUGIN_LIBS)

# Checks for header files
AC_CHECK_HEADERS([stdint.h stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics
AC_HEADER_STDBOOL
AC_TYPE_UINT32_T
AC_TYPE_SSIZE_T
AC_C_INLINE

# Checks for library functions
AC_FUNC_ERROR_AT_LINE

# Rendering backends
renderingdir=$libdir/$PACKAGE_NAME/rendering/
AC_SUBST(renderingdir)
CFLAGS="$CFLAGS -DRENDERING_DIR='\"$renderingdir\"'"

# Plugins
pluginsdir=$libdir/$PACKAGE_NAME/plugins/
AC_SUBST(pluginsdir)
CFLAGS="$CFLAGS -DPLUGINS_DIR='\"$pluginsdir\"'"

# Customise tests
CFLAGS="$CFLAGS -Werror -Wall -Wextra -Wunused-parameter -Wconversion -Wshadow -Wmissing-prototypes"

AC_ARG_ENABLE(debug,
        [  --enable-debug          enter debugging mode],
        [HAVE_DEBUG=yes],
        [HAVE_DEBUG=no])

if test "x$HAVE_DEBUG" = "xyes"
then
   CFLAGS="$CFLAGS -ggdb -O0 -D__DEBUG__"
fi

# Support   XDG   base   directory   specification  to   install   the
# configuration file
if test -z "$XDG_CONFIG_DIR"
then
	XDG_CONFIG_DIR=$sysconfdir/xdg
fi

AC_SUBST(XDG_CONFIG_DIR)

AC_SUBST(CFLAGS)

AC_CONFIG_FILES([Makefile
	include/Makefile
	src/Makefile
	rendering/Makefile
	plugins/Makefile])

AC_OUTPUT
