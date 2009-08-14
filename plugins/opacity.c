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

/** Opacity plugin:
 *  ===============
 *
 *  This  plugin handles windows  opacity.  It  relies on  a structure
 *  containing,  for   each  mapped  (or   viewable)  'window_t',  its
 *  'opacity' and 'cookie', namely 'opacity_window_t'.
 *
 *  The  cookie  is the  GetProperty  request  sent  on MapNotify  and
 *  PropertyNotify events  and whose reply  is only got  when actually
 *  getting the window opacity (this  allows to take full advantage of
 *  XCB asynchronous model)
 */

#include <assert.h>
#include <stdlib.h>

#include <xcb/xcb.h>
#include <xcb/xcb_event.h>

#include "structs.h"
#include "util.h"
#include "window.h"
#include "atoms.h"

#define OPACITY_OPAQUE 0xffffffff

typedef struct _opacity_window_t
{
  window_t *window;
  xcb_get_property_cookie_t cookie;
  uint32_t opacity;
  struct _opacity_window_t *next;
} opacity_window_t;

opacity_window_t *_opacity_windows = NULL;

/** Send the request to get the _NET_WM_WINDOW_OPACITY Atom of a given
 *  window     as    EWMH     specification     does    not     define
 *  _NET_WM_WINDOW_OPACITY
 *
 * \param window_id The Window XID
 * \return The GetProperty cookie associated with the request
 */
static xcb_get_property_cookie_t
_opacity_get_property(xcb_window_t window_id)
{
  return xcb_get_property_unchecked(globalconf.connection, 0, window_id,
				    _NET_WM_WINDOW_OPACITY, CARDINAL, 0, 1);
}

/** Get the  reply of the previously  sent request to  get the opacity
 *  property of a Window
 *
 * \param cookie The GetProperty request cookie
 * \return The opacity value as 32-bits unsigned integer
 */
static uint32_t
_opacity_get_property_reply(xcb_get_property_cookie_t cookie)
{
  xcb_get_property_reply_t *reply =
    xcb_get_property_reply(globalconf.connection, cookie, NULL);

  uint32_t opacity;

  /* If the reply is not valid  or there was an error, then the window
     is considered as opaque */
  if(!reply || reply->type != CARDINAL || reply->format != 32 ||
     !xcb_get_property_value_length(reply))
    opacity = OPACITY_OPAQUE;
  else
    opacity = *((uint32_t *) xcb_get_property_value(reply));

  debug("window_get_opacity_property_reply: opacity: %x", opacity);

  free(reply);
  return opacity;
}

/** Create a new opacity window specific to this plugin
 *
 * \param window The window to be added
 * \return The newly allocated opacity window
 */
static opacity_window_t *
_opacity_window_new(window_t *window)
{
  opacity_window_t *new_opacity_window = calloc(1, sizeof(opacity_window_t));
  new_opacity_window->window = window;

  /* Consider the window  as opaque by default but  send a GetProperty
     request to get the actual  property value (this way get the reply
     as late as possible) */
  new_opacity_window->cookie = _opacity_get_property(window->id);

  return new_opacity_window;
}

static inline void
_opacity_free_property_reply(opacity_window_t *opacity_window)
{
  if(opacity_window->cookie.sequence != 0)
    free(xcb_get_property_reply(globalconf.connection,
				opacity_window->cookie,
				NULL));  
}

static inline void
_opacity_free_window(opacity_window_t *opacity_window)
{
  _opacity_free_property_reply(opacity_window);
  free(opacity_window);
}

/** Manage existing windows
 *
 * \param nwindows The number of windows to manage
 * \param windows The windows to manage
 */
static void
opacity_window_manage_existing(const int nwindows,
			       window_t **windows)
{
  opacity_window_t *opacity_windows_tail = NULL;

  for(int nwindow = 0; nwindow < nwindows; nwindow++)
    {
      /* Only managed windows which are mapped */
      if(!windows[nwindow]->attributes ||
	 windows[nwindow]->attributes->map_state != XCB_MAP_STATE_VIEWABLE)
	continue;

      debug("Managing window %jx", (uintmax_t) windows[nwindow]->id);

      if(!_opacity_windows)
	{
	  _opacity_windows = _opacity_window_new(windows[nwindow]);
	  opacity_windows_tail = _opacity_windows;
	}
      else
	{
	  opacity_windows_tail->next = _opacity_window_new(windows[nwindow]);
	  opacity_windows_tail = opacity_windows_tail->next;
	}
    }
}

/** Get the window opacity
 *
 * \param window The window object to get opacity from
 * \return The window opacity as a 16-bits digit (ARGB)
 */
