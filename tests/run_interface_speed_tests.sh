#!/bin/sh
# Hardware-free CH347F auxiliary-dialog speed regression.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
: "${CC:=cc}"
"$CC" -std=gnu11 -O1 -g -Wall -Wextra -Werror \
    -Wno-unused-parameter -Wno-return-type -ffunction-sections \
    -DIMSPROG_WINDOWS_CH347_ONLY \
    -I"$root/IMSProg_programmer/windows/vendor" -I"$root/IMSProg_programmer" \
    "$root/tests/interface_speed_test.c" "$root/IMSProg_programmer/spi_controller.c" \
    -Wl,--gc-sections -o "$build/test"
"$build/test"
