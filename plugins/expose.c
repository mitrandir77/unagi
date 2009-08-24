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

/** Expose plugin:
 *  ==============
 *
 *  This plugin implements (roughly) Expose  feature as seen in Mac OS
 *  X and Compiz  (known as Scale plugin) but  is not really optimised
 *  (yet) because it repaints all  the windows even if the content has
 *  not been changed  and it should also maybe  use SHM for xcb_image.
 *  The window  slots could be arranged  in a better  way by including
 *  the window geometry in the computation and the rescaling algorithm
 *  should be improved to decrease the blurry effect.
 *
 *  It relies  on _NET_CLIENT_LIST  (required otherwise the  plugin is
 *  disabled) and _NET_ACTIVE_WINDOW atoms (required too and stored in
 *  '_expose_global.atoms' structure) to  get respectively the clients
 *  managed  by the  window manager  and the  current  focused window.
 *  These atoms values are updated in a lazy way (e.g.  by sending the
 *  GetProperty requests on initialisation and PropertyNotify and then
 *  getting the reply as late as needed).
 *
 *  The rendering is performed in  the following steps when the plugin
 *  is enabled ('_expose_plugin_enable'):
 *
 *   1/  Create the  slots where  each window  will be  put  by simply
 *      dividing the screen in  strips according the current number of
 *      windows ('_expose_create_slots').
 *
 *   2/ Assign each  window to a slot based  on the Euclidian distance
 *      between their center ('_expose_assign_windows_to_slots').
 *
 *   3/ Map all windows which were unmapped to get their content using
 *      NameWindowPixmap  Composite   request  (when  the   window  is
 *      unmapped, the  content is not guaranteed to  be preserved) and
 *      also set OverrideRedirect attribute  to ensure that the window
 *      manager will not care about them anymore.
 *
 *   4/ For each window, create  a new 'window_t' object which will be
 *      then given to 'window_paint_all' function of the core code. If
 *      the window  needs to be  rescaled (e.g.  when the  window does
 *      not  fit  the  slot),  create  a  new  Image,  Pixmap  and  GC
 *      ('_expose_prepare_windows'). Then (and each time the window is
 *      repainted), get the Image of  the original window and get each
 *      pixel  which   will  then  be   put  on  the   rescaled  Image
 *      ('_expose_update_scale_pixmap')  using  a Gaussian-filter-like
 *      rescaled algorithm.
 */

#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include <X11/keysym.h>

#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_image.h>
#include <xcb/xcb_aux.h>

#include "structs.h"
#include "window.h"
#include "plugin.h"
#include "atoms.h"
#include "util.h"
#include "key.h"

#define PLUGIN_KEY XK_F12
#define STRIP_SPACING 10

/** Weights values apply  to pixels around a given  pixel value (whose
    weight is center of the matrice) */
static const uint32_t _expose_scale_weights[][3] = {
  { 1, 4, 1},
  { 4, 10, 4},
  { 1, 4, 1}
};

/** Get each channel component of a 32-bit pixel */
#define GET_R(pixel, weight) (((pixel) & 0x0000ff) * weight)
#define GET_G(pixel, weight) GET_R(pixel >> 8, weight)
#define GET_B(pixel, weight) GET_R(pixel >> 16, weight)

/** From each  channel component, compute  the actual pixel  value but
    multiply each channel by a weight */
#define SET_PIXEL(r, g, b, w) ((r / w) | ((g / w) << 8) | ((b / w) << 16))

/** Expose window */
typedef struct
{
  /** Rescaled window */
  window_t *window;
  /** Rescaled image of the window */
  xcb_image_t *image;
  /** Graphical context of the rescaled window pixmap */
  xcb_gcontext_t gc;
  /** If the window was unmapped before enabling the plugin */
  bool was_unmapped;
} _expose_scale_window_t;

/** Each window is contained within a slot */
typedef struct
{
  /** Slot geometry */
  xcb_rectangle_t extents;
  /** Nearest window associated with this slot */
  window_t *window;
  /** Rescaled window */
  _expose_scale_window_t scale_window;
} _expose_window_slot_t;

/** Atoms required for this plugin */
typedef struct
{
  /** _NET_CLIENT_LIST atom cookie and value */
  xcb_get_property_cookie_t client_list_cookie;
  xcb_ewmh_get_windows_reply_t *client_list;
  /** _NET_ACTIVE_WINDOW atom cookie and value */
  xcb_get_property_cookie_t active_window_cookie;
  xcb_window_t *active_window;
} _expose_atoms_t;

static struct
{
  bool enabled;
  _expose_atoms_t atoms;
  _expose_window_slot_t *slots;
} _expose_global;

