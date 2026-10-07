# Hardware-free Qt UI integration tests

`ui_nand_test.cpp` instantiates the real `MainWindow`, uses the compiled `.ui`, and
calls the production slots. It does not copy or reimplement their control logic.
The executable uses GNU ld `--wrap` fake programmer and NAND entry points, so no
programmer enumeration, device open, SPI read, erase, or programming reaches real
hardware. Timers and modal responses are deterministic, and test configuration
and input files stay in a temporary directory. Dialogs are actual Qt dialogs.

## Run

Build the native Qt5 application from the same source revision first. With the
Qt5Widgets, libusb-1.0 and libftdi1 development pkg-config files available:

```sh
tests/ui_run_tests.sh build-linux
# Optional screenshot of the real Qt UI, marked not connected:
tests/ui_run_tests.sh build-linux /absolute/path/imsprog-nand-ui-linux.png
```

A nonstandard development sysroot also needs its runtime shared-library directory
in `LD_LIBRARY_PATH`. This is a Linux Qt/GNU-linker test, not a Windows executable
test. It uses the existing application's objects, excluding its ordinary `main`.
Rebuild the application after changing production source before rerunning tests.

## Behavioral coverage

- Actual startup with persisted CH347 selection, periodic probe and Detect use
  representable SPI clocks rather than I2C indexes/clock values; JEDEC error stops
- Selector population, hide/show, and cancelled layout changes
- All seven NAND-only inputs/labels hide and disable at startup, NOR, every
  EEPROM protocol, unknown data and invalid selections; they show-but-disable
  while NAND I/O runs and restore appropriately on cleanup
- NAND/NOR/EEPROM round trips preserve dormant layout/ECC choices; hidden
  callbacks cannot resize/recolor non-NAND buffers, and a protocol-data=6 item
  at a different row still activates NAND controls
- NOR-to-W25N01GW autodetect uses fake NOR/NAND JEDEC entry points; a real NOR
  read uses NOR transfers only and exact main-only buffer size despite dormant
  NAND choices; unconfigured NAND geometry remains blocked
- All four CH341/CH347 QAction selections preserve ECC/OOB choices and buffer
  bytes, remain selected after cleanup, and route every Read/Verify/Write device
  argument through the selected programmer index; menus lock during I/O
- Diagnostics action is present, hidden on native builds, locked during operations,
  and its native slot cannot initialize the programmer or perform chip I/O
- Main-only and Main+OOB sizes independent of ECC; all four Read, Verify and
  blank-check combinations, including output payload and per-page stride
- Changing ECC does not alter buffer bytes or open the programmer
- Reading after a short buffer allocates exactly the selected image size
- All supported Write combinations, blocked Main+OOB/ECC-on programming, and
  preflight rejection before a combined erase/write sequence begins
- Invalid geometry (including unsupported non-64-page blocks), short buffers,
  configuration/readback failures that preserve the original buffer, and
  W25N01GW ID/geometry validation; exact W25N01GW requires buffered reads
- W25N01GW OTP mode blocks main-array Read/Write/Erase
- Uncorrectable ECC (-1002) stops Read and Verify without a success report
- Raw bad-block skips preserve physical image offsets; bad-block check and
  programming failures propagate rather than being treated as success; a
  main-only compacted write reports failure when image bytes remain unwritten
- Declined erase confirmation, transport failure and Stop abort combined work;
  closing during I/O requests Stop and keeps the window visible for review
- A new Write stage cannot be inserted during an in-progress batch
- Failed reads mark incomplete buffers, leave unread bytes FF, restore controls,
  and cannot subsequently be programmed
- Real short-file Open retains actual file length rather than an old dump's tail;
  opening a raw image with page size unset does not divide by zero

## Limits

The underlying NAND protocol is faked here; `run_nand_fake_spi_tests.sh` separately
exercises production protocol functions against a simulated SPI transport.
Neither suite proves USB timing, voltage selection, a physical NAND chip's ECC
behavior, or Windows DLL/device behavior. Windows UI restrictions need Windows
runtime QA; the screenshot is explicitly a native Linux Qt preview.

## Windows backend host tests

The independent `tests/run_windows_backend_tests.sh` suite compiles the actual
Windows backend and descriptor selector on a native host with fake Win32, DLL,
and libusb APIs. It covers fallback selection, secure app-local loading,
descriptor-selected interfaces/alternates, explicit CH341/CH347 family selection
with both families attached, family-specific ambiguity, vendor descriptor/config
validation, rounded CH341 replies with buffer canaries, cleanup,
diagnostic enumeration without claims/transfers, and failure without retry or
backend switching. It does not execute actual Windows DLLs or kernel drivers.

```sh
tests/run_windows_backend_tests.sh
ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1 SANITIZE=1 tests/run_windows_backend_tests.sh
```

When running under ptrace, disable LeakSanitizer with
`ASAN_OPTIONS=detect_leaks=0`; address and undefined-behavior checks remain enabled.

## Windows UI source conformance

`python3 tests/ui_windows_conformance.py` separately checks the Windows-only
source branches for supported CH341 selectors, retained I2C/MicroWire and FTDI
restrictions, cleanup, and selected-family diagnostic routing. These checks are
source conformance, not Windows runtime tests. The common backend suite stubs
`ch341_async_exchange` to isolate routing; `run_ch341_async_tests.sh` exercises the
actual asynchronous helper and `run_ch341_spi_tests.sh` exercises actual packet
framing, bit reversal, held CS, bounded chunks and error cleanup.
