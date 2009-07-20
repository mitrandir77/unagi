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

#include <xcb/xcb.h>
#include <xcb/composite.h>
#include <xcb/xcb_event.h>

#include "event.h"
#include "structs.h"
#include "util.h"
#include "render.h"
#include "window.h"
#include "atoms.h"

/** Requests label of Composite extension for X error reporting, which
 *  are uniquely  identified according to their  minor opcode starting
 *  from 0 */
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

/** Requests label  of XFixes extension  for X error  reporting, which
 *  are uniquely  identified according to their  minor opcode starting
 *  from 0 */
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

/** Requests label  of Damage extension  for X error  reporting, which
 *  are uniquely  identified according to their  minor opcode starting
 *  from 0 */
static const char *damage_request_label[] = {
  "DamageQueryVersion",
  "DamageCreate",
  "DamageDestroy",
  "DamageSubtract",
  "DamageAdd",
};

/** Error label of XFixes specific error */
static const char *xfixes_error_label = "BadRegion";

/** Error label of Damage specific error */
static const char *damage_error_label = "BadDamage";

/** Get  the  request  label  from  the minor  opcode  if  it  exists,
    otherwise returns NULL */
#define ERROR_EXTENSION_GET_REQUEST_LABEL(labels, minor_code) \
  (minor_code < countof(labels) ? labels[minor_code] : NULL)

/** Get the request label from the major and minor codes of the failed
 *  request.  The  major codes 0 through  127 are reserved  for X core
 *  requests whereas the major codes  128 through 255 are reserved for
 *  X extensions. If this is an  extension, the minor code gives the X
 *  extension request which failed.
 *
 * \param request_major_code The X error request major opcode
 * \param request_minor_code The X error request minor opcode
 * \return The error label associated with the major and minor opcodes
 */
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

/** Handler  for X  errors  after the  initialisation routines.  Every
 *  error includes an  8-bit error code.  Error codes  128 through 255
 *  are reserved for extensions.
 *  
 *  For requests  with side-effects, the  failing resource ID  is also
 *  returned:  Colormap, Cursor,  Drawable, Font,  GContext, IDChoice,
 *  Pixmap,  and  Window.   For  Atom  errors,  the  failing  atom  is
 *  returned.  For Value errors, the failing value is returned.  Other
 *  core  errors return no  additional data.   Unused bytes  within an
 *  error are not guaranteed to be zero.
 *
 * \see error_get_request_label
 * \param error The X error
 */
static int
event_handle_error(void *data __attribute__((unused)),
		   xcb_connection_t *c __attribute__((unused)),
		   xcb_generic_error_t *error)
{
  /* To determine  whether the error comes from  an extension request,
     it use the 'first_error'  field of QueryExtension reply, plus the
     first error code of the extension */
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

  warn("X error: request=%s (major=%ju, minor=%ju, resource=%jx), error=%s",
       error_get_request_label(error->major_code, error->minor_code),
       (uintmax_t) error->major_code, (uintmax_t) error->minor_code,
       (uintmax_t) error->resource_id, error_label);

  return 0;
}

/** Handler for X errors  during initialisation (any error encountered
 *  will exit the program)
 *
 * \see event_handle_error
 * \param error The X error
 */
static int
event_handle_start_error(void *data,
			 xcb_connection_t *c,
			 xcb_generic_error_t *error)
{
  /* If the  redirection of existing windows in  the off-screen buffer
     failed, then it means that another program has already redirected
     the windows, certainly another compositing manager... */
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

/** Initialise error handlers with the given handler function */
#define INIT_ERRORS_HANDLERS(handler)				\
  /* Error codes 128 through 255 are reserved for extensions */	\
  for(int err_num = 0; err_num < 256; err_num++)		\
    xcb_event_set_error_handler(&globalconf.evenths, err_num,	\
				handler, NULL);			\

/** Initialise startup error handlers */
void
event_init_start_handlers(void)
{
  INIT_ERRORS_HANDLERS(event_handle_start_error)
}

/** Handler for DamageNotify events
 *
 * \param event The X DamageNotify event
 */
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

  /* Subtract  the current  window  Damage (e.g.   set  it as  empty),
     otherwise  this window  would  not received  DamageNotify as  the
     window area is now non-empty */
  xcb_damage_subtract(globalconf.connection, window->damage,
		      XCB_NONE, XCB_NONE);

  /* The screen has to be repainted to update the damaged area */
  globalconf.do_repaint = true;
  window->damaged = true;

  return 0;
}

/** Handler for CirculateNotify events  reported when a window changes
 *  its position in the stack (either  Top if the window is now on top
 *  of all siblings or Bottom)
 *
 * \param event The X CirculateNotify event
 */
static int
event_handle_circulate_notify(void *data __attribute__((unused)),
			      xcb_connection_t *c __attribute__((unused)),
			      xcb_circulate_notify_event_t *event)
{
  debug("CirculateNotify: event=%jx, window=%jx",
	(uintmax_t) event->event, (uintmax_t) event->window);

  window_t *window = window_list_get(event->window);

  /* Above window  of None means that  the window is  placed below all
     its siblings */
  if(event->place == XCB_PLACE_ON_BOTTOM)
    window_restack(window, XCB_NONE);
  else
    {
      /* Get the identifier of the topmost window of the stack */
      window_t *windows_tail;
      for(windows_tail = globalconf.windows;
	  windows_tail && windows_tail->next;
	  windows_tail = windows_tail->next)
	;

      window_restack(window, windows_tail->id);
    }

  return 0;
}

