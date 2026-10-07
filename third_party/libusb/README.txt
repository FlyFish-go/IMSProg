Portable Windows USB backend source

This directory contains source and notices, not DLLs or a driver installer.

Files
- libusb-1.0.30-imsprog-corresponding-source.tar.gz: libusb 1.0.30 source,
  the portability change, build configuration and build-imsprog.sh.
- libusb-portable-libusbk.patch: the libusb source changes.
- licenses/: libusb LGPL-2.1 and libusbK BSD-3-Clause notices and authors.

The libusb modification first loads libusbK.dll from the absolute executable
directory with restricted dependency searching, then retains the upstream
System32 fallback. It does not install or replace a kernel driver.

Rebuild on Linux with an x86_64 MinGW-w64 GCC toolchain:

  tar xf libusb-1.0.30-imsprog-corresponding-source.tar.gz
  cd libusb-1.0.30-imsprog
  bash build-imsprog.sh

Output: build/libusb-1.0.dll. CC, WINDRES, STRIP and TC_PREFIX can override the
toolchain commands. See IMSProg-MODIFICATIONS.txt inside the source archive.
Place the rebuilt DLL and an official x64 libusbK.dll beside IMSProg.exe.
These are replaceable userspace libraries, not kernel drivers. Keep an existing
working driver unchanged; actual hardware compatibility has not been validated.

libusb and its modifications are LGPL-2.1-or-later. The archive includes complete
corresponding source and COPYING. libusbK offers a BSD-3-Clause license option;
retain its notices when distributing its library.

Upstream source and releases:
https://github.com/libusb/libusb/tree/87a55632db62c9bdc58cd31d3ccfa673f1bb017f
https://github.com/libusb/libusb/releases/tag/v1.0.30
https://github.com/mcuee/libusbk/tree/bbd3aaa7650fd479ac888f33d76a46fd6aac7652
https://github.com/mcuee/libusbk/releases/tag/V3.1.0.0
