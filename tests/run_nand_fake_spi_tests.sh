#!/bin/sh
# Build real production NAND functions with fake transport and fake delay.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT HUP INT TERM
: "${CC:=cc}"
# Unused NOR/EEPROM functions are deliberately discarded, not reimplemented.
# Run with SANITIZE=1 for address/undefined behavior instrumentation.
sanitize=""
if [ "${SANITIZE:-0}" = 1 ]; then sanitize="-fsanitize=address,undefined -fno-omit-frame-pointer"; fi
$CC -std=gnu99 -O1 -g -ffunction-sections -fdata-sections $sanitize \
    -Dusleep=fake_usleep -I"$root/IMSProg_programmer" \
    -c "$root/IMSProg_programmer/spi_nor_flash.c" -o "$build/nand.o"
$CC -std=gnu99 -O1 -g -Wall -Wextra -Werror $sanitize \
    -I"$root/IMSProg_programmer" "$root/tests/nand_fake_spi_test.c" \
    "$build/nand.o" -Wl,--gc-sections -o "$build/nand-test"
"$build/nand-test"
