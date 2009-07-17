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
#include "render.h"

static window_t *
window_list_append(const xcb_window_t new_window_id)
{
  window_t *new_window = calloc(1, sizeof(window_t));

  new_window->id = new_window_id;

  if(globalconf.windows == NULL)
    globalconf.windows = new_window;
  else
    {
      /* Append to the end of the list */
      window_t *window_tail;
      for(window_tail = globalconf.windows; window_tail->next;
	  window_tail = window_tail->next)
	;

      window_tail->next = new_window;
    }

  return new_window;
}

void
window_free_pixmap(window_t *window)
{
  if(window->pixmap)
    {
      xcb_free_pixmap(globalconf.connection, window->pixmap);
      window->pixmap = XCB_NONE;
    }
}

static void
window_list_free_window(window_t *window)
{
  /* Destroy the damage object if any */
  if(window->damage != XCB_NONE)
    {
      xcb_damage_destroy(globalconf.connection, window->damage);
      window->damage = XCB_NONE;
    }

  window_free_pixmap(window);
  render_free_picture(&window->picture);

  free(window->attributes);
  free(window->geometry);
  free(window);
}

void
window_list_remove_window(window_t *window_delete)
{
  if(!globalconf.windows)
    return;

  if(globalconf.windows == window_delete)
    {
      window_t *old_window = globalconf.windows;
      globalconf.windows = globalconf.windows->next;

      window_list_free_window(old_window);
    }
  else
    for(window_t *window = globalconf.windows; window->next; window = window->next)
      if(window->next == window_delete)
	{
	  window->next = window->next->next;
	  window_list_free_window(window_delete);
	  break;
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
      window_list_free_window(window);
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

xcb_get_property_cookie_t
window_get_opacity_property(xcb_window_t window_id)
{
  debug("window_get_opacity_property: window: %x", window_id);

  return xcb_get_property_unchecked(globalconf.connection, 0, window_id,
				    _NET_WM_WINDOW_OPACITY, CARDINAL, 0, 1);
}

uint32_t
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

void
window_register_property_notify(window_t *window)
{
  /* Get transparency messages */
  const uint32_t select_input_val = XCB_EVENT_MASK_PROPERTY_CHANGE;

  xcb_change_window_attributes(globalconf.connection, window->id,
			       XCB_CW_EVENT_MASK, &select_input_val);
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

xcb_pixmap_t
window_get_pixmap(window_t *window)
{
  /* Update the pixmap thanks to CompositeNameWindowPixmap */
  xcb_pixmap_t pixmap = xcb_generate_id(globalconf.connection);

  xcb_composite_name_window_pixmap(globalconf.connection,
				   window->id,
				   pixmap);

  return pixmap;
}

static xcb_get_window_attributes_cookie_t
window_add_xrequests(const xcb_window_t window_id)
{
  return xcb_get_window_attributes_unchecked(globalconf.connection,
					     window_id);
}

static xcb_get_window_attributes_reply_t *
window_add_xrequests_finalise(window_t * const window,
			      const xcb_get_window_attributes_cookie_t attributes_cookie)
{
  window->attributes = xcb_get_window_attributes_reply(globalconf.connection,
						       attributes_cookie,
						       NULL);

  if(!window->attributes)
    {
      warn("GetWindowAttributes failed for window %jx", (uintmax_t) window->id);
      return NULL;
    }

  if(window->attributes->_class == XCB_WINDOW_CLASS_INPUT_ONLY)
    window->damage = XCB_NONE;
  else
    {
      window->damage = xcb_generate_id(globalconf.connection);

      xcb_damage_create(globalconf.connection, window->damage, window->id,
			XCB_DAMAGE_REPORT_LEVEL_NON_EMPTY);
    }

  return window->attributes;
}

void
window_add_all(const int nwindows,
	       const xcb_window_t * const new_windows_id)
{
  xcb_get_window_attributes_cookie_t attributes_cookies[nwindows];
  xcb_get_geometry_cookie_t geometry_cookies[nwindows];

  for(int nwindow = 0; nwindow < nwindows; ++nwindow)
    {
      /* Ignore the CM window */
      if(new_windows_id[nwindow] == globalconf.cm_window)
	continue;

      attributes_cookies[nwindow] =
	window_add_xrequests(new_windows_id[nwindow]);

      /* Only  necessary  when adding  all  the  windows, otherwise  a
	 window  is only  added on  CreateNotify event  which actually
	 contains the window geometry */
      geometry_cookies[nwindow] =
	xcb_get_geometry_unchecked(globalconf.connection,
				   new_windows_id[nwindow]);
    }

  window_t *new_windows[nwindows];
  for(int nwindow = 0; nwindow < nwindows; ++nwindow)
    new_windows[nwindow] = window_list_append(new_windows_id[nwindow]);

  xcb_get_property_cookie_t opacity_cookies[nwindows];
  memset(opacity_cookies, 0, sizeof(xcb_get_property_cookie_t) * (size_t) nwindows);

  for(int nwindow = 0; nwindow < nwindows; ++nwindow)
    {
      /* Ignore the CM window */
      if(new_windows_id[nwindow] == globalconf.cm_window)
	continue;

      if(!window_add_xrequests_finalise(new_windows[nwindow],
					attributes_cookies[nwindow]))
	{
	  window_list_remove_window(new_windows[nwindow]);
	  continue;
	}

      /* The  opacity property is  only meaninful  when the  window is
	 mapped, because when the window is unmapped, we don't receive
	 PropertyNotify */
      if(new_windows[nwindow]->attributes->map_state == XCB_MAP_STATE_VIEWABLE)
	opacity_cookies[nwindow] = window_get_opacity_property(new_windows_id[nwindow]);

      new_windows[nwindow]->geometry =
	xcb_get_geometry_reply(globalconf.connection,
			       geometry_cookies[nwindow],
			       NULL);
    }

  for(int nwindow = 0; nwindow < nwindows; ++nwindow)
    if(opacity_cookies[nwindow].sequence)
      {
	new_windows[nwindow]->opacity =
	  window_get_opacity_property_reply(opacity_cookies[nwindow]);

	window_register_property_notify(new_windows[nwindow]);
      }
}

window_t *
window_add_one(const xcb_window_t new_window_id)
{
  xcb_get_window_attributes_cookie_t attributes_cookie;

  attributes_cookie = window_add_xrequests(new_window_id);

  window_t *new_window = window_list_append(new_window_id);

  if(!window_add_xrequests_finalise(new_window,
				    attributes_cookie))
    {
      window_list_remove_window(new_window);
      return NULL;
    }

  new_window->opacity = OPACITY_OPAQUE;

  return new_window;
}

void
window_restack(window_t *window, xcb_window_t window_new_above_id)
{
  assert(globalconf.windows);
  assert(window);

  /* Remove the window from the list */
  if(globalconf.windows == window)
    globalconf.windows = window->next;
  else
    {
      window_t *old_window_below;
      for(old_window_below = globalconf.windows;
	  old_window_below && old_window_below->next != window;
	  old_window_below = old_window_below->next)
	;

      old_window_below->next = window->next;
    }

  /* If the  window is on the bottom  of the stack, then  insert it at
     the beginning of the windows list */
  if(window_new_above_id == XCB_NONE)
    {
      window->next = globalconf.windows;
      globalconf.windows = window;
    }
  /* Otherwise insert it before the above window */
  else
    {
      window_t *window_below;
      for(window_below = globalconf.windows;
	  window_below->next && window_below->id != window_new_above_id;
	  window_below = window_below->next)
	;

      window->next = window_below->next;
      window_below->next = window;
    }
}
