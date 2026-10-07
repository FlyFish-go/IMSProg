# Windows x64 SPI build

Experimental SPI support for CH341A/B and CH347T. FT232H, I2C and MicroWire remain
disabled on Windows. This repository provides source and hardware-free tests;
it does not include an EXE, Qt runtime, WCH DLL or driver.

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

- Select CH341A/B or CH347T explicitly in the Programmer menu. Only one device
  of the selected family may be attached. CH341 SPI IDs are `1A86:5512` and
  `4348:5512`; CH347T mode 1 is `1A86:55DB`. USB IDs do not distinguish A/B silicon.
- The backend first tries libusb with the existing driver. It does not install,
  replace, detach or reset drivers. Do not change a working driver just to try
  this build, and do not copy DLLs into System32.
- Use Programmer > Programmer connection diagnostics if connection fails.
  This action inspects USB metadata without sending SPI/flash commands.
  Ordinary startup polling can initialize GPIO/CS, so first check the programmer
  with no flash chip attached.
- W25N01GW requires 1.8 V power **and signal-level conversion**. Check wiring and
  board power, then start with repeated reads and matching backups. See
  [the NAND guide](ECC_OOB_zh_CN.md) before considering writes or erases.

## Optional WCH backend and limits

The source retains a family-specific WCH DLL fallback after successful device
enumeration and before SPI I/O. Device counts only cover devices exposed by
libusb and the selected vendor API; a successful enumeration can omit devices
whose driver initialization failed, so it is not a complete Windows PnP
inventory. Keep only one programmer of the selected family connected. WCH binaries are not supplied. Their official
download is [CH341PAR](https://www.wch-ic.com/downloads/CH341PAR_ZIP.html);
[WCH's terms](https://www.wch-ic.com/statement.html) do not establish a clear
unrestricted redistribution grant. Separate downloading or runtime loading does
not itself resolve compatibility with the application's GPL obligations.

The included tests simulate transports and exercise the native Qt interface.
They do not establish Windows startup, real driver compatibility, electrical
safety, chip correctness or throughput. No physical hardware validation is claimed.
