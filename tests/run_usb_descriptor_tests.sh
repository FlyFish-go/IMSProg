#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
: "${CC:=cc}"
$CC -std=gnu11 -O1 -Wall -Wextra -Werror -I"$root/IMSProg_programmer" \
 "$root/tests/ch347_usb_select_test.c" "$root/IMSProg_programmer/windows/ch347_usb_select.c" -o "$build/test"
"$build/test"
