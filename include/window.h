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

#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>
#include <limits.h>

#include <xcb/xcb.h>
#include <xcb/damage.h>

typedef struct _window_t
{
  xcb_window_t id;
  uint32_t opacity;
  xcb_get_window_attributes_reply_t *attributes;
  xcb_damage_damage_t damage;

  struct _window_t *next;
} window_t;

#define OPACITY_OPAQUE UINT_MAX

void window_list_cleanup(void);
void window_get_root_background_pixmap(void);
xcb_pixmap_t window_get_root_background_pixmap_finalise(void);
void window_add_all(const int nwindows, const xcb_window_t *);
void window_add_one(const xcb_window_t);

#endif
