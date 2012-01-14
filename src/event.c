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
 *  \brief X events management
 */

#include <stdlib.h>

#include <xcb/xcb.h>
#include <xcb/composite.h>
#include <xcb/xcb_event.h>

#include "event.h"
#include "structs.h"
#include "util.h"
#include "window.h"
#include "atoms.h"
#include "key.h"

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
  if((*globalconf.rendering->is_request)(request_major_code))
    return (*globalconf.rendering->get_request_label)(request_minor_code);

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

/** Handler for X  errors.  Every error includes an  8-bit error code.
 *  Error codes 128 through 255 are reserved for extensions.
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
static void
event_handle_error(xcb_generic_error_t *error)
{
  /* To determine  whether the error comes from  an extension request,
     it use the 'first_error'  field of QueryExtension reply, plus the
     first error code of the extension */
  const uint8_t xfixes_bad_region =
    globalconf.extensions.xfixes->first_error + XCB_XFIXES_BAD_REGION;

  const uint8_t damage_bad_damage =
    globalconf.extensions.damage->first_error + XCB_DAMAGE_BAD_DAMAGE;

  const char *error_label = (*globalconf.rendering->get_error_label)(error->error_code);

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
}

/** Handler for X events  during initialisation (any error encountered
 *  will exit  the program).
 *
 * \see event_handle_error
 * \see display_event_set_owner_property
 * \param error The X error
 */
void
event_handle_startup(xcb_generic_event_t *event)
{
  switch(XCB_EVENT_RESPONSE_TYPE(event))
    {
    case 0:
      {
	xcb_generic_error_t *error = (xcb_generic_error_t *) event;

	/* If  the redirection  of  existing windows  in the  off-screen
	   buffer failed, then it means that another program has already
	   redirected   the  windows,   certainly  another   compositing
	   manager... */
	if(error->major_code == globalconf.extensions.composite->major_opcode &&
	   error->minor_code == XCB_COMPOSITE_REDIRECT_SUBWINDOWS)
	  {
	    free(error);
	    fatal("Another compositing manager is already running");
	  }

	event_handle_error(error);
	free(error);
	fatal("Unexpected X error during startup");
      }

      break;

    case XCB_PROPERTY_NOTIFY:
      display_event_set_owner_property((void *) event);
      break;
    }
}

/** Handler for DamageNotify events
 *
 * \param event The X DamageNotify event
 */
static void
event_handle_damage_notify(xcb_damage_notify_event_t *event)
{
  debug("DamageNotify: area: %jux%ju %+jd %+jd (drawable=%jx,area=%jux%ju +%jd +%jd,geometry=%jux%ju +%jd +%jd)",
	(uintmax_t) event->area.width, (uintmax_t) event->area.height,
	(intmax_t) event->area.x, (intmax_t) event->area.y,
	(uintmax_t) event->drawable,
	(uintmax_t) event->area.width, (uintmax_t) event->area.height,
	(uintmax_t) event->area.x, (uintmax_t) event->area.y,
	(uintmax_t) event->geometry.width, (uintmax_t) event->geometry.height,
	(uintmax_t) event->geometry.x, (uintmax_t) event->geometry.y);

#ifdef __DEBUG__
  static unsigned int damage_notify_event_counter = 0;
  debug("DamageNotify: COUNT: %u", ++damage_notify_event_counter);
#endif

  window_t *window = window_list_get(event->drawable);

  /* The window may have disappeared in the meantime */
  if(!window)
    {
      debug("Window %jx has disappeared", (uintmax_t) event->drawable);
      return;
    }

  if(!window_is_visible(window))
    {
      debug("Ignore damaged as Window %x is not visible", window->id);
      xcb_damage_subtract(globalconf.connection, window->damage, XCB_NONE,
                          XCB_NONE);

      return;
    }

  xcb_xfixes_region_t damaged_region = XCB_NONE;
  bool is_temporary_region = false;

  /* If the Window has never been  damaged, then it means it has never
     be painted on the screen yet, thus paint its entire content */
  if(!window->damaged)
    {
      damaged_region = window->region;
      window->damaged = true;
      window->fully_damaged = true;

      xcb_damage_subtract(globalconf.connection, window->damage, XCB_NONE,
                          XCB_NONE);
    }
  /* Otherwise, just paint the damaged Region (which may be the entire
     Window or part of it */
  else if(window_is_fully_damaged(window, event))
    xcb_damage_subtract(globalconf.connection, window->damage, XCB_NONE,
                        XCB_NONE);
  else
    {
      damaged_region = xcb_generate_id(globalconf.connection);
      
      xcb_xfixes_create_region(globalconf.connection, damaged_region,
                               0, NULL);

      xcb_damage_subtract(globalconf.connection, window->damage, XCB_NONE,
                          damaged_region);

      xcb_xfixes_translate_region(globalconf.connection, damaged_region,
                                  (int16_t) (window->geometry->x +
                                             window->geometry->border_width),
                                  (int16_t) (window->geometry->y +
                                             window->geometry->border_width));

      is_temporary_region = true;
    }

  display_add_damaged_region(&damaged_region, is_temporary_region);

  PLUGINS_EVENT_HANDLE(event, damage, window);
}

