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

#ifndef RENDER_H
#define RENDER_H

#include <stdbool.h>

void render_preinit(void);
bool render_init(void);
bool render_init_finalise(void);
void render_init_root_background(void);
void render_paint_all(void);
bool render_is_render_request(const uint8_t);
const char *render_error_get_request_label(const uint16_t);
const char *render_error_get_error_label(const uint8_t);
void render_free_picture(xcb_render_picture_t *);
void render_cleanup(void);

#endif
