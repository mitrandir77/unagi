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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include <xcb/xcb_atom.h>
#include <xcb/composite.h>

#include "window.h"
#include "structs.h"
#include "atoms.h"
#include "util.h"

static window_t *
window_list_add(const xcb_window_t new_window_id)
{
  window_t *new_window = calloc(1, sizeof(window_t));

  new_window->id = new_window_id;

  if(globalconf.windows == NULL)
    globalconf.windows = globalconf._windows_tail = new_window;
  else
    {
      globalconf._windows_tail->next = new_window;
      globalconf._windows_tail = new_window;
    }

  return new_window;
}

static inline void
window_free_pixmap(window_t *window)
{
  if(window->pixmap)
    {
      xcb_free_pixmap(globalconf.connection, window->pixmap);
      window->pixmap = XCB_NONE;
    }
}

void
window_list_cleanup(void)
{
  window_t *window = globalconf.windows;
  window_t *window_next;

  while(window != NULL)
    {
      window_next = window->next;

      /* Destroy the damage object if any */
      if(window->damage != XCB_NONE)
	xcb_damage_destroy(globalconf.connection, window->damage);

      window_free_pixmap(window);

      free(window->attributes);
      free(window->geometry);
      free(window);

      window = window_next;
    }
}

window_t *
window_list_get(const xcb_window_t window_id)
{
  window_t *window;

  for(window = globalconf.windows;
      window != NULL && window->id != window_id;
      window = window->next)
    ;

  return window;
}

static xcb_get_property_cookie_t
window_get_opacity_property(xcb_window_t window_id)
{
  debug("window_get_opacity_property: window: %x", window_id);

  return xcb_get_property_unchecked(globalconf.connection, 0, window_id,
				    _NET_WM_WINDOW_OPACITY, CARDINAL, 0, 1);
}

static uint32_t
window_get_opacity_property_reply(xcb_get_property_cookie_t cookie)
{
  xcb_get_property_reply_t *reply = xcb_get_property_reply(globalconf.connection,
							   cookie,
							   NULL);

  uint32_t opacity;

  if(!reply || reply->type != CARDINAL || reply->format != 32 ||
     !xcb_get_property_value_length(reply))
    opacity = OPACITY_OPAQUE;
  else
    opacity = *((uint32_t *) xcb_get_property_value(reply));

  debug("window_get_opacity_property_reply: opacity: %x", opacity);

  free(reply);
  return opacity;
}

/* TODO! */
static xcb_get_property_cookie_t root_background_cookies[2];

/* Get  the root window  background pixmap  whose identifier  is given
   usually by either XROOTPMAP_ID or _XSETROOT_ID */
void
window_get_root_background_pixmap(void)
{
  for(uint8_t background_property_n = 0;
      background_property_n < background_properties_atoms_len;
      background_property_n++)
    {
      root_background_cookies[background_property_n] =
	xcb_get_property_unchecked(globalconf.connection, false,
				   globalconf.screen->root,
				   *background_properties_atoms[background_property_n],
				   XCB_GET_PROPERTY_TYPE_ANY,
				   0, 4);
    }
}

xcb_pixmap_t
window_get_root_background_pixmap_finalise(void)
{
  xcb_pixmap_t root_background_pixmap = XCB_NONE;
  xcb_get_property_reply_t *root_property_reply;

  for(uint8_t background_property_n = 0;
      background_property_n < background_properties_atoms_len;
      background_property_n++)
    {
      assert(root_background_cookies[background_property_n].sequence);

      root_property_reply =
	xcb_get_property_reply(globalconf.connection,
			       root_background_cookies[background_property_n],
			       NULL);

      if(root_property_reply && root_property_reply->type == PIXMAP &&
	 (xcb_get_property_value_length(root_property_reply)) == 4)
	{
	  memcpy(&root_background_pixmap,
		 xcb_get_property_value(root_property_reply), 4);

	  free(root_property_reply);
	  break;
	}

      free(root_property_reply);

      debug("Can't get such property for the root window");
    }

  return root_background_pixmap;
}