/** Called  on  dlopen() to  initialise  memory  areas  and also  send
 *  GetProperty requests  on the root window  for _NET_CLIENT_LIST and
 *  _NET_ACTIVE_WINDOW atoms to avoid  blocking when these values will
 *  be needed
 */
static void __attribute__((constructor))
expose_constructor(void)
{
  memset(&_expose_global, 0, sizeof(_expose_global));

  _expose_global.atoms.client_list = NULL;
  _expose_global.atoms.active_window = NULL;

  /* Send  the requests  to check  whether the  atoms are  present and
     whose replies will  be got when actually calling  the function to
     check atoms which are required */
  _expose_global.atoms.client_list_cookie =
    xcb_ewmh_get_client_list_unchecked(globalconf.connection);

  _expose_global.atoms.active_window_cookie =
    xcb_ewmh_get_active_window_unchecked(globalconf.connection);
}

/** Free all the allocated slots
 *
 * \param slots The slots to be freed
 */
static void
_expose_free_slots(_expose_window_slot_t **slots)
{
  for(_expose_window_slot_t *slot = *slots; slot && slot->window; slot++)
    {
      if(slot->scale_window.image)
	xcb_image_destroy(slot->scale_window.image);

      if(slot->scale_window.gc != XCB_NONE)
	xcb_free_gc(globalconf.connection, slot->scale_window.gc);

      /* Free the scaled  window Pixmap only if it's  not the original
	 window one */
      if(slot->scale_window.window->pixmap != XCB_NONE &&
	 slot->scale_window.window->geometry->width != slot->window->geometry->width &&
	 slot->scale_window.window->geometry->height != slot->window->geometry->height)
	xcb_free_pixmap(globalconf.connection, slot->scale_window.window->pixmap);

      (*globalconf.rendering->free_window)(slot->scale_window.window);

      free(slot->scale_window.window->geometry);
      free(slot->scale_window.window);
    }

  util_free(slots);
}

/** Update   the    values   of   _NET_CLIENT_LIST    (required)   and
 *  _NET_ACTIVE_WINDOW (required) if the GetProperty has been sent but
 *  not  already  retrieved  (thus  on  plugin  initialisation  or  on
 *  PropertyNotify event).  This function  also frees the slots if the
 *  clients list has to be updated
 *
 * \param atoms Atoms information
 * \param slots The slots to be freed if necessary
 */