/** Handler for KeyPress events reported once a key is pressed
 *
 * \param event The X KeyPress event
 */
static void
event_handle_key_press(xcb_key_press_event_t *event)
{
  debug("KeyPress: detail=%ju, event=%jx, state=%jx",
	(uintmax_t) event->detail, (uintmax_t) event->event,
	(uintmax_t) event->state);

  PLUGINS_EVENT_HANDLE(event, key_press, window_list_get(event->event));
}

/** Handler for KeyRelease events reported once a key is released
 *
 * \param event The X KeyRelease event
 */
static void
event_handle_key_release(xcb_key_release_event_t *event)
{
  debug("KeyRelease: detail=%ju, event=%jx, state=%jx",
	(uintmax_t) event->detail, (uintmax_t) event->event,
	(uintmax_t) event->state);

  PLUGINS_EVENT_HANDLE(event, key_release, window_list_get(event->event));
}

static void
event_handle_button_release(xcb_button_release_event_t *event)
{
  debug("ButtonRelease: detail=%ju, event=%jx, state=%jx",
	(uintmax_t) event->detail, (uintmax_t) event->event,
	(uintmax_t) event->state);

  PLUGINS_EVENT_HANDLE(event, button_release, window_list_get(event->event));
}

/** Handler for CirculateNotify events  reported when a window changes
 *  its position in the stack (either  Top if the window is now on top
 *  of all siblings or Bottom)
 *
 * \param event The X CirculateNotify event
 */
static void
event_handle_circulate_notify(xcb_circulate_notify_event_t *event)
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

  PLUGINS_EVENT_HANDLE(event, circulate, window);
}

/** Handler for ConfigureNotify events reported when a windows changes
 *  its size, position and/or position in the stack
 *
 * \param event The X ConfigureNotify event
 */
static void
event_handle_configure_notify(xcb_configure_notify_event_t *event)
{
  debug("ConfigureNotify: event=%jx, window=%jx above=%jx (%jux%ju +%jd +%jd, "
        "border=%ju)",
	(uintmax_t) event->event, (uintmax_t) event->window,
	(uintmax_t) event->above_sibling, 
	(uintmax_t) event->width, (uintmax_t) event->height,
	(intmax_t) event->x, (intmax_t) event->y,
        (uintmax_t) event->border_width);

  /* If  this is  the root  window, then  just create  again  the root
     background picture */
  if(event->window == globalconf.screen->root)
    {
      globalconf.screen->width_in_pixels = event->width;
      globalconf.screen->height_in_pixels = event->height;

      (*globalconf.rendering->reset_background)();

      return;
    }

  window_t *window = window_list_get(event->window);
  if(!window)
    {
      debug("No such window %jx", (uintmax_t) event->window);
      return;
    }

  /* Add the Window  Region to the damaged region  to clear old window
     position or size and re-create the Window Region as well

     @todo: Perhaps further checks  could be done to avoid re-creating
            the Window Region but would it really change anything from
            a performance POV?
  */
  if(window_is_visible(window))
    {
      display_add_damaged_region(&window->region, true);
      window->fully_damaged = true;
    }

  /* Update geometry */
  window->geometry->x = event->x;
  window->geometry->y = event->y;

  bool update_pixmap = false;

  /* Invalidate  Pixmap and  Picture if  the window  has  been resized
     because  a  new  pixmap  is  allocated everytime  the  window  is
     resized (only meaningful when the window is viewable) */
  if(window->attributes->map_state == XCB_MAP_STATE_VIEWABLE &&
     (window->geometry->width != event->width ||
      window->geometry->height != event->height ||
      window->geometry->border_width != event->border_width))
    update_pixmap = true;

  /* Update size and border width */
  window->geometry->width = event->width;
  window->geometry->height = event->height;
  window->geometry->border_width = event->border_width;
  window->attributes->override_redirect = event->override_redirect;

  if(window_is_visible(window))
    {
      window->region = window_get_region(window, true, false);

      if(update_pixmap)
        {
          window_free_pixmap(window);
          window->pixmap = window_get_pixmap(window);
        }
    }

  /* Restack the window */
  window_restack(window, event->above_sibling);

  PLUGINS_EVENT_HANDLE(event, configure, window);
}

