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
 *  \brief Effects plugins
 *
 *  General Plugins architecture:
 *
 *  Several plugins  may be loaded at  the same time.   Each plugin is
 *  defined in a 'plugin_t' structure holding the virtual table of the
 *  plugin itself ('plugin_vtable_t').
 *
 *  Each plugin has  to defined 'plugin_vtable_t plugin_vtable', which
 *  is  a virtual  table  containing the  plugin  name, general  hooks
 *  pointer and events hooks pointer ('plugin_events_notify_t').  This
 *  way, each plugin  can register one or several  hooks, run when the
 *  main  program receives  an event  notification, by  simply setting
 *  function pointers in this structure.
 *
 *  NOTE: On  startup, the constructor routine  (dlopen()) should only
 *  allocate  memory but  not  send any  X  request as  this would  be
 *  usually done by 'window_manage_existing' hook.
 */

#ifndef PLUGIN_H
#define PLUGIN_H

#include <stdbool.h>
#include <stdint.h>

#include <xcb/xcb.h>
#include <xcb/damage.h>

#include "window.h"

/** Plugin structure holding all the supported event handlers */
typedef struct
{
  /** DamageNotify event */
  void (*damage) (xcb_damage_notify_event_t *, window_t *);
  /** KeyPress event */
  void (*key_press) (xcb_key_press_event_t *, window_t *);
  /** KeyRelease event */
  void (*key_release) (xcb_key_release_event_t *, window_t *);
  /** ButtonRelease event */
  void (*button_release) (xcb_button_release_event_t *, window_t *);
  /** CirculateNotify event */
  void (*circulate) (xcb_circulate_notify_event_t *, window_t *);
  /** ConfigureNotify event */
  void (*configure) (xcb_configure_notify_event_t *, window_t *);
  /** CreateNotify event */
  void (*create) (xcb_create_notify_event_t *, window_t *);
  /** DestroyNotify event */
  void (*destroy) (xcb_destroy_notify_event_t *, window_t *);
  /** MapNotify event */
  void (*map) (xcb_map_notify_event_t *, window_t *);
  /** ReparentNotify event */
  void (*reparent) (xcb_reparent_notify_event_t *, window_t *);
  /** UnmapNotify event */
  void (*unmap) (xcb_unmap_notify_event_t *, window_t *);
  /** PropertyNotify event */
  void (*property) (xcb_property_notify_event_t *, window_t *);
} plugin_events_notify_t;

/** Plugin virtual table */
typedef struct
{
  /** Plugin name */
  const char *name;
  /** Plugin events hooks */
  plugin_events_notify_t events;
  /** Called before the main loop to check the plugin requirements */
  bool (*check_requirements)(void);
  /** Hook called when managing the window on startup */
  void (*window_manage_existing)(const int, window_t **);
  /** Hook to get the opacity of the given window */
  uint16_t (*window_get_opacity)(const window_t *);
  /** Hook to allow plugins to provide their own windows */
  window_t *(*render_windows)(void);
} plugin_vtable_t;

/** Plugin list element */
typedef struct _plugin_t
{
  /** Opaque "handle" for the plugin */
  void *dlhandle;
  /** If the plugin requirements have been met */
  bool enable;
  /** Plugin virtual table */
  plugin_vtable_t *vtable;
  /** Pointer to the previous plugin */
  struct _plugin_t *prev;
  /** Pointer to the next plugin */
  struct _plugin_t *next;
} plugin_t;

/** Call the appropriate event handlers according to the event type */
#define PLUGINS_EVENT_HANDLE(event, event_type, window)			\
  for(plugin_t *plugin = globalconf.plugins; plugin;			\
      plugin = plugin->next)						\
    {									\
      if(plugin->enable && plugin->vtable->events.event_type)		\
	(*plugin->vtable->events.event_type)(event, window);		\
    }

plugin_t *plugin_load(const char *);
void plugin_load_all(void);
void plugin_check_requirements(void);
plugin_t *plugin_search_by_name(const char *);
void plugin_unload(plugin_t **, const bool);
void plugin_unload_all(void);

#endif
