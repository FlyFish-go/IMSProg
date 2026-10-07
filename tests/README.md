# Hardware-free regression tests

Run from the repository root:

```sh
./tests/run_nand_fake_spi_tests.sh
```

Requirements: a C99-capable GCC-compatible C compiler and a linker supporting
`--gc-sections`. The tests compile the actual `IMSProg_programmer/spi_nor_flash.c`
NAND implementation against fake SPI and fake delay functions. Unrelated NOR,
EEPROM and AT45 functions are discarded by the linker. No programmer backend,
USB library, chip operation, Qt installation, or driver is involved.

Optional AddressSanitizer and UndefinedBehaviorSanitizer run:

```sh
SANITIZE=1 ./tests/run_nand_fake_spi_tests.sh
```

In environments that run executables under ptrace, LeakSanitizer is unsupported:
use `ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 ./tests/run_nand_fake_spi_tests.sh`.
This disables leak checking only. The tests do not allocate dynamic buffers.

## Coverage

- All 256 initial B0 register values, ECC enabled/disabled independently of the
  BUF requirement, unchanged-register paths, and preservation of non-target bits.
- Readback mismatches in every bit, including non-target bits.
- Failures at each CS, command and read/write phase for configuration, read,
  program, erase and identification. Negative transport errors survive unchanged;
  positive controller errors map to `NAND_ERROR_TRANSPORT`. CS deassertion is
  attempted after every asserted/failed-to-assert transaction.
- 2048-byte main and 2112-byte main+OOB reads/writes, buffer canaries, page
  addressing, and stopping a block operation on the first failing page.
- Explicit W25N01GW ECC clean/corrected/uncorrectable states, conservative handling
  of unexpected state 11, aggregate corrected status across blocks, and raw reads
  ignoring ECC status while ECC is disabled. Reads never write B0 or enable ECC.
- Busy timeout and read-status failure propagation.
- Program/erase completion, including the last page, P-FAIL/E-FAIL, and protection
  register read-modify-write verification.
- Bad-block marker column addressing and the legacy 0/1/-1 return convention.

## API and device scope

Before each read/verify/program operation, identify the physical device; call
`nand_set_ecc_status_mode()` and then `nand_configure_ecc()`. Selecting a mode or
reading JEDEC ID invalidates cached ECC configuration. Set
`NAND_ECC_STATUS_W25N01GW` only for a positively identified W25N01GW; other chips
use `NAND_ECC_STATUS_NONE`. Read/verify helpers do not implicitly configure ECC.
Reset the aggregate ECC status once per whole operation, not once per block.
The APIs share the existing single-programmer global state and are not thread-safe.

`require_buffer_mode` is only for a chip known to use B0 bit 3 as BUF. It sets that
bit; when false it preserves it. There is no dependence on requested page length.
`nand_configure_ecc()` verifies the complete readback value and returns an error
if any target or preserved bit differs. `readback` receives the actual register
value when available. Callers must stop on a nonzero return.

The NAND block erase path retains the project's existing 64-pages-per-block
geometry. This change does not add new NAND protocols or alternate vendor ECC
status layouts. It does not implement software ECC, bad-block remapping, or
reinterpret filesystem OOB layouts. ECC-enabled writes can regenerate device
ECC bytes; a raw dump round-trip requires the explicitly selected ECC-off mode.

Protocol references:

- [Winbond W25N01GW manufacturer datasheet, Rev F](https://docs.rs-online.com/f041/0900766b81703fd8.pdf):
  sections 7.2.4–7.2.5 (ECC-E and BUF), 7.3.2–7.3.3 (ECC and program/erase
  status), 8.1.1–8.1.3 (EF BA 21 ID), 8.2.14–8.2.15 (page/buffer read).
- [Linux SPI NAND common definitions](https://github.com/torvalds/linux/blob/master/include/linux/mtd/spinand.h):
  common BUSY, ERASE_FAILED and PROG_FAILED status bits.

Passing these tests establishes software behavior under the simulated transport;
it does not establish physical programmer/chip compatibility or electrical safety.

## Windows transport and UI tests

Run these host-compiled tests without a USB device or host libusb installation:

```sh
sh tests/run_ch341_spi_tests.sh
sh tests/run_ch341_async_tests.sh
sh tests/run_ch347_transport_tests.sh
sh tests/run_usb_descriptor_tests.sh
sh tests/run_windows_backend_tests.sh
python3 tests/ui_windows_conformance.py
```

The backend tests cover explicit family selection, mock libusb/WCH fallback,
metadata-only diagnostics, errors and cleanup. CH341 asynchronous APIs are
checked before interface claim or GPIO/SPI setup; missing exports cannot leave a
partially initialized function table. Transfer errors never switch backends or
retry commands. CH341 asynchronous cancellation drains callbacks before buffers
are released; a broken event loop that never completes callbacks can remain
blocked rather than free live buffers.

`SANITIZE=1` enables ASan/UBSan in the NAND, CH341 SPI/async and Windows backend
runners. The CH347 transport test uses the vendored libusb ABI header and fake
functions; it does not link a USB backend.

For the real Qt MainWindow tests, first build the Linux application from the
same source revision with Qt 5, libusb and libftdi development packages:

```sh
cmake -S IMSProg_programmer -B build-linux -DFORCE_QT5=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux
sh tests/ui_run_tests.sh build-linux
```

These UI tests replace all hardware calls. The Python UI check only inspects
source and is not a Windows GUI execution test.
