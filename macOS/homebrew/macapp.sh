#!/bin/bash

# Build MacOS freeciv .app
# WIP

# Parts of this can be tested on linux too.
if test "$(uname)" = "Linux" ; then
  APPROOT="$HOME/homebrew"
  CORES="$(nproc)"
else
  APPROOT="/Applications"
  CORES="$(sysctl -n hw.logicalcpu)"
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

eval "$("${MAINDIR}/Resources/bin/brew" shellenv)"

# Standard naming of the tools (gmake -> make etc)
export PATH="${MAINDIR}/Resources/opt/libtool/libexec/gnubin:$PATH"

if ! mkdir -p build ; then
  echo "Failed to create the build directory" >&2
  exit 1
fi

cd build || exit 1

export PATH="${MAINDIR}/Contents/Resources/bin:${MAINDIR}/Contents/Resources/opt/icu4c/bin/:${MAINDIR}/Contents/Resources/opt/qt6/bin/:$PATH"
export PKG_CONFIG_PATH="${MAINDIR}/Contents/Resources/opt/icu4c/lib/pkgconfig:$PKG_CONFIG_PATH"
export LIBS="-L${MAINDIR}/Contents/Resources/opt/qt6/lib -L${MAINDIR}/Contents/Resources/lib"

if ! test -x "${SRCROOT}/configure" ; then
  # Assume this to be a git checkout - generate required bootstrap files
  if ! "${SRCROOT}/autogen.sh" --no-configure-run ; then
    echo "Autogen.sh failed" >&2
    exit 1
  fi
fi

# TODO: Find out where the command really is, and get rid of this search
export MOCCMD="$(find "${MAINDIR}/Resources/Cellar/qt" -name "moc" | head -n 1)"

if ! "${SRCROOT}/configure" --prefix="${MAINDIR}/Contents" --bindir="${MAINDIR}/MacOS" \
     --enable-client=gtk3.22,qt,sdl2 --enable-fcmp=gtk3,qt
then
  echo "Freeciv configure failed!" >&2
  cat config.log
  exit 1
fi

if ! make "-j${CORES}" ; then
  echo "Freeciv make failed!" >&2
  exit 1
fi

if ! make install ; then
  echo "Freeciv make install failed!" >&2
  exit 1
fi
