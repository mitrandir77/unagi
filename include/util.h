/*
 * Copyright (C) 2009 Arnaud "arnau" Fontaine <arnau@mini-dweeb.org>
 *
 * This  program is  free  software: you  can  redistribute it  and/or
 * modify  it under the  terms of  the GNU  General Public  License as
 * published by the Free Software  Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT  ANY  WARRANTY;  without   even  the  implied  warranty  of
 * MERCHANTABILITY or  FITNESS FOR A PARTICULAR PURPOSE.   See the GNU
 * General Public License for more details.
 *
 * You should have  received a copy of the  GNU General Public License
 *  along      with      this      program.      If      not,      see
 *  <http://www.gnu.org/licenses/>.
 */

#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>

#define ssizeof(foo)            (ssize_t)sizeof(foo)
#define countof(foo)            (ssizeof(foo) / ssizeof(foo[0]))

#define fatal(string, ...) _fatal(true,				\
				  __LINE__, __FUNCTION__,	\
				  string, ## __VA_ARGS__)

#define fatal_no_exit(string, ...)			\
  _fatal(false, __LINE__, __FUNCTION__, string, ## __VA_ARGS__)

void _fatal(bool, int, const char *, const char *, ...)         \
  __attribute__ ((format(printf, 4, 5)));

#define warn(string, ...) _warn(__LINE__,			\
				__FUNCTION__,			\
				string, ## __VA_ARGS__)

void _warn(int, const char *, const char *, ...)
  __attribute__ ((format(printf, 3, 4)));

#define debug(string, ...) _debug(__LINE__,			\
				  __FUNCTION__,			\
				  string, ## __VA_ARGS__)

void _debug(int, const char *, const char *, ...)
  __attribute__ ((format(printf, 3, 4)));

void util_lock_mask_get_reply(xcb_get_modifier_mapping_cookie_t cookie);
xcb_keysym_t util_key_getkeysym(const xcb_keycode_t, const uint16_t);

#define util_free(mem_pp)			   \
  {						   \
        typeof(**(mem_pp)) **__ptr = (mem_pp);     \
        free(*__ptr);                              \
        *__ptr = NULL;                             \
  }

#endif
