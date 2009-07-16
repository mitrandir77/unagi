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
#include <stdio.h>
#include <stdbool.h>
#include <error.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/composite.h>
#include <xcb/xfixes.h>
#include <xcb/damage.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_event.h>

#include "structs.h"
#include "display.h"
#include "event.h"
#include "render.h"
#include "atoms.h"
#include "util.h"

#include "experiment.h"

conf_t globalconf;

static void
exit_cleanup(void)
{
  debug("Cleaning resources up");

  /* Destroy CM window, thus giving up _NET_WM_CM_Sn ownership */
  if(globalconf.cm_window != XCB_NONE)
    xcb_destroy_window(globalconf.connection, globalconf.cm_window);

  /* Destroy the linked-list of windows */
  window_list_cleanup();

  /* Free render-related resources */
  render_cleanup();

  xcb_disconnect(globalconf.connection);
}

static void
exit_on_signal(int sig __attribute__((unused)))
{
  exit_cleanup();
  _exit(EXIT_FAILURE);
}

int
main(void)
{
  memset(&globalconf, 0, sizeof(globalconf));

  globalconf.connection = xcb_connect(NULL, &globalconf.screen_nbr);
  if(xcb_connection_has_error(globalconf.connection))
    fatal("Cannot open display");

  /* Get the root window */
  globalconf.screen = xcb_aux_get_screen(globalconf.connection,
					 globalconf.screen_nbr);

  /* Set up signal handlers and function called on normal exit */
  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = exit_on_signal;
  sa.sa_flags = 0;

  sigaction(SIGHUP, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);

  atexit(exit_cleanup);

  /**
   * First round-trip
   */

  /* Send requests for EWMH atoms initialisation */
  atoms_init();

  /* Prefetch the extensions data */
  xcb_prefetch_extension_data(globalconf.connection, &xcb_composite_id);
  xcb_prefetch_extension_data(globalconf.connection, &xcb_damage_id);
  xcb_prefetch_extension_data(globalconf.connection, &xcb_xfixes_id);

  /* Initialise errors handlers */
  xcb_event_handlers_init(globalconf.connection, &globalconf.evenths);
  event_init_start_handlers();

  /* Pre-initialisation of the rendering backend */
  render_preinit();

  /* Get replies for EWMH atoms initialisation */
  if(!atoms_init_finalise())
    fatal("Cannot initialise atoms");

  /* First check whether there is already a Compositing Manager (ICCCM) */
  xcb_get_selection_owner_cookie_t wm_cm_owner_cookie =
    xcb_ewmh_get_wm_cm_owner_unchecked(globalconf.connection);

  /**
   * Second round-trip
   */

  /* Initialise   extensions   based   on   the  cache   and   perform
     initialisation of the rendering backend */
  if(!display_init_extensions() || !render_init())
    return EXIT_FAILURE;

  /* Check ownership for WM_CM_Sn before actually claiming it (ICCCM) */
  xcb_window_t wm_cm_owner_win;
  if(!xcb_ewmh_get_wm_cm_owner_reply(globalconf.connection, wm_cm_owner_cookie,
				     &wm_cm_owner_win, NULL) ||
     wm_cm_owner_win != XCB_NONE)
    fatal("A compositing manager is already active");

  /* Now send requests to register the CM */
  display_register_cm();
  
  /**
   * Third round-trip
   */

  /* Check  extensions  version   and  finish  initialisation  of  the
     rendering backend */
  if(!display_init_extensions_finalise() || !render_init_finalise())
    return EXIT_FAILURE;

  /* Validate  errors   and  get  PropertyNotify   needed  to  acquire
     _NET_WM_CM_Sn ownership */
  xcb_aux_sync(globalconf.connection);
  xcb_event_poll_for_event_loop(&globalconf.evenths);

  /* Finish CM X registration */
  if(!display_register_cm_finalise())
    fatal("Could not acquire _NET_WM_CM_Sn ownership");

  /**
   * Last initialisation round-trip
   */

  /* Grab the server before performing redirection and get the tree of
     windows  to ensure  there  won't  be anything  else  at the  same
     time */
  xcb_grab_server(globalconf.connection);

  /* Now redirect windows and add existing windows */
  display_init_redirect();

  /* Validate errors handlers during redirect */
  xcb_aux_sync(globalconf.connection);
  xcb_event_poll_for_event_loop(&globalconf.evenths);

  /* Manage existing windows */
  display_init_redirect_finalise();

  xcb_ungrab_server(globalconf.connection);

  /* Initialise normal errors and events handlers */
  event_init_handlers();

  xcb_generic_event_t *event;

  /* Main event and error loop */
  do
    {
      /* Flush existing requests before blocking */
      xcb_flush(globalconf.connection);

      /* Block until an event is received */
      event = xcb_wait_for_event(globalconf.connection);

      /* Check X connection to avoid SIGSERV */
      if(xcb_connection_has_error(globalconf.connection))
	 fatal("X connection invalid");

      xcb_event_handle(&globalconf.evenths, event);
      free(event);

      /* Then process all remaining events in the queue because before
	 painting, all the DamageNotify have to be received */
      xcb_event_poll_for_event_loop(&globalconf.evenths);

      /* Now paint the windows */
      if(globalconf.do_repaint)
	render_paint_all();
    }
  while(true);

  return EXIT_SUCCESS;
}
