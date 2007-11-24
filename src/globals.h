/*
 * Global definitions to be included in ALL source files
 *
 * This file is part of ANT (Ant is Not a Telephone)
 *
 * Copyright 2002, 2003 Roland Stigge
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

#ifndef _ANT_GLOBALS_H
#define _ANT_GLOBALS_H

#include "config.h"

/* GNU gettext introduction */
#include <locale.h>
#include "gettext.h"
#define _(String) gettext (String)
#define N_(String) gettext_noop (String)

extern int debug;

/*!
 * @brief Output a message.
 *
 * @param level message level (0=error, 1..n=debug).
 * @param format printf-like format for following arguments.
 */
void msgprintf(int level, const char *format, ...)
    __attribute__ ((format (printf, 2, 3)));

#define dbgprintf(level, ...) \
  if (level <= debug) msgprintf(level, __VA_ARGS__)

#define errprintf(...) \
  msgprintf(0, __VA_ARGS__)

#endif /* globals.h */
