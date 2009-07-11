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
#include <string.h>
#include <assert.h>

#include <xcb/composite.h>
#include <xcb/xfixes.h>
#include <xcb/damage.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_event.h>
#include <xcb/xcb_aux.h>

#include "structs.h"
#include "display.h"
#include "atoms.h"
#include "event.h"
#include "window.h"
#include "util.h"

/* TODO: global variable? */
static xcb_xfixes_query_version_cookie_t _xfixes_version_cookie = { 0 };
static xcb_damage_query_version_cookie_t _damage_version_cookie = { 0 };
static xcb_composite_query_version_cookie_t _composite_version_cookie = { 0 };

static xcb_get_selection_owner_cookie_t _get_wm_cm_owner_cookie = { 0 };

bool
display_init_extensions(void)
{
  globalconf.extensions.composite = xcb_get_extension_data(globalconf.connection,
							   &xcb_composite_id);

  globalconf.extensions.xfixes = xcb_get_extension_data(globalconf.connection,
							&xcb_xfixes_id);

  globalconf.extensions.damage = xcb_get_extension_data(globalconf.connection,
							&xcb_damage_id);

  debug("Composite: major_opcode=%ju",
	(uintmax_t) globalconf.extensions.composite->major_opcode);

  debug("XFixes: major_opcode=%ju",
	(uintmax_t) globalconf.extensions.xfixes->major_opcode);

  debug("Damage: major_opcode=%ju",
	(uintmax_t) globalconf.extensions.damage->major_opcode);

  if(!globalconf.extensions.composite ||
     !globalconf.extensions.composite->present)
    {
      fatal("No Composite extension");
      return false;
    }

  if(!globalconf.extensions.xfixes ||
     !globalconf.extensions.xfixes->present)
    {
      fatal("No XFixes extension");
      return false;
    }

  if(!globalconf.extensions.damage ||
     !globalconf.extensions.damage->present)
    {
      fatal("No Damage extension");
      return false;
    }

  _composite_version_cookie =
    xcb_composite_query_version_unchecked(globalconf.connection,
					  XCB_COMPOSITE_MAJOR_VERSION,
					  XCB_COMPOSITE_MINOR_VERSION);

  _damage_version_cookie = xcb_damage_query_version_unchecked(globalconf.connection,
							      XCB_DAMAGE_MAJOR_VERSION,
							      XCB_DAMAGE_MINOR_VERSION),

  _xfixes_version_cookie =
    xcb_xfixes_query_version_unchecked(globalconf.connection,
				       XCB_XFIXES_MAJOR_VERSION,
				       XCB_XFIXES_MINOR_VERSION);

  return true;
}

bool
display_init_extensions_finalise(void)
{
  assert(_composite_version_cookie.sequence);

  xcb_composite_query_version_reply_t *composite_version_reply =
    xcb_composite_query_version_reply(globalconf.connection,
				      _composite_version_cookie,
				      NULL);

  /* NameWindowPixmap support is needed */
  if(!composite_version_reply || composite_version_reply->minor_version < 2)
    {
      free(composite_version_reply);

      fatal("Need Composite extension 0.2 at least");
      return false;
    }

  free(composite_version_reply);

  assert(_damage_version_cookie.sequence);

  xcb_damage_query_version_reply_t *damage_version_reply = 
    xcb_damage_query_version_reply(globalconf.connection,
				   _damage_version_cookie,
				   NULL);

  if(!damage_version_reply)
    {
      fatal("Can't initialise Damage extension");
      return false;
    }

  free(damage_version_reply);

  assert(_xfixes_version_cookie.sequence);

  xcb_xfixes_query_version_reply_t *xfixes_version_reply =
    xcb_xfixes_query_version_reply(globalconf.connection,
				  _xfixes_version_cookie,
				  NULL);

  /* Need Region objects support */
  if(!xfixes_version_reply || xfixes_version_reply->major_version < 2)
    {
      free(xfixes_version_reply);

      fatal("Need XFixes extension 2.0 at least");
      return false;
    }

  free(xfixes_version_reply);

  return true;
}

/* When  changing  the property  of  the  CM  window, it  generates  a
   PropertyNotify event used  to set the time of  the SetOwner request
   for _NET_WM_CM_Sn */
static int
display_event_set_owner_property(void *data __attribute__((unused)),
				 xcb_connection_t *c __attribute__((unused)),
				 xcb_property_notify_event_t *event)
{
  debug("Set _NET_WM_CM_Sn ownership");

  xcb_ewmh_set_wm_cm_owner(globalconf.connection, globalconf.cm_window,
			   event->time, 0, 0);

  _get_wm_cm_owner_cookie = xcb_ewmh_get_wm_cm_owner_unchecked(globalconf.connection);

  return 0;
}

void
display_register_cm(void)
{
  globalconf.cm_window = xcb_generate_id(globalconf.connection);

  /* Create  a  dummy  window  meaningful  to  set  the  ownership  on
     _NET_WM_CM_Sn atom */
  const uint32_t create_win_val[] = { true, XCB_EVENT_MASK_PROPERTY_CHANGE };

  xcb_create_window(globalconf.connection, XCB_COPY_FROM_PARENT,
		    globalconf.cm_window, globalconf.screen->root, 0, 0, 1, 1, 0,
		    XCB_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
		    XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
		    create_win_val);

  xcb_event_set_property_notify_handler(&globalconf.evenths,
					display_event_set_owner_property,
					NULL);

  xcb_change_property(globalconf.connection, XCB_PROP_MODE_REPLACE,
		      globalconf.cm_window,
		      _NET_WM_NAME, UTF8_STRING, 8,
		      strlen(PACKAGE_NAME), PACKAGE_NAME);
}

bool
display_register_cm_finalise(void)
{
  assert(_get_wm_cm_owner_cookie.sequence);

  xcb_window_t wm_cm_owner_win;

  /* Check whether the ownership of WM_CM_Sn succeeded */
  return (xcb_ewmh_get_wm_cm_owner_reply(globalconf.connection, _get_wm_cm_owner_cookie,
					 &wm_cm_owner_win, NULL) &&
	  wm_cm_owner_win == globalconf.cm_window);
}

bool
display_init_redirect(void)
{
  xcb_grab_server(globalconf.connection);

  /* Manage all children windows from the root window */
  xcb_query_tree_cookie_t query_tree_cookie =
    xcb_query_tree_unchecked(globalconf.connection, globalconf.screen->root);

  xcb_composite_redirect_subwindows(globalconf.connection,
				    globalconf.screen->root,
				    XCB_COMPOSITE_REDIRECT_MANUAL);

  /* Declare interest in meaningful events */
  const uint32_t select_input_val =
    XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
    XCB_EVENT_MASK_EXPOSURE |
    XCB_EVENT_MASK_STRUCTURE_NOTIFY |
    XCB_EVENT_MASK_PROPERTY_CHANGE;

  xcb_change_window_attributes(globalconf.connection, globalconf.screen->root,
			       XCB_CW_EVENT_MASK, &select_input_val);

  /* Get all the windows below the root window */
  xcb_query_tree_reply_t *query_tree_reply =
    xcb_query_tree_reply(globalconf.connection,
			 query_tree_cookie,
			 NULL);

  /* Ignore the CM window which is the topmost one */
  window_add_all(xcb_query_tree_children_length(query_tree_reply) - 1,
		 xcb_query_tree_children(query_tree_reply));

  xcb_ungrab_server(globalconf.connection);  

  free(query_tree_reply);

  return true;
}
