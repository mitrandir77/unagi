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
 *  \brief Rendering backends management
 */

#include <dlfcn.h>

#include "rendering.h"
#include "structs.h"
#include "plugin_common.h"
#include "util.h"

/** Load the  default backend or fallback  on another one  if there is
 *  any error
 *
 * \return True if a rendering backend was successfully loaded
 */
bool
rendering_load(void)
{
  /* Clear any existing error */
  dlerror();

  globalconf.rendering_dlhandle = plugin_common_dlopen(globalconf.rendering_dir,
						       cfg_getstr(globalconf.cfg, "rendering"));

  char *error;
  if((error = dlerror()))
    {
      fatal_no_exit("Can't load rendering backend: %s", error);
      return false;
    }

  /* Get the backend functions addresses given in a structure in it */
  globalconf.rendering = dlsym(globalconf.rendering_dlhandle,
			       "rendering_functions");

  if((error = dlerror()))
    {
      fatal_no_exit("%s", error);
      return false;
    }

  return true;
}

/** Unload the rendering backend */
void
rendering_unload(void)
{
  if(!globalconf.rendering_dlhandle)
    return;

  dlclose(globalconf.rendering_dlhandle);
}
