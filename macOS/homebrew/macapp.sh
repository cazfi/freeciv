#!/bin/bash

# Build MacOS freeciv .app
# WIP

# Parts of this can be tested on linux too.
if test "$(uname)" = "Linux" ; then
  APPROOT="$HOME/homebrew"
else
  APPROOT="/Applications"
fi

cd "$(dirname "$0")" || exit 1

SRCROOT="$(cd ../.. && pwd)"

if ! test -x "${SRCROOT}/fc_version" ; then
  echo "No executable ${SRCROOT}/fcversion" >&2
  exit 1
fi

FCVER="$("${SRCROOT}/fc_version")"

if test "$1" != "" ; then
  MAINDIR="$1"
else
  MAINDIR="${APPROOT}/freeciv-${FCVER}.app"
fi

if ! test -d "${MAINDIR}" ; then
  if ! ./hbinstall.sh "${MAINDIR}" ; then
    echo "Homebrew environment installation to \"${MAINDIR}\" failed" >&2
    exit 1
  fi
else
  echo "\${MAINDIR}\" exist. Skipping homebrew install"
fi

if ! ./build_freeciv.sh "${MAINDIR}" ; then
  echo "Freeciv build failed" >&2
  exit 1
fi
