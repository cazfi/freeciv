#!/usr/bin/env bash
#/***********************************************************************
# Freeciv - Copyright (C) 2017-2025
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2, or (at your option)
#   any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#***********************************************************************/

SRCDIR="$(cd $(dirname $0) ; pwd)"

(
  echo "# Generated by generate_ruleset_loads.sh."
  echo
  echo "# ruleset_loads.sh ruleset"
  echo "# Exits with 0 when the specified ruleset is able to load. Exits with 1 if"
  echo "# it fails to load."
  echo
  echo "(echo \"lua unsafe-file ${SRCDIR}/ruleset_is.lua\" |"
  echo " (EXPECTED_RULESET=\$1 ./run.sh freeciv-server -F --Announce none --ruleset \$1)) ||"
  echo "exit 1"
  echo
  echo "# The ruleset was able to load."
  echo "exit 0"
) > $1

chmod +x "$1"
