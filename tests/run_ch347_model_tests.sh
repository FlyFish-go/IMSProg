#!/bin/sh
# Hardware-free CH347T/CH347F selection test using fake libusb entry points.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
: "${CC:=cc}"
$CC -std=gnu11 -O1 -g -Wall -Wextra -Werror \
    -I"$root/IMSProg_programmer/windows/vendor" -I"$root/IMSProg_programmer" \
    "$root/tests/ch347_model_test.c" "$root/IMSProg_programmer/ch347.c" -o "$build/test"
"$build/test"
