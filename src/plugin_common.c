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
#include <stdio.h>
#include <string.h>

#include "plugin_common.h"

/** Compute  the plugin  location by  concatenating the  directory and
 *  plugin name and then call dlopen()
 *
 * \param dir The plugin directory
 * \param name The plugin name
 * \return Handle for the plugin
 */
void *
plugin_common_dlopen(const char *dir, const char *name)
{
  /* Get the length of the plugin filename */
  const size_t path_len = strlen(name) + strlen(dir) + sizeof(".so");

  /* Get the actual plugin filename */
  char path[path_len];
  snprintf(path, path_len, "%s%s.so", dir, name);

  return dlopen(path, RTLD_LAZY);
}
