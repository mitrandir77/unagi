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

/** \file
 *  \brief Windows management
 */

#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#include <xcb/xcb.h>
#include <xcb/damage.h>

typedef struct _window_t
{
  xcb_window_t id;
  xcb_get_window_attributes_reply_t *attributes;
  xcb_get_geometry_reply_t *geometry;
  xcb_damage_damage_t damage;
  bool damaged;
  xcb_pixmap_t pixmap;
  void *rendering;
  struct _window_t *next;
} window_t;

void window_free_pixmap(window_t *);
void window_list_cleanup(void);
window_t *window_list_get(const xcb_window_t);
void window_list_remove_window(window_t *);
void window_register_notify(const window_t *);
void window_get_root_background_pixmap(void);
xcb_pixmap_t window_get_root_background_pixmap_finalise(void);
xcb_pixmap_t window_new_root_background_pixmap(void);
xcb_pixmap_t window_get_pixmap(const window_t *);
bool window_is_visible(const window_t *);
void window_get_invisible_window_pixmap(window_t *);
void window_get_invisible_window_pixmap_finalise(window_t *);
void window_manage_existing(const int nwindows, const xcb_window_t *);
window_t *window_add(const xcb_window_t);
void window_restack(window_t *, xcb_window_t);
void window_paint_all(window_t *);

#define DO_GEOMETRY_WITH_BORDER(kind)					\
  static inline uint16_t						\
  window_##kind##_with_border(const xcb_get_geometry_reply_t *geometry)	\
  {									\
    return (uint16_t) (geometry->kind + (geometry->border_width * 2));	\
  }

DO_GEOMETRY_WITH_BORDER(width)
DO_GEOMETRY_WITH_BORDER(height)

#endif
