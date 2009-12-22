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

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>

#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>

#include "atoms.h"
#include "util.h"
#include "structs.h"

/** Atoms used but not defined in either ICCCM and EWMH */
xcb_atom_t _NET_WM_WINDOW_OPACITY;
xcb_atom_t _XROOTPMAP_ID;
xcb_atom_t _XSETROOT_ID;

/** Structure defined on purpose to be able to send all the InternAtom
    requests */
typedef struct {
  /** Pointer to the atom XID declared above */
  xcb_atom_t *value;
  /** InternAtom request cookie */
  xcb_intern_atom_cookie_t cookie;
  /** Length of the Atom string name */
  uint8_t name_len;
  /** Atom string name */
  char *name;
} atom_t;

/** Prepare  InternAtom  request  (EWMH  atoms are  initialised  using
    xcb-util/ewmh library) */
static atom_t atoms_list[] = {
  { &_NET_WM_WINDOW_OPACITY, { 0 }, sizeof("_NET_WM_WINDOW_OPACITY") - 1, "_NET_WM_WINDOW_OPACITY" },
  { &_XROOTPMAP_ID, { 0 }, sizeof("_XROOTPMAP_ID") - 1, "_XROOTPMAP_ID" },
  { &_XSETROOT_ID, { 0 }, sizeof("_XSETROOT_ID") - 1, "_XSETROOT_ID" }
};

static const ssize_t atoms_list_len = countof(atoms_list);

/** Array containing  pointers to  background image property  atoms of
 *  the  root window, the  value of  these atoms  are the  Pixmap XID,
 *  these  atoms  are  not  standardized  but commonly  used  in  most
 *  software responsible to set the root window background Pixmap
 */
const xcb_atom_t *background_properties_atoms[] = {
  &_XROOTPMAP_ID,
  &_XSETROOT_ID,
  NULL
};

/** Send InternAtom requests to get the Atoms X identifiers
 *
 * @return The EWMH InternAtom cookies
 */
xcb_intern_atom_cookie_t *
atoms_init(void)
{
  for(int atom_n = 0; atom_n < atoms_list_len; atom_n++)
    atoms_list[atom_n].cookie = xcb_intern_atom_unchecked(globalconf.connection,
							  false,
							  atoms_list[atom_n].name_len,
							  atoms_list[atom_n].name);

  return xcb_ewmh_init_atoms(globalconf.connection, &globalconf.ewmh);
}

/** Get  replies  to  the  previously sent  InternAtom  request.  This
 * function is  not thread-safe but we  don't care as it  is only used
 * during initialisation
 *
 * \return true on success, false otherwise
 */
bool
atoms_init_finalise(xcb_intern_atom_cookie_t *ewmh_cookies)
{
  if(!xcb_ewmh_init_atoms_replies(&globalconf.ewmh, ewmh_cookies, NULL))
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

  globalconf.atoms_supported.cookie =
    xcb_ewmh_get_supported_unchecked(&globalconf.ewmh,
                                     globalconf.screen_nbr);

  return true;

 init_atoms_error: 
  fatal("Cannot initialise atoms");
  return false;
}

/** Check  whether  the given  Atom  is  actually  used to  store  the
 *  background Pixmap XID
 *
 * \param atom The atom
 * \return Return  true if  the given Atom  is actually used  to store
 *         background image Pixmap, false otherwise
 */
bool
atoms_is_background_atom(const xcb_atom_t atom)
{
  for(int atom_n = 0; background_properties_atoms[atom_n]; atom_n++)
    if(atom == *background_properties_atoms[atom_n])
      return true;

  return false;
}

/** On  receiving a  X  PropertyNotify for  _NET_SUPPORTED, its  value
 *  should be updated accordingly
 *
 * \param event The X PropertyNotify event received
 */
void
atoms_update_supported(const xcb_property_notify_event_t *event)
{
  if(globalconf.atoms_supported.initialised)
    {
      xcb_ewmh_get_atoms_reply_wipe(&globalconf.atoms_supported.value);
      globalconf.atoms_supported.initialised = false;
      globalconf.atoms_supported.cookie.sequence = 0;
    }

  if(event->state == XCB_PROPERTY_NEW_VALUE)
    globalconf.atoms_supported.cookie =
      xcb_ewmh_get_supported_unchecked(&globalconf.ewmh, globalconf.screen_nbr);
}

/** Check whether the  given atom is actually supported  by the window
 *  manager  thanks to  _NET_SUPPORTED  atom keeps  up-to-date by  the
 *  window manager itself
 *
 * \param atom The Atom to look for
 * \return true if the Atom is supported
 */
bool
atoms_is_supported(const xcb_atom_t atom)
{
  /* Get the _NET_SUPPORTED  reply if a request has  been sent but not
     already processed */
  if(globalconf.atoms_supported.cookie.sequence != 0)
    {
      /* Free existing value if needed */
      if(globalconf.atoms_supported.initialised)
        {
          globalconf.atoms_supported.initialised = false;
          xcb_ewmh_get_atoms_reply_wipe(&globalconf.atoms_supported.value);
        }

      if(!xcb_ewmh_get_supported_reply(&globalconf.ewmh,
				       globalconf.atoms_supported.cookie,
				       &globalconf.atoms_supported.value,
				       NULL))
	return false;

      globalconf.atoms_supported.initialised = true;
    }
  else if(!globalconf.atoms_supported.initialised)
    return false;

  for(uint32_t atom_n = 0; atom_n < globalconf.atoms_supported.value.atoms_len; atom_n++)
    if(globalconf.atoms_supported.value.atoms[atom_n] == atom)
      return true;

  return false;
}
