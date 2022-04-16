#!/bin/bash

FCVER="$(./fc_version)"

MAINDIR="/Applications/freeciv-${FCVER}.app"

if ! mkdir -p "$MAINDIR/Contents" ; then
  echo "Failed to create \"$MAINDIR/Contents\"" >&2
  exit 1
fi

if ! mkdir -p "$MAINDIR/Resources" ; then
  echo "Failed to create \"$MAINDIR/Resources\"" >&2
  exit 1
fi

if ! mkdir -p "$MAINDIR/MacOS" ; then
  echo "Failed to create \"$MAINDIR/MacOS\"" >&2
  exit 1
fi

if ! curl -L https://github.com/Homebrew/brew/tarball/master |
     tar xz --strip 1 -C "$MAINDIR/Resources"
then
  echo "Homebrew install failed" >&2
  exit 1
fi

eval "$($MAINDIR/Resources/bin/brew shellenv)"
command -p brew
brew update --force --quiet

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
 gtk4 \
 icu4c \
 pango \
 sdl2_gfx \
 sdl2_image \
 sdl2_mixer \
 sdl2_ttf \
 qt@6 \
"

if ! brew install $PACKAGES ; then
  echo "Homebrew packages installation failed." >&2
  exit 1
fi

if ! ./configure --prefix="${MAINDIR}/Contents" --bindir="${MAINDIR}/MacOS" \
 PATH="${MAINDIR}/Contents/Resources/bin:${MAINDIR}/Contents/Resources/opt/qt5/bin/:/usr/bin:/bin:/usr/sbin:/sbin:/Library/Apple/usr/bin" \
 LDFLAGS="-L${MAINDIR}/Contents/Resources/opt/qt6/lib -L${MAINDIR}/Contents/Resources/lib" \
 PKG_CONFIG_PATH="${MAINDIR}/Contents/Resources/opt/icu4c/lib/pkgconfig:$PKG_CONFIG_PATH" \
 --enable-client=gtk3.22,qt6,sdl2 --enable-fcmp=gtk3,qt
then
  echo "Freeciv configure failed!" >&2
  exit 1
fi

if ! make install ; then
  echo "Freeciv make (install) failed!" >&2
  exit 1
fi
