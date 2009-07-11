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
#include <stdint.h>
#include <assert.h>
#include <string.h>

#include <xcb/xcb.h>
#include <xcb/render.h>
#include <xcb/xcb_renderutil.h>

#include "structs.h"
#include "render.h"
#include "util.h"

static const xcb_query_extension_reply_t *render_ext;

static const char *render_request_label[] = {
  "RenderQueryVersion",
  "RenderQueryPictFormats",
  "RenderQueryPictIndexValues",
  "Render minor 3",
  "RenderCreatePicture",
  "RenderChangePicture",
  "RenderSetPictureClipRectangles",
  "RenderFreePicture",
  "RenderComposite",
  "Render minor 9",
  "RenderTrapezoids",
  "RenderTriangles",
  "RenderTriStrip",
  "RenderTriFan",
  "Render minor 14",
  "Render minor 15",
  "Render minor 16",
  "RenderCreateGlyphSet",
  "RenderReferenceGlyphSet",
  "RenderFreeGlyphSet",
  "RenderAddGlyphs",
  "Render minor 21",
  "RenderFreeGlyphs",
  "RenderCompositeGlyphs8",
  "RenderCompositeGlyphs16",
  "RenderCompositeGlyphs32",
  "RenderFillRectangles",
  "RenderCreateCursor",
  "RenderSetPictureTransform",
  "RenderQueryFilters",
  "RenderSetPictureFilter",
  "RenderCreateAnimCursor",
  "RenderAddTraps",
  "RenderCreateSolidFill",
  "RenderCreateLinearGradient",
  "RenderCreateRadialGradient",
  "RenderCreateConicalGradient"
};

static const char *render_error_label[] = {
  "PictFormat",
  "Picture",
  "PictOp",
  "GlyphSet",
  "Glyph"
};

/* TODO: global variable? */
static xcb_render_query_version_cookie_t _render_version_cookie = { 0 };
static xcb_render_query_pict_formats_cookie_t _render_pict_formats_cookie = { 0 };

void
render_preinit(void)
{
  xcb_prefetch_extension_data(globalconf.connection, &xcb_render_id);
}

bool
render_init(void)
{
  render_ext = xcb_get_extension_data(globalconf.connection,
				      &xcb_render_id);

  if(!render_ext || !render_ext->present)
    {
      fatal("No render extension");
      return false;
    }

  _render_version_cookie =
    xcb_render_query_version_unchecked(globalconf.connection,
				       XCB_RENDER_MAJOR_VERSION,
				       XCB_RENDER_MINOR_VERSION);

  _render_pict_formats_cookie =
    xcb_render_query_pict_formats_unchecked(globalconf.connection);

  /* Send requests to get the root window background pixmap */ 
  window_get_root_background_pixmap();

  return true;
}

bool
render_init_finalise(void)
{
  assert(_render_version_cookie.sequence);

  xcb_render_query_version_reply_t *render_version_reply =
    xcb_render_query_version_reply(globalconf.connection,
				   _render_version_cookie,
				   NULL);

  /* We need the alpha component */
  if(!render_version_reply || render_version_reply->minor_version < 1)
    {
      free(render_version_reply);

      fatal("Need Render extension 0.1 at least");
      return false;
    }

  free(render_version_reply);

  /* Now  create the  root window  picture used  when  compositing but
     before get the screen visuals... */

  assert(_render_pict_formats_cookie.sequence);

  /* The  "PictFormat" object  holds information  needed  to translate
     pixel values into red, green, blue and alpha channels */
  xcb_render_query_pict_formats_reply_t *render_pict_formats_reply =
    xcb_render_query_pict_formats_reply(globalconf.connection,
					_render_pict_formats_cookie,
					NULL);

  const xcb_render_pictvisual_t *screen_pictvisual;

  if(!render_pict_formats_reply ||
     !xcb_render_query_pict_formats_formats_length(render_pict_formats_reply) ||
     !(screen_pictvisual = xcb_render_util_find_visual_format(render_pict_formats_reply,
							      globalconf.screen->root_visual)))
    {
      free(render_pict_formats_reply);

      fatal("Can't get PictFormat of root window");
      return false;
    }

  memcpy(&globalconf.root_pictvisual, screen_pictvisual, sizeof(xcb_render_pictvisual_t));
  free(render_pict_formats_reply);

  /* Get the background image pixmap, if any, otherwise do nothing */
  const xcb_pixmap_t root_background_pixmap = window_get_root_background_pixmap_finalise();

  globalconf.root_picture = xcb_generate_id(globalconf.connection);
  const uint32_t create_picture_val[] = {
    true,
    XCB_SUBWINDOW_MODE_INCLUDE_INFERIORS
  };

  xcb_render_create_picture(globalconf.connection,
			    globalconf.root_picture,
			    (root_background_pixmap ? root_background_pixmap : globalconf.screen->root),
			    globalconf.root_pictvisual.format,
			    XCB_RENDER_CP_REPEAT | XCB_RENDER_CP_SUBWINDOW_MODE,
			    create_picture_val);

  return true;
}

bool
render_is_render_request(const uint8_t request_major_code)
{
  return (render_ext->major_opcode == request_major_code);
}

const char *
render_error_get_request_label(const uint16_t request_minor_code)
{
  return (request_minor_code < countof(render_request_label) ?
	  render_request_label[request_minor_code] : NULL);
}

const char *
render_error_get_error_label(const uint8_t error_code)
{
  const int render_error = error_code - render_ext->first_error;

  if(render_error < XCB_RENDER_PICT_FORMAT ||
     render_error > XCB_RENDER_GLYPH)
    return NULL;

  return render_error_label[render_error];
}

/* Free all resources allocated by the render backend */
void
render_cleanup(void)
{
  xcb_render_free_picture(globalconf.connection, globalconf.root_picture);
}
