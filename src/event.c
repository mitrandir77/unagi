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

#include <stdio.h>
#include <stdlib.h>

#include <xcb/xcb.h>
#include <xcb/shape.h>
#include <xcb/composite.h>
#include <xcb/xcb_event.h>

#include "structs.h"
#include "event.h"
#include "util.h"
#include "render.h"
#include "window.h"
#include "atoms.h"

static const char *composite_request_label[] = {
  "CompositeQueryVersion",
  "CompositeRedirectWindow",
  "CompositeRedirectSubwindows",
  "CompositeUnredirectWindow",
  "CompositeUnredirectWindows",
  "CompositeCreateRegionFromBorderClip",
  "CompositeNameWindowPixmap",
  "CompositeCompositeGetOverlayWindow",
  "CompositeCompositeReleaseOverlayWindow",
  "CompositeRedirectCoordinate",
  "CompositeTransformCoordinate"
};

static const char *xfixes_request_label[] = {
  "XFixesQueryVersion",
  "XFixesChangeSaveSet",
  "XFixesSelectSelectionInput",
  "XFixesSelectCursorInput",
  "XFixesGetCursorImage",
  "XFixesCreateRegion",
  "XFixesCreateRegionFromBitmap",
  "XFixesCreateRegionFromWindow",
  "XFixesCreateRegionFromGC",
  "XFixesCreateRegionFromPicture",
  "XFixesDestroyRegion",
  "XFixesSetRegion",
  "XFixesCopyRegion",
  "XFixesUnionRegion",
  "XFixesIntersectRegion",
  "XFixesSubtractRegion",
  "XFixesInvertRegion",
  "XFixesTranslateRegion",
  "XFixesRegionExtents",
  "XFixesFetchRegion",
  "XFixesSetGCClipRegion",
  "XFixesSetWindowShapeRegion",
  "XFixesSetPictureClipRegion",
  "XFixesSetCursorName",
  "XFixesGetCursorName",
  "XFixesGetCursorImageAndName",
  "XFixesChangeCursor",
  "XFixesChangeCursorByName",
  "XFixesExpandRegion",
  "XFixesHideCursor",
  "XFixesShowCursor"
};

static const char *damage_request_label[] = {
  "DamageQueryVersion",
  "DamageCreate",
  "DamageDestroy",
  "DamageSubtract",
  "DamageAdd",
};

static const char *xfixes_error_label = "BadRegion";
static const char *damage_error_label = "BadDamage";

#define ERROR_EXTENSION_GET_REQUEST_LABEL(labels, minor_code) \
  (minor_code < countof(labels) ? labels[minor_code] : NULL)

static const char *
error_get_request_label(const uint8_t request_major_code,
			const uint16_t request_minor_code)
{
  if(render_is_render_request(request_major_code))
    return render_error_get_request_label(request_minor_code);

  else if(request_major_code == globalconf.extensions.composite->major_opcode)
    return ERROR_EXTENSION_GET_REQUEST_LABEL(composite_request_label,
					     request_minor_code);

  else if(request_major_code == globalconf.extensions.xfixes->major_opcode)
    return ERROR_EXTENSION_GET_REQUEST_LABEL(xfixes_request_label,
					     request_minor_code);

  else if(request_major_code == globalconf.extensions.damage->major_opcode)
    return ERROR_EXTENSION_GET_REQUEST_LABEL(damage_request_label,
					     request_minor_code);

  else
      return xcb_event_get_request_label(request_major_code);
}

/*
 * Quoted from xproto:
 *
 * Every error includes  an 8-bit error code. Error  codes 128 through
 * 255  are reserved for  extensions.  Every  error also  includes the
 * major  and  minor opcodes  of  the  failed  request and  the  least
 * significant 16 bits of the sequence number of the request.  For the
 * following errors (see  section 4), the failing resource  ID is also
 * returned:  Colormap, Cursor,  Drawable,  Font, GContext,  IDChoice,
 * Pixmap,  and  Window.   For   Atom  errors,  the  failing  atom  is
 * returned. For  Value errors, the  failing value is  returned. Other
 * core  errors return  no additional  data.  Unused  bytes  within an
 * error are not guaranteed to be zero.
 */
