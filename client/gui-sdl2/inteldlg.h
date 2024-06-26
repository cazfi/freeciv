/***********************************************************************
 Freeciv - Copyright (C) 2003 - The Freeciv Project
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/
#ifndef FC__INTELDLG_H
#define FC__INTELDLG_H

/* client */
#include "inteldlg_g.h"

void intel_dialog_init(void);
void intel_dialog_done(void);

void popdown_intel_dialog(struct player *p);
void popdown_intel_dialogs(void);

#endif /* FC__INTELDLG_H */
