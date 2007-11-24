/*
 * Global definitions to be included in ALL source files
 *
 * This file is part of ANT (Ant is Not a Telephone)
 *
 * Copyright 2007 Ivan Schreter
 *
 * ANT is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * ANT is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ANT; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "globals.h"

#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>

/*--------------------------------------------------------------------------*/

void msgprintf(int level _U_, const char *format, ...)
{
  va_list args;
  va_start(args, format);

  /* for now just dump to stderr, TODO maybe make some elaborate UI for debug log */
  vfprintf(stderr, format, args);

  va_end(args);
}

/*--------------------------------------------------------------------------*/
