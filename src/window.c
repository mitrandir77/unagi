/* -*-mode:c;coding:utf-8; c-basic-offset:2;fill-column:70;c-file-style:"gnu"-*-
 *
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/composite.h>

#include <xcb/xcb_aux.h>

#include "window.h"
#include "structs.h"
#include "atoms.h"
#include "util.h"

/** Append a window to the end  of the windows list which is organized
 *  from the bottommost to the topmost window
 *
 * \param new_window_id The new Window X identifier
 * \return The newly allocated window object
 */
static window_t *
window_list_append(const xcb_window_t new_window_id)
{
  window_t *new_window = calloc(1, sizeof(window_t));

  new_window->id = new_window_id;

  /* If the windows list is empty */
  if(globalconf.windows == NULL)
    globalconf.windows = new_window;
  else
    {
      /* Otherwise, append to the end of the list */
      window_t *window_tail;
      for(window_tail = globalconf.windows; window_tail->next;
	  window_tail = window_tail->next)
	;

      window_tail->next = new_window;
    }

  return new_window;
}

/** Get the window object associated with the given Window XID
 *
 * \param window_id The Window XID to look for
 * \return The window object or NULL
 */
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

/** Free a given window and its associated resources
 *
 * \param window The window object to be freed
 */
static void
window_list_free_window(window_t *window)
{
  /* Destroy the damage object if any */
  if(window->damage != XCB_NONE)
    {
      xcb_damage_destroy(globalconf.connection, window->damage);
      window->damage = XCB_NONE;
    }

  if(window->region != XCB_NONE)
    {
      xcb_xfixes_destroy_region(globalconf.connection, window->region);
      window->region = XCB_NONE;
    }

  /* TODO: free plugins memory? */
  window_free_pixmap(window);
  (*globalconf.rendering->free_window)(window);

  free(window->attributes);
  free(window->geometry);
  free(window);
}

/** Remove the given window object from the windows list
 *
 * \param window_delete
 */
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

/** Free all resources allocated for the windows list */
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

/** Free  a  Window Pixmap  which  has  been  previously allocated  by
 *  NameWindowPixmap Composite request
 *
 * \param window The window object to free the Pixmap from
 */
void
window_free_pixmap(window_t *window)
{
  if(window->pixmap)
    {
      xcb_free_pixmap(globalconf.connection, window->pixmap);
      window->pixmap = XCB_NONE;

      /* If the Pixmap  is freed, then free its  associated Picture as
	 it does not make sense to keep it */
      (*globalconf.rendering->free_window_pixmap)(window);
    }
}

/** Send ChangeWindowAttributes request in order to get events related
 *  to a window
 *
 * \param window The window object
 */
void
window_register_notify(const window_t *window)
{
  /* Get transparency notifications */
  const uint32_t select_input_val = XCB_EVENT_MASK_PROPERTY_CHANGE;

  xcb_change_window_attributes(globalconf.connection, window->id,
			       XCB_CW_EVENT_MASK, &select_input_val);
}

/* TODO: Need to figure out a better way to do (not thread-safe!) */
static xcb_get_property_cookie_t root_background_cookies[2];

/* Get  the root window  background pixmap  whose identifier  is given
 * usually by either XROOTPMAP_ID or _XSETROOT_ID Property Atoms
 */
