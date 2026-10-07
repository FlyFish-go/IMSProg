#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
: "${CC:=cc}"
sanitize=
if [ "${SANITIZE:-0}" = 1 ]; then
    sanitize='-fsanitize=address,undefined -fno-omit-frame-pointer'
fi
# The vendored ABI header and fake functions require no USB library or device.
# CFLAGS and sanitizer options intentionally undergo shell word splitting.
# shellcheck disable=SC2086
$CC -std=c11 -O1 -g -Wall -Wextra -Werror $sanitize ${CFLAGS:-} \
    -I"$root/IMSProg_programmer" \
    "$root/IMSProg_programmer/windows/ch341_async.c" \
    "$root/tests/ch341_async_test.c" -o "$build/test"
"$build/test"