static void
_expose_update_atoms_values(_expose_atoms_t *atoms,
			    _expose_window_slot_t **slots)
{
#define CHECK_REQUIRED_ATOM(kind, kind_type, atom_name)			\
  if(atoms->kind##_cookie.sequence)					\
    {									\
      if(!atoms->kind)							\
	atoms->kind = calloc(1, sizeof(kind_type));			\
									\
      if(!xcb_ewmh_get_##kind##_reply(globalconf.connection,		\
				      atoms->kind##_cookie,		\
				      atoms->kind,			\
				      NULL))				\
	{								\
	  warn("Can't get %s: plugin disabled for now", #atom_name);	\
	  util_free(&atoms->kind);					\
	}								\
      else								\
	_expose_free_slots(slots);					\
									\
      /* Reset the cookie sequence for the next request */		\
      atoms->kind##_cookie.sequence = 0;				\
    }

  CHECK_REQUIRED_ATOM(client_list, xcb_ewmh_get_windows_reply_t, _NET_CLIENT_LIST)
    CHECK_REQUIRED_ATOM(active_window, xcb_window_t, _NET_ACTIVE_WINDOW)
    }

/** Check  whether  the plugin  can  actually  be  enabled, only  when
 *  _NET_CLIENT_LIST Atom Property has been set on the root window
 *
 * \todo make the GrabKey completely asynchronous
 * \return true if the plugin can be enabled
 */
static bool
expose_check_requirements(void)
{
  if(!atoms_is_supported(_NET_CLIENT_LIST))
    return false;

  _expose_update_atoms_values(&_expose_global.atoms, &_expose_global.slots);
  if(!_expose_global.atoms.client_list || !_expose_global.atoms.active_window)
    return false;

  /* Send the GrabKey request on the key given in the configuration */
  xcb_keycode_t *keycode = keycode = xcb_key_symbols_get_keycode(globalconf.keysyms,
								 PLUGIN_KEY);

  xcb_void_cookie_t grab_key_cookie = xcb_grab_key_checked(globalconf.connection, false,
							   globalconf.screen->root,
							   XCB_NONE, *keycode,
							   XCB_GRAB_MODE_ASYNC,
							   XCB_GRAB_MODE_ASYNC);

  free(keycode);

  /* Check  whether the GrabKey  request succeeded,  otherwise disable
     the plugin */
  xcb_generic_error_t *error = xcb_request_check(globalconf.connection,
						 grab_key_cookie);

  if(error)
    {
      warn("Can't grab selected key");
      free(error);
      exit(EXIT_FAILURE);
    }

  return true;
}

/** Check whether the window actually needs to be rescaled
 *
 * \param slot_extents The slots rectangle
 * \param window_width The window original width including border
 * \param window_height The window original height including border
 * \return true if the window needs to be rescaled
 */
static inline bool
_expose_window_need_rescaling(xcb_rectangle_t *slot_extents,
			      const uint16_t window_width,
			      const uint16_t window_height)
{
  return slot_extents->width < window_width ||
    slot_extents->height < window_height;
}

/** Create the slots where the  window will be arranged. The screen is
 *  divided in  strips of the same  size whose number is  given by the
 *  square root of the number of windows
 *
 * \param nwindows The number of windows
 * \return The newly allocated slots
 */
static _expose_window_slot_t *
_expose_create_slots(const uint32_t nwindows, unsigned int *nwindows_per_strip)
{
  _expose_window_slot_t *new_slots = calloc(nwindows + 1, sizeof(_expose_window_slot_t));
  if(!new_slots)
    return NULL;

  /* The  screen is  divided  in  strips depending  on  the number  of
     windows */
  const uint8_t strips_nb = (uint8_t) sqrt(nwindows + 1);

  /* Each strip height excludes spacing */
  const uint16_t strip_height = (uint16_t) 
    ((globalconf.screen->height_in_pixels - STRIP_SPACING * (strips_nb + 1)) / strips_nb);

  /* The number of windows per strip depends */
  *nwindows_per_strip = (unsigned int) ceilf((float) nwindows / (float) strips_nb);

  int16_t current_y = STRIP_SPACING, current_x;

  /* Each slot is a rectangle  whose coordinates depends on the number
     of strips and the number of windows */	
  unsigned int slot_n = 0;

  /* Create the strips of windows */
  for(uint8_t strip_n = 0; strip_n < strips_nb; strip_n++)
    {
      current_x = STRIP_SPACING;

      /* Number of slots for this strip which depends on the number of
	 remaining slots (the last strip may contain less windows) */
      const unsigned int strip_slots_n =
	(nwindows - slot_n > *nwindows_per_strip ? *nwindows_per_strip : nwindows - slot_n);

      /* Slot width including spacing */
      const uint16_t slot_width = (uint16_t)
	((globalconf.screen->width_in_pixels - STRIP_SPACING * (strip_slots_n + 1)) / strip_slots_n);

      /* Now create the slots associated to this strip */
      for(unsigned int strip_slot = 0; strip_slot < strip_slots_n; strip_slot++)
	{
	  new_slots[slot_n].extents.x = current_x;
	  new_slots[slot_n].extents.y = current_y;
	  new_slots[slot_n].extents.width = slot_width;
	  new_slots[slot_n].extents.height = strip_height;

	  current_x = (int16_t) (current_x + slot_width + STRIP_SPACING);
	  ++slot_n;
	}

      current_y = (int16_t) (current_y + strip_height + STRIP_SPACING);
    }

  return new_slots;
}

/** Assign each  window into the  nearest slot based on  the Euclidian
 *  distance between the center of the slot and the window
 *
 * \param nwindows The number of windows
 * \param slots The slots where the window will be assign
 */
static void
_expose_assign_windows_to_slots(const uint32_t nwindows,
				const uint32_t nwindows_per_strip,
				_expose_window_slot_t *slots)
{
  struct
  {
    window_t *window;
    /* Coordinates of the window center */
    int16_t x, y;
  } windows[nwindows];

  /* Prepare the  windows and their information  before assigning them
     to a slot */
  for(uint32_t window_n = 0; window_n < nwindows; window_n++)
    {
      window_t *window = window_list_get(_expose_global.atoms.client_list->windows[window_n]);

      windows[window_n].window = window;
      windows[window_n].x = (int16_t) (window->geometry->x + window->geometry->width / 2);
      windows[window_n].y = (int16_t) (window->geometry->y + window->geometry->height / 2);
    }

  /* Assign the windows to its slot using Euclidian distance */
  for(uint32_t slot_n = 0; slot_n < nwindows; slot_n++)
    {
      const int16_t slot_x = (int16_t) (slots[slot_n].extents.x +
					slots[slot_n].extents.width / 2);

      const int16_t slot_y = (int16_t) (slots[slot_n].extents.y +
					slots[slot_n].extents.height / 2);

      int16_t x, y;
      uint16_t distance, nearest_distance = UINT16_MAX;
      uint32_t window_n_nearest = 0;

      for(uint32_t window_n = 0; window_n < nwindows; window_n++)
	{
	  if(!windows[window_n].window)
	    continue;

	  x = (int16_t) (windows[window_n].x - slot_x);
	  y = (int16_t) (windows[window_n].y - slot_y);

	  distance = (uint16_t) sqrt(x * x + y * y);

	  if(distance < nearest_distance)
	    {
	      slots[slot_n].window = windows[window_n].window;
	      window_n_nearest = window_n;
	      nearest_distance = distance;
	    }
	}

      windows[window_n_nearest].window = NULL;
    }

  for(uint32_t slot_n = 0; slot_n < nwindows; slot_n += nwindows_per_strip)
    {
      unsigned int slot_spare_pixels = 0;
      unsigned int slots_to_extend_n = 0;

      for(uint32_t window_strip_n = 0; window_strip_n < nwindows_per_strip;
	  window_strip_n++)
	{
	  if(window_width_with_border(slots[window_strip_n].window->geometry) <
	     slots[window_strip_n].extents.width)
	    {
	      slot_spare_pixels += (unsigned int)
		(slots[window_strip_n].extents.width -
		 window_width_with_border(slots[window_strip_n].window->geometry));

	      slots[window_strip_n].extents.width = window_width_with_border(slots[window_strip_n].window->geometry);
	      slots[window_strip_n].extents.x = (int16_t) (slots[window_strip_n].extents.x + (int16_t) slot_spare_pixels);
	    }
	  else if(window_width_with_border(slots[window_strip_n].window->geometry) ==
		  slots[window_strip_n].extents.width)
	    continue;
	  else
	    slots_to_extend_n++;
	}

      if(slots_to_extend_n)
	continue;

      uint16_t spare_pixels_per_slot = (uint16_t) (slot_spare_pixels / slots_to_extend_n);

      for(uint32_t window_strip_n = 0; window_strip_n < nwindows_per_strip;
	  window_strip_n++)
	if(window_width_with_border(slots[window_strip_n].window->geometry) >
	   slots[window_strip_n].extents.width)
	  slots[window_strip_n].extents.width = (uint16_t) (slots[window_strip_n].extents.width + spare_pixels_per_slot);
    }
}

/** Draw the original window border on the scaled window Image
 *
 * \param scale_window_image The scaled window Image
 * \param scale_window_width The scaled window width including border
 * \param scale_window_height The scaled window height including border
 * \param window_image The original window Image
 * \param border_width Window border with
 */
static void
_expose_draw_scale_window_border(xcb_image_t *scale_window_image,
				 const uint16_t scale_window_width,
				 const uint16_t scale_window_height,
				 xcb_image_t *window_image,
				 const uint16_t border_width)
{
  const uint32_t border_pixel = xcb_image_get_pixel(window_image, 0, 0);

  for(uint16_t x = 0; x < scale_window_width - 1; x++)
    {
      /* Draw horizontal top border */
      for(uint16_t y = 0; y < border_width; y++)
	xcb_image_put_pixel(scale_window_image, x, y, border_pixel);

      /* Draw horizontal bottom border */
      for (uint16_t y = (uint16_t) (scale_window_height - 1); y >= scale_window_height - border_width; y--)
	xcb_image_put_pixel(scale_window_image, x, y, border_pixel);
    }
      
  for(uint16_t y = 0; y < scale_window_height - 1; y++)
    {
      /* Draw left vertical border */
      for (uint16_t x = 0; x < border_width; x++)
	xcb_image_put_pixel(scale_window_image, x, y, border_pixel);

      /* Draw right vertival border */
      for (uint16_t x = (uint16_t) (scale_window_width - 1); x >= scale_window_width - border_width; x--)
	xcb_image_put_pixel(scale_window_image, x, y, border_pixel);
    }
}

/** Scale the window content of the given Image according to the given
 *  ratio. It implements a filter similar to Gauss (but with different
 *  weights) which  takes the pixels  arounds and merge  them together
 *  with a weight depending on their position
 *
 * \param scale_window_image The scaled window Image
 * \param scale_window_width The scaled window width including border
 * \param scale_window_height The scaled window height including border
 * \param ratio_rescale The invert of the rescaling ratio
 * \param window_image The original window Image
 * \param window_width The original window width including border
 * \param window_width The original window height including border
 * \param border_width Window border with
 */
static void
_expose_draw_scale_window_content(xcb_image_t *scale_window_image,
				  const uint16_t scale_window_width,
				  const uint16_t scale_window_height,
				  const double ratio_rescale,
				  xcb_image_t *window_image,
				  const uint16_t window_width,
				  const uint16_t window_height,
				  const uint16_t border_width)
{
  uint32_t window_pixels[window_width][window_height];

  /* Get the pixel for optimisation purpose */
  for(uint16_t x = 0; x < window_width; x++)
    for(uint16_t y = 0; y < window_height; y++)
      window_pixels[x][y] = xcb_image_get_pixel(window_image, x, y);

  int16_t ys, xs, ymin, ymax, xmin, xmax;

  const uint16_t scale_content_width = (uint16_t) (scale_window_width - border_width);
  const uint16_t scale_content_height = (uint16_t) (scale_window_height - border_width);

  const bool do_pixels_around = (1.0 / ratio_rescale) <= 0.90;

  for(uint16_t y_scale = border_width; y_scale < scale_content_height; y_scale++)
    {
      ys = (int16_t) trunc((double) y_scale * ratio_rescale);

      /* Compute the minimum and maxmimum y depending on the pixel position */
      ymin = (int16_t) (y_scale == border_width || !do_pixels_around ? ys : ys - 1);
      ymax = (int16_t) (y_scale == scale_content_height - 1 || !do_pixels_around ? ys : ys + 1);

      for(uint16_t x_scale = border_width; x_scale < scale_content_width; x_scale++)
	{
	  uint32_t moy[3] = { 0, 0, 0 };

	  xs = (int16_t) trunc((double) x_scale * ratio_rescale);
	  xmin = (int16_t) (x_scale == border_width || !do_pixels_around ? xs : xs - 1);
	  xmax = (int16_t) (x_scale == scale_content_width - 1 || !do_pixels_around ? xs : xs + 1);

	  uint32_t weight = 0;

	  for(int16_t y = ymin, y_gaussian = (int16_t) (ymin - ys + 1); y <= ymax; y++, y_gaussian++)
	    for(int16_t x = xmin, x_gaussian = (int16_t) (xmin - xs + 1); x <= xmax; x++, x_gaussian++)
	      {
		moy[2] += GET_R(window_pixels[x][y], _expose_scale_weights[x_gaussian][y_gaussian]);
		moy[1] += GET_G(window_pixels[x][y], _expose_scale_weights[x_gaussian][y_gaussian]);
		moy[0] += GET_B(window_pixels[x][y], _expose_scale_weights[x_gaussian][y_gaussian]);

		weight += _expose_scale_weights[x_gaussian][y_gaussian];
	      }

	  xcb_image_put_pixel(scale_window_image, x_scale, y_scale,
			      SET_PIXEL(moy[2], moy[1], moy[0], weight));
	}
    }
}

/** Perform window  rescaling and draw  the borders too which  are not
 *  rescaled at all
 *
 * \param scale_window_image The scale window image
 * \param scale_window_width The scale window width including border
 * \param scale_window_height The scale window height including border
 * \param window_image The original window image
 * \param window_width The original window width including border
 * \param window_height The original window height including border
 * \param border_width The border width
 */
static void
_expose_do_scale_window(xcb_image_t *scale_window_image,
			const uint16_t scale_window_width,
			const uint16_t scale_window_height,
			xcb_image_t *window_image,
			const uint16_t window_width,
			const uint16_t window_height,
			const uint16_t border_width)
{
  _expose_draw_scale_window_content(scale_window_image, scale_window_width, scale_window_height,
				    (double) window_width / (double) scale_window_width,
				    window_image, window_width, window_height, border_width);

  /* A window may have no border at all */
  if(border_width)
    _expose_draw_scale_window_border(scale_window_image, scale_window_width,
				     scale_window_height, window_image,
				     border_width);
}

/** Update the rescaled window Pixmap
 *
 * \param scale_window The scale window object
 * \param scale_window_width The scale window width including border
 * \param scale_window_height The scale window height including border
 * \param window The original window object
 * \param window_width The original window width including border
 * \param window_height The original window height including border
 */
static void
_expose_update_scale_pixmap(_expose_scale_window_t *scale_window,
			    const uint16_t scale_window_width,
			    const uint16_t scale_window_height,
			    const window_t *window,
			    const uint16_t window_width,
			    const uint16_t window_height)
{
  /* Create the image associated with the original window */
  xcb_image_t *window_image = xcb_image_get(globalconf.connection,
					    window->pixmap,
					    0, 0,
					    window_width,
					    window_height,
					    UINT32_MAX, XCB_IMAGE_FORMAT_Z_PIXMAP);

  _expose_do_scale_window(scale_window->image, scale_window_width, scale_window_height,
			  window_image, window_width, window_height,
			  window->geometry->border_width);

  xcb_image_put(globalconf.connection, scale_window->window->pixmap,
		scale_window->gc, scale_window->image, 0, 0, 0);

  xcb_image_destroy(window_image);

  scale_window->window->damaged = true;
}

/** Prepare the rescaled windows which  are going to be painted on the
 *  screen  by creating  the rescale  window  image and  then put  the
 *  pixels in it from the original window
 *
 * \param slots The windows slots
 */
static void
_expose_prepare_windows(_expose_window_slot_t *slots)
{
  window_t *scale_window_prev = NULL;

  for(_expose_window_slot_t *slot = slots; slot && slot->window; slot++)
    {
      /* Allocate  the space  needed  for the  scale  window which  is
	 basically a copy of the window object itself */
      slot->scale_window.window = calloc(1, sizeof(window_t));

      /* Link the previous element with the current one */
      if(scale_window_prev)
	scale_window_prev->next = slot->scale_window.window;

      scale_window_prev = slot->scale_window.window;

      /* The scale window coordinates are the slot ones */
      slot->scale_window.window->geometry = calloc(1, sizeof(xcb_get_geometry_reply_t));
      slot->scale_window.window->geometry->x = slot->extents.x;
      slot->scale_window.window->geometry->y = slot->extents.y;
      slot->scale_window.window->geometry->border_width = slot->window->geometry->border_width;

      slot->scale_window.window->attributes = slot->window->attributes;

      const uint16_t window_width = window_width_with_border(slot->window->geometry);
      const uint16_t window_height = window_height_with_border(slot->window->geometry);

      /* If the window does not need to be rescaled, just ignore it */
      if(!_expose_window_need_rescaling(&slot->extents, window_width, window_height))
	{
	  slot->scale_window.window->geometry->width = slot->window->geometry->width;
	  slot->scale_window.window->geometry->height = slot->window->geometry->height;
	  slot->scale_window.window->pixmap = slot->window->pixmap;
	  slot->scale_window.window->damaged = true;

	  debug("Don't scale %jx", (uintmax_t) slot->window->id);
	  continue;
	}

      float ratio;

      /* Compute the ratio from the  largest side (width or height) of
	 the window */
      if((window_width - slot->extents.width) > (window_height - slot->extents.height))
	ratio = (float) slot->extents.width / (float) window_width;
      else
	ratio = (float) slot->extents.height / (float) window_height;

      slot->scale_window.window->geometry->width = (uint16_t)
	floorf(ratio * (float) slot->window->geometry->width);

      slot->scale_window.window->geometry->height = (uint16_t)
	floorf(ratio * (float) slot->window->geometry->height);

      /* The geometry width and height never include the border width */
      const uint16_t scale_window_width =
	window_width_with_border(slot->scale_window.window->geometry);

      const uint16_t scale_window_height = 
	window_height_with_border(slot->scale_window.window->geometry);

      /* Create the image associated with the rescaled window */
      slot->scale_window.image = xcb_image_create_native(globalconf.connection,
							 scale_window_width,
							 scale_window_height,
							 XCB_IMAGE_FORMAT_Z_PIXMAP,
							 24, 0, 0, 0);

      /* Create the rescaled window Pixmap and put the image in it */
      slot->scale_window.window->pixmap = xcb_generate_id(globalconf.connection);

      xcb_create_pixmap(globalconf.connection, 24,
			slot->scale_window.window->pixmap,
			globalconf.screen->root,
			scale_window_width,
			scale_window_height);

      slot->scale_window.gc = xcb_generate_id(globalconf.connection);

      xcb_create_gc(globalconf.connection, slot->scale_window.gc,
		    slot->scale_window.window->pixmap, 0, NULL);

      _expose_update_scale_pixmap(&slot->scale_window, scale_window_width,
				  scale_window_height, slot->window,
				  window_width, window_height);
    }

#ifdef __DEBUG__
  for(_expose_window_slot_t *slot = slots; slot && slot->window; slot++)
    {
      debug("slot: x=%jd, y=%jd, width=%ju, height=%ju",
	    (intmax_t) slot->extents.x, (intmax_t) slot->extents.y,
	    (uintmax_t) slot->extents.width, (uintmax_t) slot->extents.height);

      debug("scale_window: id=%jx, x=%jd, y=%jd, width=%ju, height=%ju",
	    (uintmax_t) slot->scale_window.window->id,
	    (intmax_t) slot->scale_window.window->geometry->x,
	    (intmax_t) slot->scale_window.window->geometry->y,
	    (uintmax_t) slot->scale_window.window->geometry->width,
	    (uintmax_t) slot->scale_window.window->geometry->height);
    }
#endif
}

/** Enable  the plugin  by  creating  the windows  slots  and map  the
 *  windows which are not already mapped, then fits the windows in the
 *  slots and create their Pixmap, and finally repaint the screen
 *
 * \param nwindows The numbers of windows on the screen
 * \return The newly allocated slots
 */
static _expose_window_slot_t *
_expose_plugin_enable(const uint32_t nwindows)
{
  unsigned int nwindows_per_strip;

  _expose_window_slot_t *new_slots = _expose_create_slots(nwindows,
							  &nwindows_per_strip);
  if(!new_slots)
    return NULL;

  _expose_assign_windows_to_slots(nwindows, nwindows_per_strip, new_slots);

  xcb_grab_server(globalconf.connection);

  /* Map windows which where  unmapped otherwise the window content is
     not guaranteed to be preserved while the window is unmapped */
  for(_expose_window_slot_t *slot = new_slots; slot && slot->window; slot++)
    if(slot->window->attributes->map_state != XCB_MAP_STATE_VIEWABLE &&
       !slot->scale_window.was_unmapped)
      {
	window_get_invisible_window_pixmap(slot->window);
	slot->scale_window.was_unmapped = true;
      }

  /* Process MapNotify event to get the NameWindowPixmap */
  xcb_aux_sync(globalconf.connection);
  /* TODO: get only MapNotify */
  xcb_event_poll_for_event_loop(&globalconf.evenths);

  xcb_ungrab_server(globalconf.connection);

  /* Grab the pointer in an  active way to avoid EnterNotify event due
     to the mapping hack */
  /* TODO: improve focus handling */
  xcb_grab_pointer_cookie_t grab_pointer_cookie =
    xcb_grab_pointer_unchecked(globalconf.connection, true, globalconf.screen->root,
			       XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC,
			       XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE,
			       XCB_CURRENT_TIME);

  /*  Grab the keyboard  in an  active way  to avoid  "weird" behavior
     (e.g. being  able to type in  a window which may  be not selected
     due  to  rescaling)  due   to  the  hack  consisting  in  mapping
     previously unmapped windows to get their Pixmap */
  xcb_grab_keyboard_cookie_t grab_keyboard_cookie =
    xcb_grab_keyboard_unchecked(globalconf.connection, true, globalconf.screen->root,
				XCB_CURRENT_TIME, XCB_GRAB_MODE_ASYNC,
				XCB_GRAB_MODE_ASYNC);
  
  _expose_prepare_windows(new_slots);

  xcb_grab_pointer_reply_t *grab_pointer_reply =
    xcb_grab_pointer_reply(globalconf.connection, grab_pointer_cookie, NULL);

  xcb_grab_keyboard_reply_t *grab_keyboard_reply =
    xcb_grab_keyboard_reply(globalconf.connection, grab_keyboard_cookie, NULL);

  if(!grab_pointer_reply || grab_pointer_reply->status != XCB_GRAB_STATUS_SUCCESS ||
     !grab_keyboard_reply || grab_keyboard_reply->status != XCB_GRAB_STATUS_SUCCESS)
    {
      warn("Can't grab the pointer and/or the keyboard");
      _expose_free_slots(&new_slots);
    }
  else
    /* The plugin is now enabled, so paint the screen */
    globalconf.do_repaint = true;

  free(grab_pointer_reply);
  free(grab_keyboard_reply);

  return new_slots;
}

/** Disable the  plugin by unmapping  the windows which  were unmapped
 *  before enabling the plugin and then repaint the screen again
 *
 * \param slots The windows slots
 */
static void
_expose_plugin_disable(_expose_window_slot_t *slots)
{
  /* Unmap the  window which were  previously mapped and  also restore
     override redirect */
  for(_expose_window_slot_t *slot = slots; slot && slot->window; slot++)
    if(slot->scale_window.was_unmapped)
      window_get_invisible_window_pixmap_finalise(slot->window);

  /* Now ungrab both the keyboard and the pointer */
  xcb_ungrab_keyboard(globalconf.connection, XCB_CURRENT_TIME);
  xcb_ungrab_pointer(globalconf.connection, XCB_CURRENT_TIME);

  /* Force repaint of the screen as the plugin is now disabled */
  _expose_global.enabled = false;
  globalconf.do_repaint = true;
}

/** When receiving a KeyRelease  event, just enable/disable the plugin
 *  if the plugin shortcuts key has been pressed and released
 *
 * \param event The X KeyPress event
 */
static void
expose_event_handle_key_release(xcb_key_release_event_t *event,
				window_t *window __attribute__((unused)))
{
  if(util_key_getkeysym(event->detail, event->state) != PLUGIN_KEY)
    return;

  if(_expose_global.enabled)
    _expose_plugin_disable(_expose_global.slots);
  else
    {
      /* Update the  atoms values  now if it  has been changed  in the
	 meantime */
      _expose_update_atoms_values(&_expose_global.atoms, &_expose_global.slots);

      /* Get  the number  of windows  actually managed  by  the window
	 manager (as given by _NET_CLIENT_LIST) */
      const uint32_t nwindows = _expose_global.atoms.client_list->windows_len;
      if(nwindows)
	{
	  _expose_global.slots = _expose_plugin_enable(nwindows);

	  if(!_expose_global.slots)
	    warn("Couldn't create the slots");
	  else
	    _expose_global.enabled = true;
	}
    }
}

static inline bool
_expose_in_window(const int16_t x, const int16_t y,
		  const window_t *window)
{
  return x >= window->geometry->x &&
    x < (int16_t) (window->geometry->x + window_width_with_border(window->geometry)) &&
    y >= window->geometry->y &&
    y < (int16_t) (window->geometry->y + window_height_with_border(window->geometry));
}

static void
expose_event_handle_button_release(xcb_button_release_event_t *event,
				   window_t *window __attribute__ ((unused)))
{
  for(uint32_t window_n = 0;
      window_n < _expose_global.atoms.client_list->windows_len;
      window_n++)
    {
      if(_expose_in_window(event->root_x, event->root_y,
			   _expose_global.slots[window_n].scale_window.window))
	{
	  _expose_plugin_disable(_expose_global.slots);

	  xcb_ewmh_request_change_active_window(globalconf.connection,
						_expose_global.slots[window_n].window->id,
						XCB_EWMH_CLIENT_SOURCE_TYPE_OTHER,
						event->time, XCB_NONE);

	  break;
	}
    }
}

static inline void
_expose_do_event_handle_property_notify(xcb_get_property_cookie_t (*get_property_func) (xcb_connection_t *),
					xcb_get_property_cookie_t *cookie)
{
  /* If a request has already  been sent without being retrieved, just
     free it before sending a new one */
  if(cookie->sequence)
    free(xcb_get_property_reply(globalconf.connection, *cookie, NULL));

  *cookie = (*get_property_func)(globalconf.connection);
}				  

/** When  receiving  PropertyNotify   of  either  _NET_CLIENT_LIST  or
 *  _NET_ACTIVE_WINDOW Atoms  Properties, send the request  to get the
 *  new value (but do not retrieve the reply yet, simply because it is
 *  not needed yet)
 *
 * \todo Perhaps it should be handle in the core code for the root window
 * \param event The X PropertyNotify event
 */
static void
expose_event_handle_property_notify(xcb_property_notify_event_t *event,
				    window_t *window __attribute__((unused)))
{
  if(event->atom == _NET_CLIENT_LIST)
    _expose_do_event_handle_property_notify(xcb_ewmh_get_client_list_unchecked,
					    &_expose_global.atoms.client_list_cookie);

  else if(event->atom == _NET_ACTIVE_WINDOW)
    _expose_do_event_handle_property_notify(xcb_ewmh_get_active_window_unchecked,
					    &_expose_global.atoms.active_window_cookie);
}				    

/** If the plugin is enabled, update the scaled Pixmap and then return
 *  the scaled windows objects
 *
 * \return The scaled windows list
 */
static window_t *
expose_render_windows(void)
{
  if(!_expose_global.enabled)
    return NULL;

  for(_expose_window_slot_t *slot = _expose_global.slots; slot && slot->window; slot++)
    {
      const uint16_t window_width = window_width_with_border(slot->window->geometry);
      const uint16_t window_height = window_height_with_border(slot->window->geometry);

      if(_expose_window_need_rescaling(&slot->extents, window_width, window_height))
	_expose_update_scale_pixmap(&slot->scale_window,
				    window_width_with_border(slot->scale_window.window->geometry),
				    window_height_with_border(slot->scale_window.window->geometry),
				    slot->window, window_width, window_height);
      else
	slot->scale_window.window->damaged = true;
    }

  return _expose_global.slots[0].scale_window.window;
}

/** Called on dlclose() and fee the memory allocated by this plugin */
static void __attribute__((destructor))
expose_destructor(void)
{
  if(_expose_global.atoms.client_list)
    {
      xcb_ewmh_get_windows_reply_wipe(_expose_global.atoms.client_list);
      free(_expose_global.atoms.client_list);
    }

  free(_expose_global.atoms.active_window);
  _expose_free_slots(&_expose_global.slots);
}

/** Structure holding all the functions addresses */
plugin_vtable_t plugin_vtable = {
  .name = "expose",
  .events = {
    NULL,
    NULL,
    expose_event_handle_key_release,
    expose_event_handle_button_release,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    expose_event_handle_property_notify
  },
  .check_requirements = expose_check_requirements,
  .window_manage_existing = NULL,
  .window_get_opacity = NULL,
  .render_windows = expose_render_windows
};