/** Handler  for  CreateNotify  event  reported  when  a  CreateWindow
 *  request is issued. It's worth noticing that this request specifies
 *  the new window  geometry but some programs such  as xterm create a
 *  1x1 window and then issue a ConfigureNotify
 *
 * \param event The X CreateNotify event
 */
static void
event_handle_create_notify(xcb_create_notify_event_t *event)
{
  debug("CreateNotify: parent=%jx, window=%jx (%jux%ju +%jd +%jd, border=%ju)",
	(uintmax_t) event->parent, (uintmax_t) event->window,
	(uintmax_t) event->width, (uintmax_t) event->height,
	(intmax_t) event->x, (intmax_t) event->y,
        (uintmax_t) event->border_width);

  /* Add  the  new window  whose  identifier  is  given in  the  event
     itself and  */
  window_t *new_window = window_add(event->window);
  if(!new_window)
    {
      debug("Cannot create window %jx", (uintmax_t) event->window);
      return;
    }

  /* No need  to do  a GetGeometry request  as the window  geometry is
     given in the CreateNotify event itself */
  new_window->geometry = calloc(1, sizeof(xcb_get_geometry_reply_t));
  new_window->geometry->x = event->x;
  new_window->geometry->y = event->y;
  new_window->geometry->width = event->width;
  new_window->geometry->height = event->height;
  new_window->geometry->border_width = event->border_width;

  if(window_is_visible(new_window))
    /* Create and store the region associated with the window to avoid
       creating regions all the time, this Region will be destroyed
       only upon DestroyNotify or re-created upon ConfigureNotify
    */
    new_window->region = window_get_region(new_window, true, true);

  PLUGINS_EVENT_HANDLE(event, create, new_window);
}

/** Handler  for  DestroyNotify event  reported  when a  DestroyWindow
 *  request is issued
 *
 * \param event The X DestroyNotify event
 */
static void
event_handle_destroy_notify(xcb_destroy_notify_event_t *event)
{
  debug("DestroyNotify: parent=%jx, window=%jx",
	(uintmax_t) event->event, (uintmax_t) event->window);

  window_t *window = window_list_get(event->window);
  if(!window)
    {
      debug("Can't destroy window %jx", (uintmax_t) event->window);
      return;
    }

  /* If a DestroyNotify has been received, then the damage object have
     been freed automatically in the meantime */
  window->damage = XCB_NONE;

  PLUGINS_EVENT_HANDLE(event, destroy, window);

  window_list_remove_window(window);
}

/** Handler for  MapNotify event reported when a  MapWindow request is
 *  issued
 *
 * \param event The X MapNotify event
 */
static void
event_handle_map_notify(xcb_map_notify_event_t *event)
{
  debug("MapNotify: event=%jx, window=%jx",
	(uintmax_t) event->event, (uintmax_t) event->window);

  window_t *window = window_list_get(event->window);
  if(!window)
    {
      debug("Window %jx disappeared", (uintmax_t) event->window);
      return;
    }

  window->attributes->map_state = XCB_MAP_STATE_VIEWABLE;

  if(window_is_visible(window))
    {
      window->region = window_get_region(window, true, true);

      /* Everytime a window is mapped, a new pixmap is created */
      window_free_pixmap(window);
      window->pixmap = window_get_pixmap(window);
    }

  window->damaged = false;

  PLUGINS_EVENT_HANDLE(event, map, window);
}

/** Handler  for ReparentNotify event  reported when  a ReparentWindow
 * request is  issued which reparents  the window to new  given parent
 * after unmapping it if it is already mapped (which is then mapped at
 * the end)
 *
 * \param event The X ReparentNotify event
 */
static void
event_handle_reparent_notify(xcb_reparent_notify_event_t *event)
{
  debug("ReparentNotify: event=%jx, window=%jx, parent=%jx",
	(uintmax_t) event->event, (uintmax_t) event->window,
	(uintmax_t) event->parent);

  window_t *window = window_list_get(event->window);

  /* Add the window if it is not already managed */ 
  if(event->parent == globalconf.screen->root ||
     !window_list_get(event->window))
    window_add(event->window);
  /* Don't manage the window if the parent is not the root window */
  else
    window_list_remove_window(window);

  PLUGINS_EVENT_HANDLE(event, reparent, window);

  return;
}

/** Handler for UnmapNotify event  reported when a UnmapWindow request
 *  is issued
 *
 * \param event The X UnmapNotify event
 */
