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
#ifndef FC__DAIPARADROP_H
#define FC__DAIPARADROP_H

struct adv_choice;
struct city;
struct player;
struct unit;

void dai_manage_paratrooper(struct ai_type *ait, struct player *pplayer,
                            struct unit *punit);
void dai_choose_paratrooper(struct ai_type *ait, const struct civ_map *nmap,
                            struct player *pplayer,
                            struct city *pcity, struct adv_choice *choice,
                            bool allow_gold_upkeep);

#endif /* FC__DAIPARADROP_H */
