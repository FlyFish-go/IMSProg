#!/bin/sh
# Compile and run real Qt MainWindow integration tests using already-built app objects.
# Usage: tests/ui_run_tests.sh [CMake native build directory]
# Qt5Widgets, libusb-1.0 and libftdi1 development pkg-config files are required.
# The normal application target must first be built from the same source revision.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
appbuild=${1:-"$root/build-linux"}
screenshot=${2:-}
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
: "${CXX:=c++}"
objects=$(find "$appbuild" -path '*/CMakeFiles/IMSProg.dir/*' -name '*.o' ! -name 'main.cpp.o')
[ -n "$objects" ] || { echo "Build the native IMSProg application first." >&2; exit 1; }
wraps=""
for symbol in ProgDeviceInit ProgDeviceClose getDeviceDescriptor snor_read_devid snor_read_param nand_read_devid nand_configure_ecc \
    nand_set_ecc_status_mode nand_reset_ecc_read_status nand_get_ecc_read_status nand_unprotect \
    nand_checkBadBlock nand_block_read nand_page_read nand_block_write nand_block_erase \
    nand_ECCEnable nand_ECCDisable; do
    wraps="$wraps -Wl,--wrap=$symbol"
done
# Deliberate word splitting for pkg-config flags and the CMake object list.
$CXX -std=c++17 -fPIC -O0 -g $(pkg-config --cflags Qt5Widgets libusb-1.0 libftdi1) \
    -I"$root/IMSProg_programmer" -I"$root/IMSProg_programmer/qhexedit2/src" \
    "$root/tests/ui_nand_test.cpp" $objects $wraps \
    $(pkg-config --libs Qt5Widgets libusb-1.0 libftdi1) -o "$build/ui-nand-test"
mkdir -p "$build/config" "$build/data" "$build/runtime" "$build/home" "$build/cache"
chmod 700 "$build/runtime"
QT_QPA_PLATFORM=offscreen HOME="$build/home" XDG_CACHE_HOME="$build/cache" XDG_CONFIG_HOME="$build/config" XDG_DATA_HOME="$build/data" \
    XDG_RUNTIME_DIR="$build/runtime" timeout 120 "$build/ui-nand-test" \
    "$root/IMSProg_programmer/database/IMSProg.Dat" ${screenshot:+"$screenshot"}
