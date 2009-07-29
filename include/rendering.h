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

/** Rendering backends architecture:
 *  ================================
 *
 *  For obvious reasons, only one  rendering backend is enabled at the
 *  same  time and  is only  a layer  on top  of the  actual rendering
 *  library  used (at  the moment  XRender) to  allow  writing another
 *  backend easily.
 *
 *  Each  backend  exports  a  'render_t  rendering_functions'  global
 *  variable which  defines all the members of  the render_t structure
 *  given below.
 */

#ifndef RENDERING_H
#define RENDERING_H

#include <stdbool.h>
#include <stdint.h>

#include "window.h"

/** Functions exported by the rendering backend */
typedef struct
{
  /** Initialisation routine */
  bool (*init) (void);
  /** Second step of the initialisation routine */
  bool (*init_finalise) (void);
  /** Reset the root Window background */
  void (*reset_background) (void);
  /** Paint the root background to the root window */
  void (*paint_background) (void);
  /** Paint a given window */
  void (*paint_window) (window_t *);
  /** Paint all the windows on the root window */
  void (*paint_all) (void);
  /** Check whether the given request is backend-specific */
  bool (*is_request) (const uint8_t);
  /** Get the request label of a backend request */
  const char *(*get_request_label) (const uint16_t);
  /** Get the error label of a backend error */
  const char *(*get_error_label) (const uint8_t);
  /** Free resources associated with a window when the Pixmap is freed */
  void (*free_window_pixmap) (window_t *);
  /** Free resources associated with a window */
  void (*free_window) (window_t *);
} rendering_t;

bool rendering_load(void);
void rendering_unload(void);

#endif
