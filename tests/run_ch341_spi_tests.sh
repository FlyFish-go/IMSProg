#!/bin/sh
# Exercise production Windows CH341 framing using a fake transport, without USB.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
: "${CC:=cc}"
sanitize=""
if [ "${SANITIZE:-0}" = 1 ]; then sanitize="-fsanitize=address,undefined -fno-omit-frame-pointer"; fi
$CC -std=gnu11 -O1 -g -Wall -Wextra -Werror -Wvla $sanitize -D_WIN32 -D_WIN64 \
    -I"$root/tests/windows_fakes" -I"$root/IMSProg_programmer" \
    "$root/tests/ch341_spi_test.c" "$root/IMSProg_programmer/windows/ch341_spi_win32.c" \
    -o "$build/ch341-spi-test"
"$build/ch341-spi-test"
