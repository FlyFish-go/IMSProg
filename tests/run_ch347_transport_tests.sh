#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
: "${CC:=cc}"
$CC -std=gnu11 -O1 -g -ffunction-sections -fdata-sections \
    -I"$root/IMSProg_programmer/windows/vendor" -I"$root/IMSProg_programmer" \
    -c "$root/IMSProg_programmer/ch347.c" -o "$build/ch347.o"
$CC -std=gnu11 -O1 -g -Wall -Wextra -Werror \
    -I"$root/IMSProg_programmer/windows/vendor" -I"$root/IMSProg_programmer" \
    "$root/tests/ch347_transport_test.c" "$build/ch347.o" \
    -Wl,--gc-sections -o "$build/test"
"$build/test"
