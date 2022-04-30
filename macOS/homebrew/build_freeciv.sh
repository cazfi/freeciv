#!/bin/bash

# Build freeciv to homebrew environment

# Parts of this can be tested on linux too.
if test "$(uname)" = "Linux" ; then
  CORES="$(nproc)"
else
  CORES="$(sysctl -n hw.logicalcpu)"
fi

cd "$(dirname "$0")" || exit 1

SRCROOT="$(cd ../.. && pwd)"

if test "$1" != "" ; then
  MAINDIR="$1"
else
  echo "No maindir given" >&2
  exit 1
fi

if ! test -d "${MAINDIR}/Contents/Resources" ; then
  echo "\"${MAINDIR}\" doesn't look like proper homebrew environment." >&2
  exit 1
fi

eval "$("${MAINDIR}/Contents/Resources/bin/brew" shellenv)"

# Standard naming of the tools (gmake -> make etc)
export PATH="${MAINDIR}/Contents/Resources/opt/libtool/libexec/gnubin:$PATH"

if ! mkdir -p build ; then
  echo "Failed to create the build directory" >&2
  exit 1
fi

cd build || exit 1

export PATH="${MAINDIR}/Contents/Resources/bin:$(brew --prefix qt@6)/bin:$PATH"
export PKG_CONFIG_PATH="$(brew --prefix icu4c)/lib/pkgconfig:$PKG_CONFIG_PATH"
export LIBS="-L$(brew --prefix qt@6)/lib -L${MAINDIR}/Contents/Resources/lib"
export CPPFLAGS="-I$(brew --prefix qt@6)/include $CPPFLAGS"

if ! test -x "${SRCROOT}/configure" ; then
  # Assume this to be a git checkout - generate required bootstrap files
  if ! "${SRCROOT}/autogen.sh" --no-configure-run ; then
    echo "Autogen.sh failed" >&2
    exit 1
  fi
fi

# TODO: Find out where the command really is, and get rid of this search
export MOCCMD="$(find "${MAINDIR}/Contents/Resources" -name "moc" | head -n 1)"

# This sed assumes that the only space is between the name and the version number.
# Basing this to a detection of numbers would break for a name like "qt6"
QT6VERDIR="$(brew ls --versions qt@6 | sed 's/.* //')"

if ! "${SRCROOT}/configure" --prefix="${MAINDIR}/Contents" \
     --with-qt6-framework="${MAINDIR}/Contents/Resources/Cellar/qt/${QT6VERDIR}" \
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