/** Handler for ConfigureNotify events reported when a windows changes
 *  its size, position and/or position in the stack
 *
 * \param event The X ConfigureNotify event
 */
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

  /* If  this is  the root  window, then  just create  again  the root
     background picture */
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

  /* Invalidate  Pixmap and  Picture if  the window  has  been resized
     because  a  new  pixmap  is  allocated everytime  the  window  is
     resized */
  if(window->geometry->width != event->width ||
     window->geometry->height != event->height ||
     window->geometry->border_width != event->border_width)
    window_free_pixmap(window);

  /* Update size and border width */
  window->geometry->width = event->width;
  window->geometry->height = event->height;
  window->geometry->border_width = event->border_width;
  window->attributes->override_redirect = event->override_redirect;

  /* Restack the window */
  window_restack(window, event->above_sibling);

  return 0;
}

/** Handler  for  CreateNotify  event  reported  when  a  CreateWindow
 *  request is issued. It's worth noticing that this request specifies
 *  the new window  geometry but some programs such  as xterm create a
 *  1x1 window and then issue a ConfigureNotify
 *
 * \param event The X CreateNotify event
 */
static int
event_handle_create_notify(void *data __attribute__((unused)),
			   xcb_connection_t *c __attribute__((unused)),
			   xcb_create_notify_event_t *event)
{
  debug("CreateNotify: parent=%jx, window=%jx (%jux%ju +%jd +%jd)",
	(uintmax_t) event->parent, (uintmax_t) event->window,
	(uintmax_t) event->width, (uintmax_t) event->height,
	(intmax_t) event->x, (intmax_t) event->y);

  /* Add  the  new window  whose  identifier  is  given in  the  event
     itself and  */
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

/** Handler  for  DestroyNotify event  reported  when a  DestroyWindow
 *  request is issued
 *
 * \param event The X DestroyNotify event
 */
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
     been freed automatically in the meantime */
  window->damage = XCB_NONE;

  window_list_remove_window(window);

  return 0;
}

/** Handler for  MapNotify event reported when a  MapWindow request is
 *  issued
 *
 * \param event The X MapNotify event
 */
static int
event_handle_map_notify(void *data __attribute__((unused)),
			xcb_connection_t *c __attribute__((unused)),
			xcb_map_notify_event_t *event)
{
  debug("MapNotify: event=%jx, window=%jx",
	(uintmax_t) event->event, (uintmax_t) event->window);

  window_t *window = window_list_get(event->window);
  window->attributes->map_state = XCB_MAP_STATE_VIEWABLE;

  /* Everytime a window is mapped, a new pixmap is created */
  window_free_pixmap(window);

  /* Get the opacity property  because we don't receive PropertyNotify
     events while the window is not mapped */
  xcb_get_property_cookie_t opacity_cookie = window_get_opacity_property(window->id);
  window_register_property_notify(window);
  window->opacity = window_get_opacity_property_reply(opacity_cookie);

  window->damaged = false;

  return 0;
}

/** Handler  for ReparentNotify event  reported when  a ReparentWindow
 * request is  issued which reparents  the window to new  given parent
 * after unmapping it if it is already mapped (which is then mapped at
 * the end)
 *
 * \param event The X ReparentNotify event
 */
static int
event_handle_reparent_notify(void *data __attribute__((unused)),
			     xcb_connection_t *c __attribute__((unused)),
			     xcb_reparent_notify_event_t *event)
{
  debug("ReparentNotify: event=%jx, window=%jx, parent=%jx",
	(uintmax_t) event->event, (uintmax_t) event->window,
	(uintmax_t) event->parent);

  /* Add the window if it is not already managed */ 
  if(event->parent == globalconf.screen->root ||
     !window_list_get(event->window))
    window_add_one(event->window);
  /* Don't manage the window if the parent is not the root window */
  else
    window_list_remove_window(window_list_get(event->window));

  return 0;
}

/** Handler for UnmapNotify event  reported when a UnmapWindow request
 *  is issued
 *
 * \param event The X UnmapNotify event
 */
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

  /* Update window state */
  window->attributes->map_state = XCB_MAP_STATE_UNMAPPED;

  /* The window is not damaged anymore as it is not visible */
  window->damaged = false;

  return 0;
}

/** Handler  for PropertyNotify event  reported when  a ChangeProperty
 *  request is issued
 *
 * \param event The X PropertyNotify event
 */
static int
event_handle_property_notify(void *data __attribute__((unused)),
			     xcb_connection_t *c __attribute__((unused)),
			     xcb_property_notify_event_t *event)
{
  debug("PropertyNotify: window=%jx, atom=%ju",
	(uintmax_t) event->window, (uintmax_t) event->atom);

  /* Update the opacity atom if any */
  if(event->atom == _NET_WM_WINDOW_OPACITY)
    {
      const uint32_t opacity =
	window_get_opacity_property_reply(window_get_opacity_property(event->window));

      window_t *window = window_list_get(event->window);

      if(opacity != window->opacity)
	{
	  /* Force redraw of the window as the opacity has changed */
	  window->damaged = true;
	  window->opacity = opacity;
	}
    }
  /* If the background image has been updated */
  else if(atoms_is_background_atom(event->atom))
    {
      render_free_picture(&globalconf.root_background_picture);
      render_init_root_background();

      /* Force repaint of the entire screen */
      globalconf.do_repaint = true;
    }

  return 0;
}

/** Initialise errors and events handlers
 *
 * \see display_init_redirect
 */
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

  xcb_event_set_property_notify_handler(&globalconf.evenths,
					event_handle_property_notify, NULL);
}
