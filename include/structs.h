/* -*-mode:c;coding:utf-8; c-basic-offset:2;fill-column:70;c-file-style:"gnu"-*-
 *
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
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xfixes.h>

#include <confuse.h>
#include <ev.h>

#include "window.h"
#include "rendering.h"
#include "plugin.h"
#include "atoms.h"
#include "util.h"

/** Hold information related to the X extension */
typedef struct _display_extensions_t
{
  /** The Composite extension information */
  const xcb_query_extension_reply_t *composite;
  /** The XFixes extension information */
  const xcb_query_extension_reply_t *xfixes;
  /** The Damage extension information */
  const xcb_query_extension_reply_t *damage;
  /** The RandR extension information */
  const xcb_query_extension_reply_t *randr;
} display_extensions_t;

/** Repaint interval to 20ms (50Hz) if  it could not have been obtained
    from RandR */
#define DEFAULT_REPAINT_INTERVAL 0.02

/** Minimum value for the repaint interval, 10ms (200Hz), used on
    startup if the refresh rate is too high and when determining the
    repaint interval according to the painting time */
#define MINIMUM_REPAINT_INTERVAL 0.01

/** Global structure holding variables used all across the program */
typedef struct _conf_t
{
  /** libev event loop */
  struct ev_loop *event_loop;
  /** libev I/O watcher on XCB FD, invoked in paint callback to ensure
      that no events have been queued while calling the callback */
  ev_io event_io_watcher;
  /** libev paint timer watcher to be reset according to the painting
      average time */
  ev_timer event_paint_timer_watcher;

  /** The XCB connection structure */
  xcb_connection_t *connection;
  /** The screen number as defined by the protocol */
  int screen_nbr;
  /** The screen information */
  xcb_screen_t *screen;
  /** Maximum painting interval in seconds (from screen refresh rate) */
  float refresh_rate_interval;
  /** Repaint interval computed from the painting time average */
  float repaint_interval;
  /** Sum of all painting times (for calculating the global average) */
  float paint_time_sum;
  /** Numbre of paintings (for calculating the global average) */
  unsigned int paint_counter;
  /** EWMH-related information */
  xcb_ewmh_connection_t ewmh;
  /** The X extensions information */
  display_extensions_t extensions;
  /** The Window specific to the compositing manager */
  xcb_window_t cm_window;
  /** The list of all windows as objects */
  window_t *windows;
  /** Binary Trees used for lookups (The list is still useful for stack order) */
  util_itree_t *windows_itree;
  /** Damaged region which must be repainted */
  xcb_xfixes_region_t damaged;
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