static int
event_handle_error(void *data __attribute__((unused)),
		   xcb_connection_t *c __attribute__((unused)),
		   xcb_generic_error_t *error)
{
  const uint8_t xfixes_bad_region =
    globalconf.extensions.xfixes->first_error + XCB_XFIXES_BAD_REGION;

  const uint8_t damage_bad_damage =
    globalconf.extensions.damage->first_error + XCB_DAMAGE_BAD_DAMAGE;

  const char *error_label = render_error_get_error_label(error->error_code);

  if(!error_label)
    {
      if(error->error_code == xfixes_bad_region)
	error_label = xfixes_error_label;
      else if(error->error_code == damage_bad_damage)
	error_label = damage_error_label;
      else
	error_label = xcb_event_get_error_label(error->error_code);
    }

  warn("X error: request=%s (major=%ju, minor=%ju), error=%s",
	error_get_request_label(error->major_code, error->minor_code),
	(uintmax_t) error->major_code, (uintmax_t) error->minor_code,
	error_label);

  return 0;
}

/* static int __attribute__((noreturn)) */
static int
event_handle_start_error(void *data,
			 xcb_connection_t *c,
			 xcb_generic_error_t *error)
{
  if(error->major_code == globalconf.extensions.composite->major_opcode &&
     error->minor_code == XCB_COMPOSITE_REDIRECT_SUBWINDOWS)
    {
      free(error);
      fatal("Another compositing manager is already running");
    }

  event_handle_error(data, c, error);
  free(error);
  fatal("Unexpected X error during startup");

  return 0;
}

#define INIT_ERRORS_HANDLERS(handler)				\
  /* Error codes 128 through 255 are reserved for extensions */	\
  for(int err_num = 0; err_num < 256; err_num++)		\
    xcb_event_set_error_handler(&globalconf.evenths, err_num,	\
				handler, NULL);			\

void
event_init_start_handlers(void)
{
  INIT_ERRORS_HANDLERS(event_handle_start_error)
}

static int
event_handle_damage_notify(void *data __attribute__((unused)),
			   xcb_connection_t *c __attribute__((unused)),
			   xcb_damage_notify_event_t *event)
{
  debug("DamageNotify: area: %jux%ju %+jd %+jd (drawable=%jx,area=%jux%ju +%jd +%jd,geometry=%jux%ju +%jd +%jd)",
	(uintmax_t) event->area.width, (uintmax_t) event->area.height,
	(intmax_t) event->area.x, (intmax_t) event->area.y,
	(uintmax_t) event->drawable,
	(uintmax_t) event->area.width, (uintmax_t) event->area.height,
	(uintmax_t) event->area.x, (uintmax_t) event->area.y,
	(uintmax_t) event->geometry.width, (uintmax_t) event->geometry.height,
	(uintmax_t) event->geometry.x, (uintmax_t) event->geometry.y);

  window_t *window = window_list_get(event->drawable);

  /* The window may have disappeared in the meantime */
  if(!window)
    {
      debug("Window %jx has disappeared", (uintmax_t) event->drawable);
      return 0;
    }

  /* Subtract  the  current window  Damage  (e.g.  set  it as  empty),
     otherwise  this window  would  not received  DamageNotify as  the
     window area is now non-empty */
  xcb_damage_subtract(globalconf.connection, window->damage,
		      XCB_NONE, XCB_NONE);

  globalconf.do_repaint = true;
  window->damaged = true;

  return 0;
}

static int
event_handle_circulate_notify(void *data __attribute__((unused)),
			      xcb_connection_t *c __attribute__((unused)),
			      xcb_circulate_notify_event_t *event)
{
  debug("CirculateNotify: event=%jx, window=%jx",
	(uintmax_t) event->event, (uintmax_t) event->window);

  window_t *window = window_list_get(event->window);

  if(event->place == XCB_PLACE_ON_BOTTOM)
    window_restack(window, XCB_NONE);
  else
    {
      window_t *windows_tail;
      for(windows_tail = globalconf.windows;
	  windows_tail && windows_tail->next;
	  windows_tail = windows_tail->next)
	;

      window_restack(window, windows_tail->id);
    }

  return 0;
}

