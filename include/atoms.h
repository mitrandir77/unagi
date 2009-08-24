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
 *  \brief Atoms management
 */

#ifndef ATOMS_H
#define ATOMS_H

#include <stdbool.h>

#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_ewmh.h>

extern xcb_atom_t _NET_WM_WINDOW_OPACITY;
extern xcb_atom_t _XROOTPMAP_ID;
extern xcb_atom_t _XSETROOT_ID;

extern const xcb_atom_t *background_properties_atoms[];

void atoms_init(void);
bool atoms_init_finalise(void);
bool atoms_is_background_atom(const xcb_atom_t);
void atoms_update_supported(const xcb_property_notify_event_t *);
bool atoms_is_supported(const xcb_atom_t);

#endif
