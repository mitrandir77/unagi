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
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <dlfcn.h>
#include <getopt.h>

#include <xcb/xcb.h>
#include <xcb/composite.h>
#include <xcb/xfixes.h>
#include <xcb/damage.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_event.h>

#include <basedir.h>
#include <basedir_fs.h>
#include <confuse.h>

#include "structs.h"
#include "display.h"
#include "event.h"
#include "atoms.h"
#include "util.h"
#include "plugin.h"

conf_t globalconf;

static bool
parse_configuration_file(FILE *config_fp)
{
  cfg_opt_t opts[] = {
    CFG_STR("rendering", "render", CFGF_NONE),
    CFG_STR_LIST("plugins", "{}", CFGF_NONE),
    CFG_END()
  };

  globalconf.cfg = cfg_init(opts, CFGF_NONE);
  if(cfg_parse_fp(globalconf.cfg, config_fp) == CFG_PARSE_ERROR)
    return false;

  return true;
}

static inline void
display_help(void)
{
  printf("Usage: " PACKAGE_NAME "[options]\n\
  -h, --help                show help\n\
  -v, --version             show version\n\
  -c, --config FILE         configuration file path\n\
  -r, --rendering-path PATH rendering backend path\n\
  -p, --plugins-path PATH   plugins path\n");
}

static void
parse_command_line_parameters(int argc, char **argv)
{
  const struct option long_options[] = {
    { "help", 0, NULL, 'h' },
    { "version", 0, NULL, 'v' },
    { "config", 1, NULL, 'c' },
    { "rendering-path", 1, NULL, 'r' },
    { "plugins-path", 1, NULL, 'p' },
    { NULL, 0, NULL, 0 }
  };

  int opt;
  FILE *config_fp = NULL;

  while((opt = getopt_long(argc, argv, "vhc:r:p:",
			   long_options, NULL)) != -1)
    {
      switch(opt)
	{
	case 'v':
	  printf(VERSION);
	  exit(EXIT_SUCCESS);
	  break;
	case 'h':
	  display_help();
	  exit(EXIT_SUCCESS);
	  break;
	case 'c':
	  if(!strlen(optarg) || !(config_fp = fopen(optarg, "r")))
	    {
	      display_help();
	      exit(EXIT_FAILURE);
	    }
	  break;
	case 'r':
	  if(optarg && strlen(optarg))
	    globalconf.rendering_dir = strdup(optarg);
	  else
	    fatal("-r option requires a directory");
	  break;
	case 'p':
	  if(optarg && strlen(optarg))
	    globalconf.plugins_dir = strdup(optarg);
	  else
	    fatal("-p option requires a directory");
	  break;
	}
    }

  /* Get the configuration file */
  if(!config_fp)
    {
      xdgHandle xdg;
      xdgInitHandle(&xdg);
      config_fp = xdgConfigOpen(PACKAGE_NAME ".conf", "r", &xdg);
      xdgWipeHandle(&xdg);

      if(!config_fp)
	fatal("Can't open configuration file");
    }

  if(!parse_configuration_file(config_fp))
    {
      fclose(config_fp);
      fatal("Can't parse configuration file");
    }

  fclose(config_fp);

  /* Get the rendering backend path if not given in the command line
     parameters */
  if(!globalconf.rendering_dir)
    globalconf.rendering_dir = strdup(RENDERING_DIR);

  /* Get  the  plugins   path  if  not  given  in   the  command  line
     parameters */
  if(!globalconf.plugins_dir)
    globalconf.plugins_dir = strdup(PLUGINS_DIR);
}

/** Perform cleanup on normal exit */
static void
exit_cleanup(void)
{
  debug("Cleaning resources up");

  /* Destroy CM window, thus giving up _NET_WM_CM_Sn ownership */
  if(globalconf.cm_window != XCB_NONE)
    xcb_destroy_window(globalconf.connection, globalconf.cm_window);

  /* Destroy the linked-list of windows */
  window_list_cleanup();

  /* Free resources related to the rendering backend */
  rendering_unload();

  /* Free resources related to the plugins */
  plugin_unload_all();

  cfg_free(globalconf.cfg);
  free(globalconf.rendering_dir);
  free(globalconf.plugins_dir);

  xcb_disconnect(globalconf.connection);
}

/** Perform  cleanup when  a  signal (SIGHUP,  SIGINT  or SIGTERM)  is
 *  received
 */
static void
exit_on_signal(int sig __attribute__((unused)))
{
  exit_cleanup();

  /* Exit  the  program without  calling  the  function register  with
     atexit() */
  _exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
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

  parse_command_line_parameters(argc, argv);

  /* Prefetch the extensions data */
  xcb_prefetch_extension_data(globalconf.connection, &xcb_composite_id);
  xcb_prefetch_extension_data(globalconf.connection, &xcb_damage_id);
  xcb_prefetch_extension_data(globalconf.connection, &xcb_xfixes_id);

  /* Initialise errors handlers */
  xcb_event_handlers_init(globalconf.connection, &globalconf.evenths);
  event_init_start_handlers();

  /* Pre-initialisation of the rendering backend */
  if(!rendering_load())
    fatal("Can't initialise rendering backend");

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
  display_init_extensions();
  if(!(*globalconf.rendering->init)())
    return EXIT_FAILURE;

  /* Check ownership for WM_CM_Sn before actually claiming it (ICCCM) */
  xcb_window_t wm_cm_owner_win;
  if(xcb_ewmh_get_wm_cm_owner_reply(globalconf.connection, wm_cm_owner_cookie,
				    &wm_cm_owner_win, NULL) &&
     wm_cm_owner_win != XCB_NONE)
    fatal("A compositing manager is already active (window=%jx)",
	  (uintmax_t) wm_cm_owner_win);

  /* Now send requests to register the CM */
  display_register_cm();
  
  /**
   * Third round-trip
   */

  /* Check  extensions  version   and  finish  initialisation  of  the
     rendering backend */
  display_init_extensions_finalise();
  if(!(*globalconf.rendering->init_finalise)())
    return EXIT_FAILURE;

  /* All the plugins given in the configuration file */
  plugin_load_all();

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

  /* Flush existing  requests before  the loop as  DamageNotify events
     may have been received in the meantime */
  xcb_flush(globalconf.connection);
  globalconf.do_repaint = true;

  /* Main event and error loop */
  do
    {
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
	{
	  window_paint_all();
	  xcb_aux_sync(globalconf.connection);
	}

      globalconf.do_repaint = false;
    }
  while(true);

  return EXIT_SUCCESS;
}