static int
event_handle_configure_notify(void *data __attribute__((unused)),
			      xcb_connection_t *c __attribute__((unused)),
			      xcb_configure_notify_event_t *event)
{
  debug("ConfigureNotify: event=%jx, window=%jx above=%jx (%jux%ju +%jd +%jd)",
	(uintmax_t) event->event, (uintmax_t) event->window,
	(uintmax_t) event->above_sibling, 
	(uintmax_t) event->width, (uintmax_t) event->height,
	(intmax_t) event->x, (intmax_t) event->y);

  if(event->window == globalconf.screen->root)
    {
      globalconf.screen->width_in_pixels = event->width;
      globalconf.screen->height_in_pixels = event->height;

      render_free_picture(&globalconf.root_background_picture);
      render_init_root_background();

      return 0;
    }

  window_t *window = window_list_get(event->window);
  if(!window)
    {
      debug("No such window %jx", (uintmax_t) event->window);
      return 0;
    }

  /* Update geometry */
  window->geometry->x = event->x;
  window->geometry->y = event->y;

  /* Invalidate Pixmap and Picture if the window has been resized */
  if(window->geometry->width != event->width ||
     window->geometry->height != event->height)
    {
      window_free_pixmap(window);
      render_free_picture(&window->picture);
    }

  window->geometry->width = event->width;
  window->geometry->height = event->height;
  window->geometry->border_width = event->border_width;
  window->attributes->override_redirect = event->override_redirect;

  /* Restack the window */
  window_restack(window, event->above_sibling);

  return 0;
}

static int
event_handle_create_notify(void *data __attribute__((unused)),
			   xcb_connection_t *c __attribute__((unused)),
			   xcb_create_notify_event_t *event)
{
  debug("CreateNotify: parent=%jx, window=%jx (%jux%ju +%jd +%jd)",
	(uintmax_t) event->parent, (uintmax_t) event->window,
	(uintmax_t) event->width, (uintmax_t) event->height,
	(intmax_t) event->x, (intmax_t) event->y);

  window_t *new_window = window_add_one(event->window);
  if(!new_window)
    {
      warn("Can't create window %jx", (uintmax_t) event->window);
      return 0;
    }

  /* No need  to do  a GetGeometry request  as the window  geometry is
     given in the CreateNotify event itself */
  new_window->geometry = calloc(1, sizeof(xcb_get_geometry_reply_t));
  new_window->geometry->x = event->x;
  new_window->geometry->y = event->y;
  new_window->geometry->width = event->width;
  new_window->geometry->height = event->height;
  new_window->geometry->border_width = event->border_width;

  return 0;
}

static int
event_handle_destroy_notify(void *data __attribute__((unused)),
			    xcb_connection_t *c __attribute__((unused)),
			    xcb_destroy_notify_event_t *event)
{
  debug("DestroyNotify: parent=%jx, window=%jx",
	(uintmax_t) event->event, (uintmax_t) event->window);

  window_t *window = window_list_get(event->window);

  if(!window)
    {
      warn("Can't destroy window %jx", (uintmax_t) event->window);
      return 0;
    }

  /* If a DestroyNotify has been received, then the damage object have
     been freed automatically */
  window->damage = XCB_NONE;

  window_list_remove_window(window);

  return 0;
}

static int
event_handle_map_notify(void *data __attribute__((unused)),
			xcb_connection_t *c __attribute__((unused)),
			xcb_map_notify_event_t *event)
{
  debug("MapNotify: event=%jx, window=%jx",
	(uintmax_t) event->event, (uintmax_t) event->window);

  window_t *window = window_list_get(event->window);
  window->attributes->map_state = XCB_MAP_STATE_VIEWABLE;

  xcb_get_property_cookie_t opacity_cookie = window_get_opacity_property(window->id);
  window_register_property_notify(window);
  window->opacity = window_get_opacity_property_reply(opacity_cookie);

  window->damaged = false;

  return 0;
}

