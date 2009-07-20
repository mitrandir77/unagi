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

/** Structure   holding   cookies   for   QueryVersion   requests   of
    extensions */
typedef struct {
  xcb_xfixes_query_version_cookie_t xfixes;
  xcb_damage_query_version_cookie_t damage;
  xcb_composite_query_version_cookie_t composite;
}  init_extensions_cookies_t;

/** NOTICE:  All above  variables are  not thread-safe,  but  well, we
    don't care as they are only used during initialisation */

/** Initialise the  QueryVersion extensions cookies with  a 0 sequence
    number, this  is not thread-safe but  we don't care here  as it is
    only used during initialisation */
static init_extensions_cookies_t _init_extensions_cookies = { { 0 }, { 0 }, { 0 } };

/** Cookie request used when acquiring ownership on _NET_WM_CM_Sn */
static xcb_get_selection_owner_cookie_t _get_wm_cm_owner_cookie = { 0 };

/** Cookie  when querying  the  windows tree  starting  from the  root
    window */
static xcb_query_tree_cookie_t _query_tree_cookie = { 0 };

/** Check  whether  the  needed   X  extensions  are  present  on  the
 *  server-side (all the data  have been previously pre-fetched in the
 *  extension  cache). Then send  requests to  check their  version by
 *  sending  QueryVersion  requests which  is  compulsory because  the
 *  client  MUST  negotiate  the   version  of  the  extension  before
 *  executing extension requests
 */
void
display_init_extensions(void)
{
  globalconf.extensions.composite = xcb_get_extension_data(globalconf.connection,
							   &xcb_composite_id);

  globalconf.extensions.xfixes = xcb_get_extension_data(globalconf.connection,
							&xcb_xfixes_id);

  globalconf.extensions.damage = xcb_get_extension_data(globalconf.connection,
							&xcb_damage_id);

  if(!globalconf.extensions.composite ||
     !globalconf.extensions.composite->present)
    fatal("No Composite extension");

  debug("Composite: major_opcode=%ju",
	(uintmax_t) globalconf.extensions.composite->major_opcode);

  if(!globalconf.extensions.xfixes ||
     !globalconf.extensions.xfixes->present)
    fatal("No XFixes extension");

  debug("XFixes: major_opcode=%ju",
	(uintmax_t) globalconf.extensions.xfixes->major_opcode);

  if(!globalconf.extensions.damage ||
     !globalconf.extensions.damage->present)
    fatal("No Damage extension");

  debug("Damage: major_opcode=%ju",
	(uintmax_t) globalconf.extensions.damage->major_opcode);

  _init_extensions_cookies.composite =
    xcb_composite_query_version_unchecked(globalconf.connection,
					  XCB_COMPOSITE_MAJOR_VERSION,
					  XCB_COMPOSITE_MINOR_VERSION);

  _init_extensions_cookies.damage =
    xcb_damage_query_version_unchecked(globalconf.connection,
				       XCB_DAMAGE_MAJOR_VERSION,
				       XCB_DAMAGE_MINOR_VERSION);

  _init_extensions_cookies.xfixes =
    xcb_xfixes_query_version_unchecked(globalconf.connection,
				       XCB_XFIXES_MAJOR_VERSION,
				       XCB_XFIXES_MINOR_VERSION);
}

/** Get the  replies of the QueryVersion requests  previously sent and
 * check if their version actually matched the versions needed
 *
 * \see display_init_extensions
 */
void
display_init_extensions_finalise(void)
{
  assert(_init_extensions_cookies.composite.sequence);

  xcb_composite_query_version_reply_t *composite_version_reply =
    xcb_composite_query_version_reply(globalconf.connection,
				      _init_extensions_cookies.composite,
				      NULL);

  /* Need NameWindowPixmap support introduced in version >= 0.2 */
  if(!composite_version_reply || composite_version_reply->minor_version < 2)
    {
      free(composite_version_reply);
      fatal("Need Composite extension 0.2 at least");
    }

  free(composite_version_reply);

  assert(_init_extensions_cookies.damage.sequence);

  xcb_damage_query_version_reply_t *damage_version_reply = 
    xcb_damage_query_version_reply(globalconf.connection,
				   _init_extensions_cookies.damage,
				   NULL);

  if(!damage_version_reply)
    fatal("Can't initialise Damage extension");

  free(damage_version_reply);

  assert(_init_extensions_cookies.xfixes.sequence);

  xcb_xfixes_query_version_reply_t *xfixes_version_reply =
    xcb_xfixes_query_version_reply(globalconf.connection,
				  _init_extensions_cookies.xfixes,
				  NULL);

  /* Need Region objects support introduced in version >= 2.0 */
  if(!xfixes_version_reply || xfixes_version_reply->major_version < 2)
    {
      free(xfixes_version_reply);
      fatal("Need XFixes extension 2.0 at least");
    }

  free(xfixes_version_reply);
}

