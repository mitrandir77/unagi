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
#include <xcb/composite.h>

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

void
experiment_paint_all(xcb_xfixes_region_t region __attribute__((unused)))
{
#if 0
  if(!region)
    {
      xcb_rectangle_t r = {
	.x = 0, .y = 0, .width = globalconf.screen->width_in_pixels,
	.height = globalconf.screen->height_in_pixels 
      };

      region = xcb_generate_id(globalconf.connection);

      xcb_xfixes_create_region(globalconf.connection, region, 1, &r);
    }

  xcb_xfixes_set_picture_clip_region(globalconf.connection,
				     globalconf.root_picture,
				     region, 0, 0);

  for(window_t *window = globalconf.windows; window; window = window->next)
    {
      if(!window->damaged)
	continue;

      if(!window->picture)
	{
	  window->pixmap = xcb_generate_id(globalconf.connection);

	  debug("%d, %d, %d, %d", window->geometry->x, window->geometry->y,
		window->geometry->width, window->geometry->border_width);

	  xcb_composite_name_window_pixmap(globalconf.connection,
					   window->id, window->pixmap);

	  xcb_render_pictvisual_t *window_pictvisual =
	    xcb_render_util_find_visual_format(globalconf.pict_formats,
					       window->attributes->visual);

	  const uint32_t create_picture_val = XCB_SUBWINDOW_MODE_INCLUDE_INFERIORS;
	  window->picture = xcb_generate_id(globalconf.connection);

	  xcb_render_create_picture(globalconf.connection,
				    window->picture,
				    window->pixmap,
				    window_pictvisual->format,
				    XCB_RENDER_CP_SUBWINDOW_MODE,
				    &create_picture_val);	  
	}

      /* border_size */
      if(!window->border_size)
	{
	  window->border_size = xcb_generate_id(globalconf.connection);

	  xcb_xfixes_create_region_from_window(globalconf.connection,
					       window->border_size,
					       window->id,
					       XCB_SHAPE_SK_BOUNDING);

	  xcb_xfixes_translate_region(globalconf.connection,
				      window->border_size,
				      (int16_t) (window->geometry->x + window->geometry->border_width),
				      (int16_t) (window->geometry->y + window->geometry->border_width));
	}

      /* win_extents */
      if(!window->extents)
	{
	  window->extents = xcb_generate_id(globalconf.connection);

	  xcb_rectangle_t r = {
	    .x = window->geometry->x, .y = window->geometry->y,
	    .width = (uint16_t) (window->geometry->width + window->geometry->border_width * 2),
	    .height = (uint16_t) (window->geometry->height + window->geometry->border_width * 2)
	  };

	  xcb_xfixes_create_region(globalconf.connection, window->extents, 1, &r);
	}

      if(!window->border_clip)
	{
	  window->border_clip = xcb_generate_id(globalconf.connection);

	  xcb_xfixes_create_region(globalconf.connection, window->border_clip, 0, NULL);
	  xcb_xfixes_copy_region(globalconf.connection, region, window->border_clip);
	}
    }

  xcb_xfixes_set_picture_clip_region(globalconf.connection, globalconf.root_picture,
				     region, 0, 0);

  /* paint_root */
  if(!root_tile)
    {
      /* paint_root() */
      root_tile = xcb_generate_id(globalconf.connection);
      
      window_get_root_background_pixmap();
      xcb_pixmap_t pixmap = window_get_root_background_pixmap_finalise();

      const uint32_t create_picture_val = true;

      xcb_render_create_picture(globalconf.connection,
				root_tile, pixmap,
				globalconf.root_pictvisual->format,
				XCB_RENDER_CP_REPEAT,
				&create_picture_val);
    }

  xcb_render_composite(globalconf.connection, XCB_RENDER_PICT_OP_SRC,
		       root_tile, XCB_NONE, globalconf.root_picture,
		       0, 0, 0, 0, 0, 0, globalconf.screen->width_in_pixels,
		       globalconf.screen->height_in_pixels);		       

  /* end of paint_root */

  for(window_t *window = globalconf.windows; window; window = window->next)
    {
      if(!window->damaged)
	continue;

      xcb_xfixes_set_picture_clip_region(globalconf.connection,
					 globalconf.root_picture,
					 window->border_clip,
					 0, 0);

      xcb_render_picture_t alpha_picture = xcb_generate_id(globalconf.connection);

      /* solid_picture */
      {
	xcb_pixmap_t pixmap = xcb_generate_id(globalconf.connection);

	xcb_create_pixmap(globalconf.connection, 8, pixmap,
			  globalconf.screen->root, 1, 1);

	xcb_render_pictforminfo_t *standard_format =
	  xcb_render_util_find_standard_format(globalconf.pict_formats,
					       XCB_PICT_STANDARD_A_8);

	const uint32_t create_picture_val = true;

	xcb_render_create_picture(globalconf.connection,
				  alpha_picture,
				  pixmap,
				  standard_format->id,
				  XCB_RENDER_CP_REPEAT,
				  &create_picture_val);

	xcb_render_color_t color;
	color.alpha = (uint16_t) ((window->opacity / OPACITY_OPAQUE) * 0xffff);
	color.red = color.green = color.blue = 0;

	xcb_rectangle_t rect = { .x = 0, .y = 0, .width = 1, .height = 1 };

	xcb_render_fill_rectangles(globalconf.connection, XCB_RENDER_PICT_OP_SRC,
				   alpha_picture, color, 1, &rect);

	xcb_free_pixmap(globalconf.connection, pixmap);
      }

      xcb_render_composite(globalconf.connection, XCB_RENDER_PICT_OP_OVER,
			   window->picture, alpha_picture, globalconf.root_picture,
			   0, 0, 0, 0,
			   window->geometry->x, window->geometry->y,
			   (uint16_t) (window->geometry->width + window->geometry->border_width * 2),
			   (uint16_t) (window->geometry->height + window->geometry->border_width * 2));

      xcb_xfixes_destroy_region(globalconf.connection, window->border_clip);
      window->border_clip = XCB_NONE;
    }

  xcb_xfixes_destroy_region(globalconf.connection, region);

  xcb_xfixes_set_picture_clip_region(globalconf.connection, globalconf.root_picture, XCB_NONE, 0, 0);

  xcb_render_composite(globalconf.connection, XCB_RENDER_PICT_OP_SRC,
		       globalconf.root_picture, XCB_NONE, globalconf.root_picture,
		       0, 0, 0, 0, 0, 0, globalconf.screen->width_in_pixels,
		       globalconf.screen->height_in_pixels);

#endif
}
