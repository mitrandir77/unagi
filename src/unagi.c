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

/** \file
 *  \brief Main file
 */

#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <dlfcn.h>
#include <getopt.h>
#include <stdlib.h>

#include <xcb/xcb.h>
#include <xcb/composite.h>
#include <xcb/xfixes.h>
#include <xcb/damage.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_aux.h>
#include <xcb/xcb_keysyms.h>

#include <basedir.h>
#include <basedir_fs.h>
#include <confuse.h>

#include "structs.h"
#include "display.h"
#include "event.h"
#include "atoms.h"
#include "util.h"
#include "plugin.h"
#include "key.h"

#ifdef __DEBUG__
/*
 * Basic painting performance benchmark using gettimeofday
 */
#include <time.h>
#include <math.h>
#endif

conf_t globalconf;

#define CONFIG_FILENAME PACKAGE_NAME ".conf"

/** Parse the configuration file with confuse
 *
 * \param config_fp The configuration file stream
 * \return Return true if parsing succeeded
 */
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

/** Display help information */
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

/** Parse command line parameters
 *
 * \param argc The number of command line parameters
 * \param argv The strings list of arguments
 */
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
      /* Look    for    the    configuration    file    in    Autoconf
	 $sysconfigdir/xdg, then fall back on XDG if not found */
      if((config_fp = fopen(XDG_CONFIG_DIR "/" CONFIG_FILENAME, "r")) == NULL)
	{
	  xdgHandle xdg;
	  xdgInitHandle(&xdg);
	  config_fp = xdgConfigOpen(CONFIG_FILENAME, "r", &xdg);
	  xdgWipeHandle(&xdg);
	}

      if(!config_fp)
	fatal("Can't open configuration file");
    }

  /* Parse configuration file */
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

  /* Free resources related to the plugins */
  plugin_unload_all();

  /* Destroy the  linked-list of  windows which has  to be  done after
     unloading the plugins as the  plugins may use the windows list to
     free memory */
  window_list_cleanup();

  /* Free resources related  to the rendering backend which  has to be
     done  after the  windows  list  cleanup as  the  latter free  the
     rendering information associated with each window */
  rendering_unload();

  /* Free resources related to the keymaps */
  xcb_key_symbols_free(globalconf.keysyms);

  /* Free resources related to EWMH */
  xcb_ewmh_connection_wipe(&globalconf.ewmh);

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
  xcb_intern_atom_cookie_t *ewmh_cookies = atoms_init();

  parse_command_line_parameters(argc, argv);

  /* Prefetch the extensions data */
  xcb_prefetch_extension_data(globalconf.connection, &xcb_composite_id);
  xcb_prefetch_extension_data(globalconf.connection, &xcb_damage_id);
  xcb_prefetch_extension_data(globalconf.connection, &xcb_xfixes_id);

  /* Pre-initialisation of the rendering backend */
  if(!rendering_load())
    {
      free(ewmh_cookies);
      fatal("Can't initialise rendering backend");
    }

  /* Get replies for EWMH atoms initialisation */
  if(!atoms_init_finalise(ewmh_cookies))
    /* No need to  free ewmh_cookies in case of  error as it's already
       handles by xcb-ewmh when getting the replies */
    fatal("Cannot initialise atoms");

  /* First check whether there is already a Compositing Manager (ICCCM) */
  xcb_get_selection_owner_cookie_t wm_cm_owner_cookie =
    xcb_ewmh_get_wm_cm_owner(&globalconf.ewmh, globalconf.screen_nbr);

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
  if(xcb_ewmh_get_wm_cm_owner_reply(&globalconf.ewmh, wm_cm_owner_cookie,
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
  event_handle_poll_loop(event_handle_startup);

  globalconf.keysyms = xcb_key_symbols_alloc(globalconf.connection);
  xcb_get_modifier_mapping_cookie_t key_mapping_cookie =
    xcb_get_modifier_mapping_unchecked(globalconf.connection);

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
  event_handle_poll_loop(event_handle_startup);

  /* Manage existing windows */
  display_init_redirect_finalise();

  xcb_ungrab_server(globalconf.connection);

  /* Check the  plugin requirements  which will disable  plugins which
     don't meet the requirements */
  plugin_check_requirements();

  /* Get the lock masks reply of the request previously sent */ 
  key_lock_mask_get_reply(key_mapping_cookie);

  /* Flush existing  requests before  the loop as  DamageNotify events
     may have been received in the meantime */
  xcb_flush(globalconf.connection);

#ifdef __DEBUG__
  /* Meaningful to measure painting performances */
  long paint_time_sum = 0;
  long paint_time_min = LONG_MAX;
  long paint_time_max = 0;

  /* For online computation of standard deviation */
  long paint_time_mean = 0;
  long paint_time_variance_sum = 0;
  unsigned int paint_counter = 0;
#endif

  window_paint_all(globalconf.windows);

  /* Main event and error loop */
  xcb_generic_event_t *event;
  do
    {
      /* Block until an event is received */
      event = xcb_wait_for_event(globalconf.connection);

      /* Check X connection to avoid SIGSEGV */
      if(xcb_connection_has_error(globalconf.connection))
	 fatal("X connection invalid");

      event_handle(event);
      free(event);

      /* Then process all remaining events in the queue because before
	 painting, all the DamageNotify have to be received */
      event_handle_poll_loop(event_handle);

      /* Now paint the windows */
      if(globalconf.damaged)
	{
#ifdef __DEBUG__
          debug("COUNT: %d: Begin re-painting", paint_counter);
#endif
	  window_t *windows = NULL;
	  for(plugin_t *plugin = globalconf.plugins; plugin; plugin = plugin->next)
	    if(plugin->enable && plugin->vtable->render_windows &&
	       (windows = (*plugin->vtable->render_windows)()))
	      break;

	  if(!windows)
	    windows = globalconf.windows;

#ifdef __DEBUG__
          /* Display damaged regions */
          xcb_xfixes_fetch_region_reply_t *r = \
            xcb_xfixes_fetch_region_reply(globalconf.connection,
                                          xcb_xfixes_fetch_region(globalconf.connection,
                                                                  globalconf.damaged),
                                          NULL);
          if(r)
            {
              xcb_rectangle_t *rects = xcb_xfixes_fetch_region_rectangles(r);

              for(int i = 0; i < xcb_xfixes_fetch_region_rectangles_length(r);
                  i++)
                debug("Damaged region #%d: %dx%d +%d+%d",
                      i, rects[i].width, rects[i].height,
                      rects[i].x, rects[i].y);
            }

          struct timespec start, end;
          clock_gettime(CLOCK_MONOTONIC_RAW, &start);
#endif /* __DEBUG__ */
	  window_paint_all(windows);
	  xcb_aux_sync(globalconf.connection);
#ifdef __DEBUG__
          clock_gettime(CLOCK_MONOTONIC_RAW, &end);

          ++paint_counter;

          long paint_time = ((end.tv_nsec - start.tv_nsec) / 1000) +
            (((long) end.tv_sec - start.tv_sec) * 1000000);

          if(paint_time < paint_time_min)
            paint_time_min = paint_time;
          if(paint_time > paint_time_max)
            paint_time_max = paint_time;

          paint_time_sum += paint_time;

          /* Compute standard deviation for this iteration */
          long double delta = paint_time - paint_time_mean;
          paint_time_mean += delta / paint_counter;
          paint_time_variance_sum += delta * (paint_time - paint_time_mean);

          debug("Painting time in ms (#%u): %lu, min=%lu, max=%lu, "
                "average=%lu (+/- %.1Lf)",
                paint_counter, paint_time, paint_time_min, paint_time_max,
                paint_time_sum / paint_counter,
                sqrtl(paint_time_variance_sum / paint_counter));
#endif /* __DEBUG__ */
          display_reset_damaged();
          debug("Finish re-painting");
	}
    }
  while(true);

  return EXIT_SUCCESS;
}
