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

#include <dlfcn.h>
#include <string.h>
#include <stdlib.h>

#include "plugin.h"
#include "structs.h"
#include "util.h"
#include "plugin_common.h"

/** Load the plugin with the given name
 *
 * \param name The plugin name
 * \return The plugin loaded or NULL if any error
 */
plugin_t *
plugin_load(const char *name)
{
  plugin_t *new_plugin = calloc(1, sizeof(plugin_t));
  char *error;

  /* Clear any existing error */
  dlerror();

  /* Open the plugin in the  plugins directory given as a command line
     parameter or the default path set during compilation */
  new_plugin->dlhandle = plugin_common_dlopen(globalconf.plugins_dir, name);
  if((error = dlerror()))
    goto plugin_load_error;

  /* Load the virtual table of  the plugins containing the pointers to
     the plugins functions */
  new_plugin->vtable = dlsym(new_plugin->dlhandle, "plugin_vtable");
  if((error = dlerror()))
    goto plugin_load_error;

  debug("Plugin %s loaded", name);
  return new_plugin;	  

 plugin_load_error:
  debug("Can't load plugin %s", name);
  fatal_no_exit(error);
  free(new_plugin);
  return NULL;
}

/** Load all the plugins given in the configuration file */
void
plugin_load_all(void)
{
  const unsigned int plugins_nb = cfg_size(globalconf.cfg, "plugins");
  if(!plugins_nb)
    return;

  plugin_t *plugin = globalconf.plugins;
  for(unsigned int plugin_n = 0; plugin_n < plugins_nb; plugin_n++)
    {
      plugin_t *new_plugin = plugin_load(cfg_getnstr(globalconf.cfg, "plugins", plugin_n));
      if(!new_plugin)
	continue;

      if(!globalconf.plugins)
	globalconf.plugins = plugin = new_plugin;
      else
	{
	  plugin->next = new_plugin;
	  plugin->next->prev = plugin;
	  plugin = plugin->next;
	}
    }
}

void
plugin_check_requirements(void)
{
  for(plugin_t *plugin = globalconf.plugins; plugin; plugin = plugin->next)
    plugin->enable = (!plugin->vtable->check_requirements ? true :
		     (*plugin->vtable->check_requirements)());
}

/** Look for a plugin from its name
 *
 * \param name The plugin name
 * \return Return the plugin or NULL
 */
plugin_t *
plugin_search_by_name(const char *name)
{
  for(plugin_t *plugin = globalconf.plugins; plugin; plugin = plugin->next)
    if(strcmp(plugin->vtable->name, name) == 0)
      return plugin;

  return NULL;
}

/** Unload the given plugin and free the associated memory
 *
 * \param plugin A pointer to the plugin to be freed
 */
void
plugin_unload(plugin_t **plugin, const bool do_update_list)
{
  if(do_update_list)
    {
      if((*plugin)->prev)
	(*plugin)->prev->next = (*plugin)->next;
      else
	globalconf.plugins = (*plugin)->next;
    }

  dlclose((*plugin)->dlhandle);
  free(*plugin);
}

/** Unload all the plugins and their memory */
void
plugin_unload_all(void)
{
  plugin_t *plugin = globalconf.plugins;
  plugin_t *plugin_next;

  while(plugin != NULL)
    {
      plugin_next = plugin->next;
      plugin_unload(&plugin, false);
      plugin = plugin_next;
    }
}