void
window_get_root_background_pixmap(void)
{
  for(uint8_t background_property_n = 0;
      background_properties_atoms[background_property_n];
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

/** Get the  first Property  whose reply is  valid and get  the stored
 *  Pixmap
 *
 * \return The root window background image Pixmap, otherwise None
 */
xcb_pixmap_t
window_get_root_background_pixmap_finalise(void)
{
  xcb_pixmap_t root_background_pixmap = XCB_NONE;
  xcb_get_property_reply_t *root_property_reply;

  for(uint8_t background_property_n = 0;
      background_properties_atoms[background_property_n];
      background_property_n++)
    {
      assert(root_background_cookies[background_property_n].sequence);

      root_property_reply =
	xcb_get_property_reply(globalconf.connection,
			       root_background_cookies[background_property_n],
			       NULL);

      if(root_property_reply && root_property_reply->type == XCB_ATOM_PIXMAP &&
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

/** Create a new Pixmap for the  root Window background if there is no
 *  image set
 *
 * \return The new Pixmap
 */
xcb_pixmap_t
window_new_root_background_pixmap(void)
{
  xcb_pixmap_t root_pixmap = xcb_generate_id(globalconf.connection);

  xcb_create_pixmap(globalconf.connection, globalconf.screen->root_depth,
		    root_pixmap, globalconf.screen->root, 1, 1);

  return root_pixmap;
}

/** Get  the Pixmap  associated with  the  given Window  by sending  a
 *  NameWindowPixmap Composite  request. Must be careful  when to free
 *  this Pixmap, because  a new one is generated  each time the window
 *  is mapped or resized
 *
 * \param window The window object
 * \return The Pixmap associated with the Window
 */
xcb_pixmap_t
window_get_pixmap(const window_t *window)
{
  /* Update the pixmap thanks to CompositeNameWindowPixmap */
  xcb_pixmap_t pixmap = xcb_generate_id(globalconf.connection);

  xcb_composite_name_window_pixmap(globalconf.connection,
				   window->id,
				   pixmap);

  return pixmap;
}

/** No need to include Shape extension header just for that */
#define XCB_SHAPE_SK_BOUNDING 0

/** Get   the  region   of  the   given  Window   and  take   care  of
 *  non-rectangular windows by using CreateRegionFromWindow instead of
 *  Window size and position
 *
 * \param window The window object
 * \return The region associated with the given Window
 */
xcb_xfixes_region_t
window_get_region(window_t *window)
{
  xcb_xfixes_region_t new_region = xcb_generate_id(globalconf.connection);

  xcb_xfixes_create_region_from_window(globalconf.connection,
                                       new_region,
                                       window->id,
                                       XCB_SHAPE_SK_BOUNDING);

  xcb_xfixes_translate_region(globalconf.connection,
                              new_region,
                              (int16_t) (window->geometry->x +
                                         window->geometry->border_width),
                              (int16_t) (window->geometry->y +
                                         window->geometry->border_width));

  debug("Created new region %x from window %x", new_region, window->id);

  return new_region;
}

/** Check whether the window is visible within the screen geometry
 *
 * \param window The window object
 * \return true if the window is visible
 */
bool
window_is_visible(const window_t *window)
{
  return (window->geometry &&
	  window->geometry->x + window->geometry->width >= 1 &&
	  window->geometry->y + window->geometry->height >= 1 &&
	  window->geometry->x < globalconf.screen->width_in_pixels &&
	  window->geometry->y < globalconf.screen->height_in_pixels);
}

/** Send ChangeWindowAttributes  request to set  the override-redirect
 *  flag  on the  given window  to define  whether the  window manager
 *  should take care of the window or not
 *
 * \param window The window object
 * \param do_override_redirect The value to set to override-redirect flag
 */
static inline void
window_set_override_redirect(const window_t *window,
			     const uint32_t do_override_redirect)
{
  xcb_change_window_attributes(globalconf.connection,
			       window->id,
			       XCB_CW_OVERRIDE_REDIRECT,
			       &do_override_redirect);
}

/** Get  the Pixmap associated  with a  previously unmapped  window by
 *  simply mapping  it and setting override-redirect to  true to avoid
 *  the  window manager managing  it anymore.   This function  is only
 *  relevant for plugins which may  want to get the Pixmap of unmapped
 *  windows
 *
 * \param window The window object to get the Pixmap from
 */
void
window_get_invisible_window_pixmap(window_t *window)
{
  if(!window_is_visible(window) || !window->attributes ||
     window->attributes->map_state == XCB_MAP_STATE_VIEWABLE)
    return;

  debug("Getting Pixmap of invisible window %jx", (uintmax_t) window->id);

  if(!window->attributes->override_redirect)
    window_set_override_redirect(window, true);

  xcb_map_window(globalconf.connection, window->id);
}

/** This  function must  be called  on  each window  object which  was
 *  previously  unmapped once  the plugin  is not  enabled  anymore to
 *  restore the state before the plugin was run
 *
 * \param window The window object to act on
 */
void
window_get_invisible_window_pixmap_finalise(window_t *window)
{
  xcb_unmap_window(globalconf.connection, window->id);
  window_set_override_redirect(window, false);
}

/** Send requests when a window is added (CreateNotify or on startup),
 *  e.g. GetWindowAttributes request
 *
 * \param window_id The Window XID
 * \return The cookie associated with the request
 */
static xcb_get_window_attributes_cookie_t
window_add_requests(const xcb_window_t window_id)
{
  return xcb_get_window_attributes_unchecked(globalconf.connection,
					     window_id);
}

/** Get  the GetWindowAttributes  reply and  also associated  a Damage
 *  object  to it and  set the  attributes field  of the  given window
 *  object
 *
 * \param window The window object
 * \param attributes_cookie The cookie associated with the GetWindowAttributes request
 * \return The GetWindowAttributes reply
 */
static xcb_get_window_attributes_reply_t *
window_add_requests_finalise(window_t * const window,
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

  /* No  need to create  a Damage  object for  an InputOnly  window as
     nothing will never be painted in it */
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

/** Manage  all  existing   windows  and  get  information  (geometry,
 *  attributes and opacity). This function is called on startup to add
 *  existing windows
 *
 * \param nwindows The number of windows to add
 * \param new_windows_id The Windows XIDs
 */
void
window_manage_existing(const int nwindows,
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
	window_add_requests(new_windows_id[nwindow]);

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

  for(int nwindow = 0; nwindow < nwindows; ++nwindow)
    {
      /* Ignore the CM window */
      if(new_windows_id[nwindow] == globalconf.cm_window)
	continue;

      if(!window_add_requests_finalise(new_windows[nwindow],
					attributes_cookies[nwindow]))
	{
	  window_list_remove_window(new_windows[nwindow]);
	  continue;
	}

      new_windows[nwindow]->geometry =
	xcb_get_geometry_reply(globalconf.connection,
			       geometry_cookies[nwindow],
			       NULL);

      /* The opacity  property is only  meaningful when the  window is
	 mapped, because when the window is unmapped, we don't receive
	 PropertyNotify */
      if(new_windows[nwindow]->attributes->map_state == XCB_MAP_STATE_VIEWABLE &&
         window_is_visible(new_windows[nwindow]))
	{
	  window_register_notify(new_windows[nwindow]);
	  new_windows[nwindow]->pixmap = window_get_pixmap(new_windows[nwindow]);

          /* Get the Window Region as  well, this is also performed in
             CreateNotify   and   ConfigureNotify   handler  for   new
             Windows */
          new_windows[nwindow]->region = window_get_region(new_windows[nwindow]);
	}
    }

  for(plugin_t *plugin = globalconf.plugins; plugin; plugin = plugin->next)
    if(plugin->vtable->window_manage_existing)
      (*plugin->vtable->window_manage_existing)(nwindows, new_windows);
}

/** Add  the  given   window  to  the  windows  list   and  also  send
 *  GetWindowAttributes request  (no need  to split this  function for
 *  now)
 *
 * \param new_window_id The new Window XID
 * \return The new window object
 */
window_t *
window_add(const xcb_window_t new_window_id)
{
  xcb_get_window_attributes_cookie_t attributes_cookie =
    window_add_requests(new_window_id);

  window_t *new_window = window_list_append(new_window_id);

  /* The request should never fail... */
  if(!window_add_requests_finalise(new_window,
				    attributes_cookie))
    {
      window_list_remove_window(new_window);
      return NULL;
    }

  return new_window;
}

/** Restack  the given  window object  by placing  it below  the given
 *  window  (e.g. it  simply inserts  before the  given  window object
 *  _before_ the new above window)
 *
 * \param window The window object to restack
 * \param window_new_above_id The window which is going to above
 * \todo optimization
 */
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

/** Paint all windows  on the screen by calling  the rendering backend
 *  hooks (not all windows may be painted though)
 *
 * \param windows The list of windows currently managed
 */
void
window_paint_all(window_t *windows)
{
  (*globalconf.rendering->paint_background)();

  for(window_t *window = windows; window; window = window->next)
    {
      if(!window->damaged || !window_is_visible(window))
	{
	  debug("Ignoring window %jx", (uintmax_t) window->id);
	  continue;
	}

      debug("Painting window %jx", (uintmax_t) window->id);
      (*globalconf.rendering->paint_window)(window);
    }

  (*globalconf.rendering->paint_all)();
  xcb_aux_sync(globalconf.connection);
}
