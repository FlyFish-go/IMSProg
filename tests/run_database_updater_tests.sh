#!/bin/sh
# Exercise the actual Qt updater, using finished fake replies and a temporary database.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
appbuild=${1:-"$root/build-linux"}
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
: "${CXX:=c++}"
objects=$(find "$appbuild" -path '*/CMakeFiles/IMSProg_database_update.dir/*' -name '*.o' ! -name 'main.cpp.o')
[ -n "$objects" ] || { echo 'Build the database updater first.' >&2; exit 1; }
$CXX -std=c++17 -fPIC $(pkg-config --cflags Qt5Widgets Qt5Network) \
    -I"$root/IMSProg_database_update" "$root/tests/database_updater_test.cpp" $objects \
    -L"$(pkg-config --variable=libdir Qt5Core)" $(pkg-config --libs Qt5Widgets Qt5Network) -o "$build/updater-test"
mkdir -p "$build/runtime"
chmod 700 "$build/runtime"
QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR="$build/runtime" timeout 30 "$build/updater-test" \
    "$root/IMSProg_programmer/database/IMSProg.Dat"
