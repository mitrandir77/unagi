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
 *  \brief Display management run on startup
 */

#ifndef INIT_DISPLAY_H
#define INIT_DISPLAY_H

#include <stdbool.h>

#include <xcb/xcb.h>

void display_init_event_handlers(void);

void display_init_extensions(void);
void display_init_extensions_finalise(void);

void display_register_cm(void);
bool display_register_cm_finalise(void);

void display_init_atoms(void);
bool display_init_atoms_finalise(void);

void display_init_redirect(void);
void display_init_redirect_finalise(void);

#endif