static uint16_t
opacity_get_window_opacity(const window_t *window)
{
  opacity_window_t *opacity_window;
  for(opacity_window = _opacity_windows;
      opacity_window && opacity_window->window != window;
      opacity_window = opacity_window->next)
    ;

  /* Can't find this window, maybe  because it comes from a plugin, so
     consider it as opaque */
  if(!opacity_window)
    return UINT16_MAX;

  /* Request the reply for the GetProperty request previously sent */
  if(opacity_window->cookie.sequence != 0)
    {
      opacity_window->opacity = _opacity_get_property_reply(opacity_window->cookie);
      opacity_window->cookie.sequence = 0;
    }

  return (uint16_t) (((double) opacity_window->opacity / OPACITY_OPAQUE) * 0xffff);
}

/** Handler for  MapNotify event. Get the opacity  property because we
 *  don't receive PropertyNotify events while the window is not mapped
 *
 * \param event The MapNotify event
 * \param window The window object
 */
static void
opacity_event_handle_map_notify(xcb_map_notify_event_t *event,
				window_t *window)
{
  debug("MapNotify: event=%jx, window=%jx",
	(uintmax_t) event->event, (uintmax_t) event->window);

  /* There may be no windows mapped yet */
  if(!_opacity_windows)
    _opacity_windows = _opacity_window_new(window);
  else
    {
      for(opacity_window_t *old__opacity_windows_tail = _opacity_windows;;
	  old__opacity_windows_tail = old__opacity_windows_tail->next)
	if(!old__opacity_windows_tail->next)
	  {
	    old__opacity_windows_tail->next = _opacity_window_new(window);
	    break;
	  }
    }

  window_register_notify(window);
}

/** Handler  for PropertyNotify  event which  only send  a GetProperty
 *  request on the given window
 *
 * \param event The PropertyNotify event
 * \param window The window object
 */
static void
opacity_event_handle_property_notify(xcb_property_notify_event_t *event,
				     window_t *window)
{
  /* Update the opacity atom if any */
  if(event->atom != _NET_WM_WINDOW_OPACITY)
    return;

  debug("PropertyNotify: window=%jx, atom=%ju",
	(uintmax_t) event->window, (uintmax_t) event->atom);

  /* Get the corresponding opacity window */
  opacity_window_t *opacity_window;
  for(opacity_window = _opacity_windows;
      opacity_window && opacity_window->window != window;
      opacity_window = opacity_window->next)
    ;

  assert(opacity_window);

  /* Send  a  GetProperty  request  if  the property  value  has  been
     updated, but free existing one if any */
  _opacity_free_property_reply(opacity_window);

  if(event->state == XCB_PROPERTY_NEW_VALUE)
    opacity_window->cookie = _opacity_get_property(window->id);
      
  /* Force redraw of the window as the opacity has changed */
  window->damaged = true;
}

/** Handle  for  UnmapNotify,  only  responsible to  free  the  memory
 *  allocated on MapNotify because  opacity is only relevant to mapped
 *  windows and moreover PropertyNotify  events are not sent while the
 *  window is unmapped
 *
 * \param event The UnmapNotify event
 * \param window The window object
 */
static void
opacity_event_handle_unmap_notify(xcb_unmap_notify_event_t *event __attribute__((unused)),
				  window_t *window)
{
  if(!_opacity_windows)
    return;

  opacity_window_t *old_opacity_window = NULL;

  if(_opacity_windows->window == window)
    {
      old_opacity_window = _opacity_windows;
      _opacity_windows = _opacity_windows->next;
    }
  else
    {
      opacity_window_t *opacity_window;
      for(opacity_window = _opacity_windows;
	  opacity_window->next && opacity_window->next->window != window;
	  opacity_window = opacity_window->next)
	;

      if(!opacity_window->next)
	return;

      old_opacity_window = opacity_window->next;
      opacity_window->next = old_opacity_window->next;
    }

  _opacity_free_window(old_opacity_window);
}

/** Called on dlclose() and free the memory allocated by this plugin */
static void __attribute__((destructor))
opacity_destructor(void)
{
  opacity_window_t *opacity_window = _opacity_windows;
  opacity_window_t *opacity_window_next;

  while(opacity_window != NULL)
    {
      opacity_window_next = opacity_window->next;
      _opacity_free_window(opacity_window);
      opacity_window = opacity_window_next;
    }
}

/** Structure holding all the functions addresses */
plugin_vtable_t plugin_vtable = {
  .name = "opacity",
  .events = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    opacity_event_handle_map_notify,
    NULL,
    opacity_event_handle_unmap_notify,
    opacity_event_handle_property_notify
  },
  .check_requirements = NULL,
  .window_manage_existing = opacity_window_manage_existing,
  .window_get_opacity = opacity_get_window_opacity,
  .render_windows = NULL
};
