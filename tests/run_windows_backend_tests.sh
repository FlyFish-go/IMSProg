#!/bin/sh
# Native host test of the production Windows transport using fake Windows/DLL APIs.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
: "${CC:=cc}"
sanitize=""
if [ "${SANITIZE:-0}" = 1 ]; then sanitize="-fsanitize=address,undefined -fno-omit-frame-pointer"; fi
$CC -std=gnu11 -O1 -g -Wall -Wextra -Werror $sanitize -D_WIN32 -D_WIN64 \
    -I"$root/tests/windows_fakes" -I"$root/IMSProg_programmer" \
    "$root/tests/windows_backend_test.c" "$root/IMSProg_programmer/windows/ch347_usb_select.c" \
    -o "$build/windows-backend-test"
"$build/windows-backend-test"
