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
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include <xcb/xfixes.h>
#include <xcb/shape.h>
#include <xcb/xcb_aux.h>

#include <xcb/render.h>
#include <xcb/xcb_renderutil.h>
#include <xcb/xcb_aux.h>

#include "structs.h"
#include "util.h"
#include "window.h"
#include "atoms.h"
#include "experiment.h"

void
window_test_xfixes_regions(void)
{
  for(window_t *window = globalconf.windows;
      window != NULL; window = window->next)
    {
      xcb_xfixes_region_t region = xcb_generate_id(globalconf.connection);

      xcb_xfixes_create_region_from_window(globalconf.connection, region,
					   window->id, XCB_SHAPE_SK_BOUNDING);

      xcb_aux_sync(globalconf.connection);

      xcb_xfixes_fetch_region_reply_t *fetch_region_reply =
	xcb_xfixes_fetch_region_reply(globalconf.connection,
				      xcb_xfixes_fetch_region_unchecked(globalconf.connection,
									region),
				      NULL);

      if(!fetch_region_reply)
	{
	  fprintf(stderr, "Can't fetch region (window=%x)\n", window->id);
	  continue;
	}

      xcb_rectangle_t *region_rectangles =
	xcb_xfixes_fetch_region_rectangles(fetch_region_reply);

      for(int region_idx = 0;
	  region_idx < xcb_xfixes_fetch_region_rectangles_length(fetch_region_reply);
	  region_idx++)
	{
	  const xcb_rectangle_t r = region_rectangles[region_idx];

	  printf("%x: rectangle: %dx%d (width=%u, height=%u)\n",
		 window->id, r.x, r.x, r.width, r.height);
	}

      free(fetch_region_reply);
    }
}

void
window_test_init_render(void)
{
  
#if 0
  xcb_render_query_pict_formats_reply_t *render_pict_formats_reply =
    xcb_render_query_pict_formats_reply(globalconf.connection,
					xcb_render_query_pict_formats_unchecked(globalconf.connection),
					NULL);

  if(!render_pict_formats_reply)
    {
      fprintf(stderr, "FOO");
      exit(EXIT_FAILURE);
    }

  const xcb_render_pictvisual_t *screen_pictvisual =
    xcb_render_util_find_visual_format(render_pict_formats_reply,
				       globalconf.screen->root_visual);

  /* BEGIN */

  /* BEGIN paint_all() */

  xcb_pixmap_t root_pixmap = xcb_generate_id(globalconf.connection);

  xcb_create_pixmap(globalconf.connection,
		    globalconf.screen->root_depth,
		    root_pixmap,
		    globalconf.screen->root,
		    globalconf.screen->width_in_pixels,
		    globalconf.screen->height_in_pixels);

  xcb_render_picture_t root_buffer = xcb_generate_id(globalconf.connection);

  xcb_render_create_picture(globalconf.connection, 
			    root_buffer,
			    root_pixmap,
			    screen_pictvisual->format,
			    0, NULL);

  xcb_free_pixmap(globalconf.connection, root_pixmap);

  xcb_rectangle_t r = {
    .x = 0, .y = 0,
    .width = globalconf.screen->width_in_pixels,
    .height = globalconf.screen->height_in_pixels };

  xcb_xfixes_region_t region = xcb_generate_id(globalconf.connection);
  xcb_xfixes_create_region(globalconf.connection, region, 1, &r);

  xcb_xfixes_set_picture_clip_region(globalconf.connection, globalconf.root_picture,
				     region, 0, 0);

  xcb_xfixes_set_picture_clip_region(globalconf.connection, root_buffer,
				     region, 0, 0);

  /* BEGIN paint_root() */

  /* BEGIN root_tile() */

  xcb_pixmap_t pixmap = 0;
  xcb_get_property_reply_t *window_property_reply;

  for(int background_property_n = 0;
      background_properties_atoms[background_property_n];
      background_property_n++)
    {
      window_property_reply =
	xcb_get_property_reply(globalconf.connection,
			       xcb_get_property_unchecked(globalconf.connection,
							  false, globalconf.screen->root,
							  *background_properties_atoms[background_property_n],
							  XCB_GET_PROPERTY_TYPE_ANY,
							  0, 4),
			       NULL);

      if(window_property_reply && window_property_reply->type == PIXMAP &&
	 (xcb_get_property_value_length(window_property_reply) >> 2) == 1)
	{
	  memcpy(&pixmap, xcb_get_property_value(window_property_reply), 4);
	  free(window_property_reply);      
	  break;
	}

      
      free(window_property_reply);
      fprintf(stderr, "Can't get root window property\n");
    }

  if(!pixmap)
    {
      pixmap = xcb_generate_id(globalconf.connection);

      xcb_create_pixmap(globalconf.connection, 
			globalconf.screen->root_depth,
			pixmap,
			globalconf.screen->root,
			1, 1);
    }

  xcb_render_picture_t root_tile_picture = xcb_generate_id(globalconf.connection);

  const uint32_t create_picture_data = true;

  xcb_render_create_picture(globalconf.connection,
			    root_tile_picture,
			    pixmap,
			    screen_pictvisual->format,
			    XCB_RENDER_CP_REPEAT,
			    &create_picture_data);

  xcb_render_composite(globalconf.connection, XCB_RENDER_PICT_OP_SRC,
		       root_tile_picture, XCB_NONE, root_buffer,
		       0, 0, 0, 0, 0, 0,
		       globalconf.screen->width_in_pixels,
		       globalconf.screen->height_in_pixels);

  xcb_xfixes_destroy_region(globalconf.connection, region);

  xcb_xfixes_set_picture_clip_region(globalconf.connection, root_buffer,
				     0, 0, XCB_NONE);

  xcb_render_composite(globalconf.connection, XCB_RENDER_PICT_OP_SRC,
		       root_buffer, XCB_NONE, globalconf.root_picture,
		       0, 0, 0, 0, 0, 0,
		       globalconf.screen->width_in_pixels,
		       globalconf.screen->height_in_pixels);

  /* END root_tile() */

  /* END paint_root() */

  /* END paint_all() */

  /* END */

  free(render_pict_formats_reply);
#endif
}