static void
event_handle_unmap_notify(xcb_unmap_notify_event_t *event)
{
  debug("UnmapNotify: event=%jx, window=%jx",
	(uintmax_t) event->event, (uintmax_t) event->window);

  window_t *window = window_list_get(event->window);
  if(!window)
    {
      warn("Window %jx disappeared", (uintmax_t) event->window);
      return;
    }

  if(window_is_visible(window))
    display_add_damaged_region(&window->region, true);

  /* Update window state */
  window->attributes->map_state = XCB_MAP_STATE_UNMAPPED;

  /* The window is not damaged anymore as it is not visible */
  window->damaged = false;

  PLUGINS_EVENT_HANDLE(event, unmap, window);
}

/** Handler  for PropertyNotify event  reported when  a ChangeProperty
 *  request is issued
 *
 * \param event The X PropertyNotify event
 */
static void
event_handle_property_notify(xcb_property_notify_event_t *event)
{
  debug("PropertyNotify: window=%jx, atom=%ju",
	(uintmax_t) event->window, (uintmax_t) event->atom);

  /* If the background image has been updated */
  if(atoms_is_background_atom(event->atom) &&
     event->window == globalconf.screen->root)
    {
      debug("New background Pixmap set");
      (*globalconf.rendering->reset_background)();
    }

  /* Update _NET_SUPPORTED value */
  if(event->atom == globalconf.ewmh._NET_SUPPORTED)
    atoms_update_supported(event);

  /* As plugins  requirements are  only atoms, if  the plugin  did not
     meet the requirements on startup, it can try again... */
  window_t *window = window_list_get(event->window);

  for(plugin_t *plugin = globalconf.plugins; plugin; plugin = plugin->next)
    if(plugin->vtable->events.property)
      {
	(*plugin->vtable->events.property)(event, window);

	if(!plugin->enable && plugin->vtable->check_requirements)
	  plugin->enable = (*plugin->vtable->check_requirements)();
      }

  return;
}

/** Handler for  Mapping event reported  when the keyboard  mapping is
 *  modified
 *
 * \param event The X Mapping event
 */
static void
event_handle_mapping_notify(xcb_mapping_notify_event_t *event)
{
  debug("MappingNotify: request=%ju, first_keycode=%ju, count=%ju",
	(uintmax_t) event->request, (uintmax_t) event->first_keycode,
	(uintmax_t) event->count);

  if(event->request != XCB_MAPPING_MODIFIER &&
     event->request != XCB_MAPPING_KEYBOARD)
    return;

  xcb_get_modifier_mapping_cookie_t key_mapping_cookie =
    xcb_get_modifier_mapping_unchecked(globalconf.connection);

  xcb_key_symbols_free(globalconf.keysyms);
  globalconf.keysyms = xcb_key_symbols_alloc(globalconf.connection);

  key_lock_mask_get_reply(key_mapping_cookie);
}

/** Initialise errors and events handlers
 *
 * \see display_init_redirect
 */
void
event_handle(xcb_generic_event_t *event)
{
  const uint8_t response_type = XCB_EVENT_RESPONSE_TYPE(event);

  if(response_type == 0)
    {
      event_handle_error((void *) event);
      return;
    }
  else if(response_type == globalconf.extensions.damage->first_event + XCB_DAMAGE_NOTIFY)
    {
      event_handle_damage_notify((void *) event);
      return;
    }

  switch(response_type)
    {
#define EVENT(type, callback) case type: callback((void *) event); return
      EVENT(XCB_KEY_PRESS, event_handle_key_press);
      EVENT(XCB_KEY_RELEASE, event_handle_key_release);
      EVENT(XCB_BUTTON_RELEASE, event_handle_button_release);
      EVENT(XCB_CIRCULATE_NOTIFY, event_handle_circulate_notify);
      EVENT(XCB_CONFIGURE_NOTIFY, event_handle_configure_notify);
      EVENT(XCB_CREATE_NOTIFY, event_handle_create_notify);
      EVENT(XCB_DESTROY_NOTIFY, event_handle_destroy_notify);
      EVENT(XCB_MAP_NOTIFY, event_handle_map_notify);
      EVENT(XCB_REPARENT_NOTIFY, event_handle_reparent_notify);
      EVENT(XCB_UNMAP_NOTIFY, event_handle_unmap_notify);
      EVENT(XCB_PROPERTY_NOTIFY, event_handle_property_notify);
      EVENT(XCB_MAPPING_NOTIFY, event_handle_mapping_notify);
    }
}

/** Handle all events in the queue
 *
 * \param event_handler The event handler function to call for each event
 */
void
event_handle_poll_loop(void (*event_handler)(xcb_generic_event_t *))
{
  xcb_generic_event_t *event;
  while((event = xcb_poll_for_event(globalconf.connection)) != NULL)
    {
      event_handler(event);
      free(event);
    }
}
