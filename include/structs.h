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

#ifndef STRUCTS_H
#define STRUCTS_H

#include <xcb/xcb.h>
#include <xcb/xcb_event.h>
#include <xcb/render.h>

#include "window.h"

typedef struct _display_extensions_t
{
  const xcb_query_extension_reply_t *composite;
  const xcb_query_extension_reply_t *xfixes;
  const xcb_query_extension_reply_t *damage;
} display_extensions_t;

typedef struct _conf_t
{
  xcb_connection_t *connection;
  int screen_nbr;
  xcb_screen_t *screen;
  display_extensions_t extensions;
  xcb_event_handlers_t evenths;
  xcb_window_t cm_window;
  window_t *windows;
  window_t *_windows_tail;

  xcb_render_picture_t root_picture;
  xcb_render_pictvisual_t root_pictvisual;
} conf_t;

extern conf_t globalconf;

#endif
