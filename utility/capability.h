/***********************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__CAPABILITY_H
#define FC__CAPABILITY_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "support.h"            /* bool type */

bool has_capability(const char *cap, const char *capstr)
  fc__attribute((nonnull (1, 2)));
bool has_capabilities(const char *us, const char *them)
  fc__attribute((nonnull (1, 2)));

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* FC__CAPABILITY_H */
