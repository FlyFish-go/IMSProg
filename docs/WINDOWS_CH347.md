# Windows x64 SPI build

SPI support for CH341A/B, CH347T and CH347F. FT232H, I2C and MicroWire are
disabled on Windows. `cmake --install` also copies the signed WCH driver package
from `third_party/wch_driver` to `driver\` (see its README.txt).

## Build

Use CMake, Ninja and an x64 MinGW compiler matching your Qt kit, including
LinguistTools. Put that compiler's `bin` directory on `PATH`. From the repository
root, adjust the example Qt path for your installation:

```powershell
cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/mingw_64
cmake --build build-windows
cmake --install build-windows --prefix dist-windows
C:/Qt/6.8.3/mingw_64/bin/windeployqt.exe --release --no-translations dist-windows/IMSProg.exe
```

Keep `IMSProg.Dat` beside `IMSProg.exe`. Qt deployment does not supply USB backend
DLLs. Build `libusb-1.0.dll` using the source and script in `third_party/libusb/`,
and obtain x64 `libusbK.dll` from its [official release](https://github.com/mcuee/libusbk/releases/tag/V3.1.0.0).
Place both beside the EXE; these userspace libraries do not install a driver.
Preserve the component notices and corresponding source when redistributing
the modified library.

For a Linux cross-build, use x86_64 MinGW POSIX tools and matching Windows/Linux Qt kits:

```sh
cmake -S IMSProg_programmer -B build-windows -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/mingw-w64.cmake" \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/mingw_64 \
  -DQT_HOST_PATH=/path/to/Qt/6.8.3/gcc_64 -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows
```

Qt 5 is selectable with `-DFORCE_QT5=ON`. MSVC has not been verified. The Windows
build excludes the separate database editor and updater.

## Connection and first use

- Select CH341A/B, CH347T or CH347F in the Programmer menu and pick an SPI
  clock in its submenu. The CH341 SPI clock is fixed by the chip. CH347 clocks
  are 60 MHz / 2^n; SPI NOR is capped at 30 MHz and EEPROMs at their defaults.
  Only one device of the selected family may be attached. CH341 SPI IDs are
  `1A86:5512` and `4348:5512`; CH347T mode 1 is `1A86:55DB`; CH347F is
  `1A86:55DE` (SPI/I2C on interface 4).
- Driver: Programmer > Driver... (or the driver item in the status bar) shows
  whether the WCH driver is installed and installs the bundled package with
  `pnputil /add-driver ch341wdm.inf /install` after a UAC prompt. CH347 serial
  (COM) drivers are not changed.
- Backends: libusb (WinUSB/libusbK-bound devices) is tried first, then the WCH
  vendor DLL (CH341DLLA64/CH347DLLA64), loaded from the program folder, its
  `driver\` folder or System32. CH347F is matched by PID `55DE` and interface
  `MI_04` with FuncType 1 or 3; WCH driver 2.6.2025.x reports ChipMode 0 for it.
- Use Programmer > Programmer connection diagnostics if connection fails.
  It inspects USB metadata without sending SPI/flash commands.
- 1.8 V chips such as W25N01GW need 1.8 V power **and signal-level conversion**.
  Start with repeated reads and matching backups. See
  [the NAND guide](ECC_OOB_zh_CN.md) before writing or erasing.

## Validation

The included tests simulate transports and exercise the native Qt interface.
Version 1.9.3 was additionally checked on Windows 11 with a CH341B (WCH
driver, SPI NOR JEDEC read) and a CH347F (WCH driver, SPI initialisation).
