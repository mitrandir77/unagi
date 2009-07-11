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

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>

#include "atoms.h"
#include "util.h"
#include "structs.h"

typedef struct {
  xcb_atom_t *value;
  xcb_intern_atom_cookie_t cookie;
  uint8_t name_len;
  char *name;
} atom_t;

/* TODO: generate automatically like in xcb-util/ewmh */
xcb_atom_t _NET_WM_WINDOW_OPACITY;
xcb_atom_t _XROOTPMAP_ID;
xcb_atom_t _XSETROOT_ID;

static atom_t atoms_list[] = {
  { &_NET_WM_WINDOW_OPACITY, { 0 }, sizeof("_NET_WM_WINDOW_OPACITY") - 1, "_NET_WM_WINDOW_OPACITY" },
  { &_XROOTPMAP_ID, { 0 }, sizeof("_XROOTPMAP_ID") - 1, "_XROOTPMAP_ID" },
  { &_XSETROOT_ID, { 0 }, sizeof("_XSETROOT_ID") - 1, "_XSETROOT_ID" }
};

static ssize_t atoms_list_len = countof(atoms_list);

const xcb_atom_t *background_properties_atoms[] = {
  &_XROOTPMAP_ID,
  &_XSETROOT_ID
};

const uint8_t background_properties_atoms_len = countof(background_properties_atoms);

void
atoms_init(void)
{
  xcb_ewmh_init_atoms_list(globalconf.connection, globalconf.screen_nbr);

  for(int atom_n = 0; atom_n < atoms_list_len; atom_n++)
    atoms_list[atom_n].cookie = xcb_intern_atom_unchecked(globalconf.connection,
							  false,
							  atoms_list[atom_n].name_len,
							  atoms_list[atom_n].name);
}

bool
atoms_init_finalise(void)
{
  if(!xcb_ewmh_init_atoms_list_replies(globalconf.connection, NULL))
    goto init_atoms_error;

  xcb_intern_atom_reply_t *atom_reply;

  for(int atom_n = 0; atom_n < atoms_list_len; atom_n++)
    {
      assert(atoms_list[atom_n].cookie.sequence);

      atom_reply = xcb_intern_atom_reply(globalconf.connection,
					 atoms_list[atom_n].cookie,
					 NULL);

      if(!atom_reply)
	goto init_atoms_error;

      *(atoms_list[atom_n].value) = atom_reply->atom;
      free(atom_reply);
    }

  return true;

 init_atoms_error: 
  fatal("Cannot initialise atoms");
  return false;
}