xcb_pixmap_t
window_new_root_background_pixmap(void)
{
  xcb_pixmap_t root_pixmap = xcb_generate_id(globalconf.connection);

  xcb_create_pixmap(globalconf.connection, globalconf.screen->root_depth,
		    root_pixmap, globalconf.screen->root, 1, 1);

  return root_pixmap;
}

static void
window_map(window_t *window)
{
  debug("Mapping window %jx", (uintmax_t) window->id);

  /* Free existing window pixmap if any */
  window_free_pixmap(window);

  /* Update the pixmap thanks to CompositeNameWindowPixmap */
  window->pixmap = xcb_generate_id(globalconf.connection);

  xcb_composite_name_window_pixmap(globalconf.connection,
				   window->id,
				   window->pixmap);				   
}

static void
window_add_xrequests(const xcb_window_t window_id,
		     xcb_get_window_attributes_cookie_t *attributes_cookie,
		     xcb_get_geometry_cookie_t *geometry_cookie,
		     xcb_get_property_cookie_t *opacity_cookie)
{
  *attributes_cookie = xcb_get_window_attributes_unchecked(globalconf.connection,
							   window_id);

  *geometry_cookie = xcb_get_geometry_unchecked(globalconf.connection,
						window_id);

  *opacity_cookie = window_get_opacity_property(window_id);
}

static void
window_add_xrequests_finalise(window_t * const window,
			      const xcb_get_window_attributes_cookie_t attributes_cookie,
			      const xcb_get_geometry_cookie_t geometry_cookie,
			      const xcb_get_property_cookie_t opacity_cookie)
{
  window->attributes = xcb_get_window_attributes_reply(globalconf.connection,
						       attributes_cookie,
						       NULL);

  if(window->attributes->_class == XCB_WINDOW_CLASS_INPUT_ONLY)
    window->damage = XCB_NONE;
  else
    {
      window->damage = xcb_generate_id(globalconf.connection);

      xcb_damage_create(globalconf.connection, window->damage, window->id,
			XCB_DAMAGE_REPORT_LEVEL_NON_EMPTY);
    }

  window->geometry = xcb_get_geometry_reply(globalconf.connection,
					    geometry_cookie,
					    NULL);

  window->opacity = window_get_opacity_property_reply(opacity_cookie);

  if(window->attributes->map_state == XCB_MAP_STATE_VIEWABLE)
    window_map(window);
}

void
window_add_all(const int nwindows,
	       const xcb_window_t * const new_windows_id)
{
  xcb_get_window_attributes_cookie_t attributes_cookies[nwindows];
  xcb_get_geometry_cookie_t geometry_cookies[nwindows];
  xcb_get_property_cookie_t opacity_cookies[nwindows];

  for(int nwindow = 0; nwindow < nwindows; ++nwindow)
    window_add_xrequests(new_windows_id[nwindow],
			 &attributes_cookies[nwindow],
			 &geometry_cookies[nwindow],
			 &opacity_cookies[nwindow]);

  window_t *new_windows[nwindows];
  for(int nwindow = 0; nwindow < nwindows; ++nwindow)
    new_windows[nwindow] = window_list_add(new_windows_id[nwindow]);

  for(int nwindow = 0; nwindow < nwindows; ++nwindow)
    window_add_xrequests_finalise(new_windows[nwindow],
				  attributes_cookies[nwindow],
				  geometry_cookies[nwindow],
				  opacity_cookies[nwindow]);
}

void
window_add_one(const xcb_window_t new_window_id)
{
  xcb_get_window_attributes_cookie_t attributes_cookie;
  xcb_get_geometry_cookie_t geometry_cookie;
  xcb_get_property_cookie_t opacity_cookie;

  window_add_xrequests(new_window_id,
		       &attributes_cookie,
		       &geometry_cookie,
		       &opacity_cookie);

  window_t *new_window = window_list_add(new_window_id);

  window_add_xrequests_finalise(new_window,
				attributes_cookie,
				geometry_cookie,
				opacity_cookie);
}
