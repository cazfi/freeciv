#!/bin/bash
#
# Freeciv CI Script
#
# https://github.com/freeciv/freeciv/actions
echo "Running CI job $1 for Freeciv."
basedir=$(pwd)
logfile="${basedir}/freeciv-CI.log"


# Redirect copy of output to a log file.
exec > >(tee ${logfile})
exec 2>&1
set -e

uname -a

case $1 in
"dist")
mkdir build
cd build
../autogen.sh \
 || (let config_exit_status=$? \
     && echo "Config exit status: $config_exit_status" \
     && cat config.log \
     && exit $config_exit_status)
make -s -j$(nproc) distcheck
echo "Freeciv distribution check successful!"
;;

"meson")
mkdir build
cd build
meson .. -Dprefix=${HOME}/freeciv/meson -Ddebug=true -Dack_experimental=true -Dclients='gtk3.22','qt','sdl2' -Dfcmp='gtk3','qt','cli'
ninja
ninja install
;;

"os_x")
# gcc is an alias for clang on OS X

./macOS/homebrew/macapp.sh
;;

"clang_debug")
# Configure and build Freeciv
mkdir build
cd build
../autogen.sh \
 CC="clang" \
 CXX="clang++" \
 --enable-debug \
 --enable-sys-lua \
 --enable-sys-tolua-cmd \
 --disable-fcdb \
 --with-qtver=qt5 \
 --enable-client=gtk3.22,qt,sdl2,stub \
 --enable-fcmp=cli,gtk3,qt \
 --enable-freeciv-manual \
 --enable-ai-static=classic,threaded,tex,stub \
 --prefix=${HOME}/freeciv/clang \
 || (let config_exit_status=$? \
     && echo "Config exit status: $config_exit_status" \
     && cat config.log \
     && exit $config_exit_status)
make -s -j$(nproc)
make install
;;

*)
# Fetch S3_1 in the background for the ruleset upgrade test
git fetch --no-tags --quiet https://github.com/freeciv/freeciv.git S3_1:S3_1 &

# Configure and build Freeciv
mkdir build
cd build
../autogen.sh \
 CFLAGS="-O3" \
 CXXFLAGS="-O3" \
 --with-qtver=qt5 \
 --enable-client=gtk3.22,qt,sdl2,stub \
 --enable-fcmp=cli,gtk3,qt \
 --enable-freeciv-manual \
 --enable-ruledit=experimental \
 --enable-ai-static=classic,threaded,tex,stub \
 --enable-fcdb=sqlite3,mysql \
 --prefix=${HOME}/freeciv/default \
 || (let config_exit_status=$? \
     && echo "Config exit status: $config_exit_status" \
     && cat config.log \
     && exit $config_exit_status)
make -s -j$(nproc)
make install
echo "Freeciv build successful!"

# Check that each ruleset loads
echo "Checking rulesets"
./tests/rulesets_not_broken.sh

# Check ruleset saving
echo "Checking ruleset saving"
./tests/rulesets_save.sh

# Check ruleset upgrade
echo "Ruleset upgrade"
echo "Preparing test data"
../tests/rs_test_res/upgrade_ruleset_sync.bash
echo "Checking ruleset upgrade"
./tests/rulesets_upgrade.sh

# Check ruleset autohelp generation
echo "Checking ruleset auto help generation"
./tests/rulesets_autohelp.sh

echo "Running Freeciv server autogame"
cd ${HOME}/freeciv/default/bin/
./freeciv-server --Announce none -e --read ${basedir}/scripts/test-autogame.serv

echo "Freeciv server autogame successful!"
;;
esac