static int
event_handle_reparent_notify(void *data __attribute__((unused)),
			     xcb_connection_t *c __attribute__((unused)),
			     xcb_reparent_notify_event_t *event)
{
  debug("ReparentNotify: event=%jx, window=%jx, parent=%jx",
	(uintmax_t) event->event, (uintmax_t) event->window,
	(uintmax_t) event->parent);

  /* TODO */
  if(event->parent == globalconf.screen->root)
    {
      window_t *window = window_add_one(event->window);
      window_register_property_notify(window);
    }
  else
    window_list_remove_window(window_list_get(event->window));

  return 0;
}

static int
event_handle_unmap_notify(void *data __attribute__((unused)),
			  xcb_connection_t *c __attribute__((unused)),
			  xcb_unmap_notify_event_t *event)
{
  debug("UnmapNotify: event=%jx, window=%jx",
	(uintmax_t) event->event, (uintmax_t) event->window);

  window_t *window = window_list_get(event->window);
  if(!window)
    {
      warn("Window %jx disappeared", (uintmax_t) event->window);
      return 0;
    }

  /* TODO */
  window_free_pixmap(window);
  render_free_picture(&window->picture);

  window->attributes->map_state = XCB_MAP_STATE_UNMAPPED;
  window->damaged = false;

  return 0;
}

static int
event_handle_expose(void *data __attribute__((unused)),
		    xcb_connection_t *c __attribute__((unused)),
		    xcb_expose_event_t *event)
{
  debug("Expose: window=%jx", (uintmax_t) event->window);

  return 0;
}

static int
event_handle_property_notify(void *data __attribute__((unused)),
			     xcb_connection_t *c __attribute__((unused)),
			     xcb_property_notify_event_t *event)
{
  debug("PropertyNotify: window=%jx, atom=%ju",
	(uintmax_t) event->window, (uintmax_t) event->atom);

  if(event->atom == _NET_WM_WINDOW_OPACITY)
    {
      window_t *window = window_list_get(event->window);
      window->opacity = window_get_opacity_property_reply(window_get_opacity_property(window->id));
    }
  /* Has the background image been updated */
  else if(atoms_is_background_atom(event->atom))
    {
      render_free_picture(&globalconf.root_background_picture);
      render_init_root_background();
    }

  globalconf.do_repaint = true;

  return 0;
}

void
event_init_handlers(void)
{
  INIT_ERRORS_HANDLERS(event_handle_error)

  xcb_event_set_handler(&globalconf.evenths,
			globalconf.extensions.damage->first_event + XCB_DAMAGE_NOTIFY,
			(xcb_generic_event_handler_t) event_handle_damage_notify,
			NULL);

  xcb_event_set_circulate_notify_handler(&globalconf.evenths,
					 event_handle_circulate_notify, NULL);

  xcb_event_set_configure_notify_handler(&globalconf.evenths,
					 event_handle_configure_notify, NULL);

  xcb_event_set_create_notify_handler(&globalconf.evenths,
				      event_handle_create_notify, NULL);

  xcb_event_set_destroy_notify_handler(&globalconf.evenths,
				       event_handle_destroy_notify, NULL);

  xcb_event_set_map_notify_handler(&globalconf.evenths,
				   event_handle_map_notify, NULL);

  xcb_event_set_reparent_notify_handler(&globalconf.evenths,
					event_handle_reparent_notify, NULL);

  xcb_event_set_unmap_notify_handler(&globalconf.evenths,
				     event_handle_unmap_notify, NULL);

  xcb_event_set_expose_handler(&globalconf.evenths, event_handle_expose, NULL);

  xcb_event_set_property_notify_handler(&globalconf.evenths,
					event_handle_property_notify, NULL);
}
