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
 *  \brief General structures definitions
 */

#ifndef STRUCTS_H
#define STRUCTS_H

#include <xcb/xcb.h>
#include <xcb/xcb_event.h>
#include <xcb/xcb_keysyms.h>

#include <confuse.h>

#include "window.h"
#include "rendering.h"
#include "plugin.h"
#include "atoms.h"

/** Hold information related to the X extension
typedef struct _display_extensions_t
{
  /** The Composite extension information */
  const xcb_query_extension_reply_t *composite;
  /** The XFixes extension information */
  const xcb_query_extension_reply_t *xfixes;
  /** The Damage extension information */
  const xcb_query_extension_reply_t *damage;
} display_extensions_t;

/** Global structure holding variables used all across the program */
typedef struct _conf_t
{
  /** The XCB connection structure */
  xcb_connection_t *connection;
  /** The screen number as defined by the protocol */
  int screen_nbr;
  /** The screen information */
  xcb_screen_t *screen;
  /** The X extensions information */
  display_extensions_t extensions;
  /** Registered event handlers as provided by xcb-event */
  xcb_event_handlers_t evenths;
  /** The Window specific to the compositing manager */
  xcb_window_t cm_window;
  /** The list of all windows as objects */
  window_t *windows;
  /** Specify whether the content of the screen should be updated */
  bool do_repaint;
  /** Confuse configuration file options */
  cfg_t *cfg;
  /** List of KeySyms, only updated when receiving a KeyboardMapping event */
  xcb_key_symbols_t *keysyms;

  /** Hold _NET_SUPPORTED atom */
  struct
  {
    /** _NET_SUPPORTED reply value */
    xcb_ewmh_get_atoms_reply_t value;
    /** _NET_SUPPORTED request cookie */
    xcb_get_property_cookie_t cookie;
    /** Specify whether this property has been set */
    bool initialised;
  } atoms_supported;

  /** Path to the rendering backends directory */
  char *rendering_dir;
  /** dlopen() opaque structure for the rendering backend */
  void *rendering_dlhandle;
  /** */
  rendering_t *rendering;

  /** Path to the effects plugins directory */
  char *plugins_dir;
  /** List of plugins enabled in the configuration file */
  plugin_t *plugins;

  /** Keyboard masks values meaningful on KeyPress/KeyRelease event */
  struct
  {
    uint16_t numlock;
    uint16_t shiftlock;
    uint16_t capslock;
    uint16_t modeswitch;
  } key_masks;

} conf_t;

extern conf_t globalconf;

#endif
