#!/bin/bash

# Build MacOS freeciv .app
# WIP

# "master" works too, if you want development version
HBVER="3.4.6"

# Parts of this can be tested on linux too.
if test "$(uname)" = "Linux" ; then
  APPROOT="$HOME/homebrew"
else
  APPROOT="/Applications"
fi

cd "$(dirname $0)"

SRCROOT="$(cd ../.. && pwd)"

if ! test -x "${SRCROOT}/fc_version" ; then
  echo "No executable ${SRCROOT}/fcversion at $(pwd)" >&2
  exit 1
fi

FCVER="$(${SRCROOT}/fc_version)"

MAINDIR="${APPROOT}/freeciv-${FCVER}.app"

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

if ! curl -L https://github.com/Homebrew/brew/tarball/$HBVER |
     tar xz --strip 1 -C "$MAINDIR/Resources"
then
  echo "Homebrew install failed" >&2
  exit 1
fi

eval "$($MAINDIR/Resources/bin/brew shellenv)"

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
 gtk4 \
 gtk+3 \
 icu4c \
 pango \
 sdl2_gfx \
 sdl2_image \
 sdl2_mixer \
 sdl2_ttf \
 qt@6 \
"

if ! brew fetch $PACKAGES ; then
  echo "Homebrew fetching packages failed." >&2
  exit 1
fi

if ! brew install $PACKAGES ; then
  echo "Homebrew packages installation failed." >&2
  exit 1
fi

# Standard naming of the tools (gmake -> make etc)
export PATH="${MAINDIR}/Resources/opt/libtool/libexec/gnubin:$PATH"

if ! mkdir -p build ; then
  echo "Failed to create the build directory" >&2
  exit 1
fi

cd build

if ! test -x ${SRCROOT}/configure ; then
  # Assume this to be a git checkout - generate required bootstrap files
  if ! ${SRCROOT}/autogen.sh --no-configure-run ; then
    echo "Autogen.sh failed" >&2
    exit 1
  fi
fi

if ! ${SRCROOT}/configure --prefix="${MAINDIR}/Contents" --bindir="${MAINDIR}/MacOS" \
 PATH="${MAINDIR}/Contents/Resources/bin:${MAINDIR}/Contents/Resources/opt/qt6/bin/:/usr/bin:/bin:/usr/sbin:/sbin:/Library/Apple/usr/bin" \
 LDFLAGS="-L${MAINDIR}/Contents/Resources/opt/qt6/lib -L${MAINDIR}/Contents/Resources/lib" \
 PKG_CONFIG_PATH="${MAINDIR}/Contents/Resources/opt/icu4c/lib/pkgconfig:$PKG_CONFIG_PATH" \
 --enable-client=gtk3.22,qt,sdl2 --enable-fcmp=gtk3,qt
then
  echo "Freeciv configure failed!" >&2
  exit 1
fi

if ! make -j$(sysctl -n hw.logicalcpu) ; then
  echo "Freeciv make failed!" >&2
  exit 1
fi

if ! make install ; then
  echo "Freeciv make install failed!" >&2
  exit 1
fi
