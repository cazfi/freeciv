#!/bin/bash

# Install homebrew for building MacOS freeciv .app
# WIP

# Can be "master", if you want development version
HBVER="3.4.6"

if test "$1" = "" ; then
  echo "No installation directory given." >&2
  exit 1
fi

MAINDIR="$1"

if ! mkdir -p "${MAINDIR}/Contents" ; then
  echo "Failed to create \"${MAINDIR}/Contents\"" >&2
  exit 1
fi

if ! mkdir -p "${MAINDIR}/Resources" ; then
  echo "Failed to create \"${MAINDIR}/Resources\"" >&2
  exit 1
fi

if ! mkdir -p "${MAINDIR}/MacOS" ; then
  echo "Failed to create \"${MAINDIR}/MacOS\"" >&2
  exit 1
fi

if ! curl -L "https://github.com/Homebrew/brew/tarball/${HBVER}" |
     tar xz --strip 1 -C "${MAINDIR}/Resources"
then
  echo "Homebrew install failed" >&2
  exit 1
fi

eval "$("${MAINDIR}/Resources/bin/brew" shellenv)"

# Packages that we temporarily take from HEAD
# - shared-mime-info : current version available from homebrew does not build with recent meson
# - meson : version available from homebrew fails building latest gtk+3
HEAD_PACKAGES="\
 shared-mime-info \
 meson \
"

PACKAGES="\
 lua@5.4 \
 autoconf \
 automake \
 gettext \
 libtool \
 pkg-config \
 atk \
 freetype \
 gettext \
 glib \
 gtk+3 \
 icu4c \
 pango \
 sdl2_gfx \
 sdl2_image \
 sdl2_mixer \
 sdl2_ttf \
 qt@6 \
"

if test "$HEAD_PACKAGES" != "" ; then
  if ! brew install --HEAD $HEAD_PACKAGES ; then
    echo "Homebrew install of HEAD packages failed" >&2
    exit 1
  fi
fi

if ! brew fetch $PACKAGES ; then
  echo "Homebrew fetching packages failed." >&2
  exit 1
fi

if ! brew install $PACKAGES ; then
  echo "Homebrew packages installation failed." >&2
  exit 1
fi
