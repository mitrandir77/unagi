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

# Checks for libraries
PKG_CHECK_MODULES(DEPS, [
	  xcb
	  xcb-composite
	  xcb-xfixes
	  xcb-damage
	  xcb-ewmh
	  xcb-event
	  xcb-shape
	  xcb-renderutil
	  xcb-aux
])

AC_SUBST(DEPS_CFLAGS)
AC_SUBST(DEPS_LIBS)

# Checks for header files
AC_CHECK_HEADERS([stdint.h stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics
AC_HEADER_STDBOOL
AC_TYPE_UINT32_T
AC_TYPE_SSIZE_T
AC_C_INLINE

# Checks for library functions
AC_FUNC_ERROR_AT_LINE

# Customise tests
CFLAGS="-Werror -Wall -Wextra -Wunused-parameter -Wconversion -Wshadow -Wmissing-prototypes"

AC_ARG_ENABLE(debug,
        [  --enable-debug          enter debugging mode],
        [HAVE_DEBUG=yes],
        [HAVE_DEBUG=no])

if test "$HAVE_DEBUG" = "yes"; then
   CFLAGS="$CFLAGS -ggdb -O0 -D__DEBUG__"
fi

AC_SUBST(CFLAGS)

AC_CONFIG_FILES([Makefile
	include/Makefile
	src/Makefile])

AC_OUTPUT