/** Handler for  PropertyNotify event meaningful to  set the timestamp
 *  (given  in  the PropertyNotify  event  field)  when acquiring  the
 *  ownership of _NET_WM_CM_Sn using SetOwner request (as specified in
 *  ICCCM and EWMH)
 *
 * \see display_register_cm
 * \param event The X PropertyNotify event
 */
static int
display_event_set_owner_property(void *data __attribute__((unused)),
				 xcb_connection_t *c __attribute__((unused)),
				 xcb_property_notify_event_t *event)
{
  debug("Set _NET_WM_CM_Sn ownership");

  /* Set ownership on _NET_WM_CM_Sn giving the Compositing Manager window */
  xcb_ewmh_set_wm_cm_owner(globalconf.connection, globalconf.cm_window,
			   event->time, 0, 0);

  /* Send request to check whether the ownership succeeded */
  _get_wm_cm_owner_cookie = xcb_ewmh_get_wm_cm_owner_unchecked(globalconf.connection);

  return 0;
}

/** Register  Compositing   Manager,  e.g.   set   ownership  on  EMWH
 *  _NET_WM_CM_Sn  atom used  to politely  stating that  a Compositing
 *  Manager is  currently running. Acquiring ownership is  done in the
 *  following  steps  (ICCCM  explains  the  principles  of  selection
 *  ownership):
 *
 *  0/ Check  whether this  selection  is  already  owned by  another
 *     program
 *
 *  1/ Create  a Window whose  identifier is set as  the _NET_WM_CM_Sn
 *     value
 *
 *  2/ Change a Window  property to  generate a  PropertyNotify event
 *     used as the timestamp  to SetOwner request as multiple attempts
 *     may be sent at the same time
 *
 *  3/ Send SetOwner request
 *
 *  4/ Check whether the SetOwner request succeeds
 */
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

/** Finish  acquiring  ownership  by  checking  whether  the  SetOwner
 *  request succeeded
 *
 * \see display_register_cm
 * \return bool true if it succeeded, false otherwise
 */
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

/** Redirect all  the windows to  the off-screen buffer  starting from
 *  the  root window  and change  root window  attributes to  make the
 *  server reporting meaningful events
 */
void
display_init_redirect(void)
{
  /* Manage all children windows from the root window */
  _query_tree_cookie = xcb_query_tree_unchecked(globalconf.connection,
						globalconf.screen->root);

  xcb_composite_redirect_subwindows(globalconf.connection,
				    globalconf.screen->root,
				    XCB_COMPOSITE_REDIRECT_MANUAL);

  /* Declare interest in meaningful events */
  const uint32_t select_input_val =
    XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
    XCB_EVENT_MASK_STRUCTURE_NOTIFY |
    XCB_EVENT_MASK_PROPERTY_CHANGE;

  xcb_change_window_attributes(globalconf.connection, globalconf.screen->root,
			       XCB_CW_EVENT_MASK, &select_input_val);
}

/** Finish  redirection by  adding  all the  existing  windows in  the
 *  hierarchy
 */
void
display_init_redirect_finalise(void)
{
  assert(_query_tree_cookie.sequence);

  /* Get all the windows below the root window */
  xcb_query_tree_reply_t *query_tree_reply =
    xcb_query_tree_reply(globalconf.connection,
			 _query_tree_cookie,
			 NULL);

  /* Add all these windows excluding the root window of course */
  window_add_all(xcb_query_tree_children_length(query_tree_reply),
		 xcb_query_tree_children(query_tree_reply));

  free(query_tree_reply);
}
