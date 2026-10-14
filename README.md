**简体中文** | [English](README_EN.md)

# IMSProg Windows 增强版

<img align="right" src="img/mini_logo_IMSProg.svg">

本分支基于 [bigbigmdm/IMSProg](https://github.com/bigbigmdm/IMSProg)，用于 Windows 功能增强与自用源码备份。下文保留上游的使用说明、Linux/macOS 构建与安装资料、芯片列表、署名和许可证信息。其他平台的说明来自上游，并不代表本分支已完成这些平台的验证。

模拟测试和构建检查不能证明编程器或存储芯片实机通过；硬件验证仍待完成。

- [Windows 增强功能](#windows-增强功能)
- [项目简介](#项目简介)
- [编译项目](#编译项目)
- [系统与软件要求](#系统与软件要求)
- [使用方法](#使用方法)
- [芯片数据库编辑器](#芯片数据库编辑器)
- [芯片数据库更新器](#芯片数据库更新器)
- [支持的芯片列表](#支持的芯片列表)
- [软件包](#软件包)
- [更新记录](ChangeLog)
- [项目结构](#项目结构)
- [芯片数据库格式](#芯片数据库格式)
- [许可证](#许可证)
- [翻译与贡献者](#翻译与贡献者)

## Windows 增强功能

Windows x64 版本支持 CH341A/B、CH347T 和 CH347F 的 SPI 操作，保留 libusb／libusbK 路径及可选的 WCH 官方后端源码。Windows 下暂不支持 FT232H、I2C 和 MicroWire，独立数据库编辑器与更新器也不参与 Windows 构建。只连接所选类型的一台编程器；设备唯一性检查仅覆盖后端能够枚举到的设备。

SPI NAND 的转储布局和片内 ECC 独立选择，相关控件只在 SPI NAND 模式显示：

| 布局 | 片内 ECC | Read／Verify | Write |
| :--- | :--- | :--- | :--- |
| 主数据 | 开 | 支持，读取经芯片纠错的主数据 | 支持 |
| 主数据 | 关 | 支持，读取原始主数据 | 支持 |
| 主数据 + OOB | 开 | 支持，主数据已纠错，不能称为原始转储 | 不支持此格式组合 |
| 主数据 + OOB | 关 | 支持，按每页主数据后跟 OOB 保存 | 支持原始镜像写回 |

`Read`、`Verify` 和 `Auto` 按所选 ECC 配置操作，并回读 B0 寄存器确认。校验不会强制打开 ECC，避免原始数据在 ECC 关闭时写入、却在 ECC 打开时校验而误报。写入和校验要求缓冲区长度与所选布局完全匹配，未完成的读取不能直接用于写入。

W25N01GW 识别为 `EF BA 21`，使用每页 2048 字节主数据与 64 字节 OOB：主数据镜像为 128 MiB，含 OOB 的镜像为 132 MiB。芯片需要 **1.8 V 供电与信号电平转换**。本分支没有新增软件 BCH/QPIC ECC、OOB 重排或镜像修复能力。操作步骤与格式要求见 [ECC/OOB 中文说明](docs/ECC_OOB_zh_CN.md)。

### Windows 构建

使用与 Qt 匹配的 x64 MinGW、CMake、Ninja 和 Qt LinguistTools。以 MSYS2 UCRT64 为例，将工具链 `bin` 加入本次终端的 `PATH`，在仓库根目录运行：

```powershell
cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64
cmake --build build-windows --parallel
cmake --install build-windows --prefix dist-windows
```

使用其他 Qt 套件时，替换 `CMAKE_PREFIX_PATH`。程序与 `IMSProg.Dat` 放在同一目录。Qt 部署、USB 后端和连接诊断的详细说明见 [Windows 构建与连接指南](docs/WINDOWS_CH347.md)。仓库仅发布源码、构建资料、必要的依赖对应源码及许可证，没有附带 EXE、WCH DLL 或驱动；WCH 二进制的公共再分发许可与 GPL 兼容性仍未核实。

### 测试与验证范围

本机已使用 MinGW GCC 16.2.0 和 Qt 6.11.2 完成 Windows x64 Release 构建，CH341 异步 560 案例、USB 描述符选择测试及 19 项 Windows UI 源码检查通过。完整硬件无关回归的运行方式见 [tests/README.md](tests/README.md)，Qt 集成覆盖见 [tests/ui_README.md](tests/ui_README.md)。依赖 Unix 头文件与 ELF 链接行为的测试需在 GNU/Linux 运行，不能将 MinGW 无法运行的测试记为通过。

[本分支 CI](https://github.com/FlyFish-go/IMSProg/actions/workflows/windows-regression.yml) 执行 Windows 构建、Linux 协议与后端回归、ASan/UBSan 和原生 Qt 界面测试。实际状态以对应提交的 Actions 结果为准。模拟测试、源码检查和成功构建均不代表真实 Windows 驱动、USB 时序、芯片 ECC 行为或电气连接已通过；目前没有完成实机验证。

## 项目简介

**IMSProg** 的名称来自 **I**2C、**M**icroWire、**S**PI 和 **Prog**rammer。它通过 `CH341A`、`CH347T` 和 `FT232H v1.2` 编程器读取、写入 EEPROM/Flash 芯片。

| CH341A/B v1.2 | CH341A v1.7 | CH347T v1.0 | CH347T v1.1 | FT232H v1.2 |
| :---: | :---: | :---: | :---: | :---: |
| ![CH341A 黑色](img/ch341_black150.png) ![CH341A 绿色](img/ch341_green150.png) | ![CH341A v1.7](img/ch341v1_7.png) | ![CH347T v1.0](img/ch347_150.png) | ![CH347T v1.1](img/ch347_v1_1_150.png) | ![FT232H v1.2](img/ft232h_150.png) |

IMSProg 使用了 [QHexEdit2](https://github.com/Simsys/qhexedit2) 十六进制编辑器和 [SNANDer](https://github.com/McMCCRU/SNANDer) 编程器项目的成果。芯片数据库格式参考 EZP2019、EZP2020、EZP2023、Minpro I 和 XP866+，但格式并不相同，不能直接互换。

项目包含三个工具：

1. `IMSProg`：芯片编程器，主程序。
2. `IMSProg_editor`：芯片数据库编辑器。
3. `IMSProg_database_update`：从外部服务器更新芯片数据库。

![CH341A EEPROM 编程器](img/IMSProg_1.png)

![CH341A EEPROM 编程器](img/IMSProg.png)

## 编译项目

下面是上游 Linux/macOS 构建入口；Windows 增强版的构建资料随对应源码提供。

```sh
git clone https://github.com/bigbigmdm/IMSProg.git && cd IMSProg
sudo ./build_all.sh
```

## 系统与软件要求

### Linux

构建需要：

- g++ 或 clang
- CMake
- libusb 1.0
- libftdi
- Qt5 或 Qt6
- Qt5LinguistTools 或 Qt6LinguistTools
- pkgconf 或 pkg-config
- udev

Debian 及其衍生发行版，使用 Qt5：

```sh
sudo apt-get install cmake g++ libusb-1.0-0-dev qtbase5-dev qttools5-dev pkgconf libftdi1-dev
```

使用 Qt6：

```sh
sudo apt-get install cmake g++ libusb-1.0-0-dev pkgconf qt6-base-dev qt6-tools-dev libftdi1-dev linguist-qt6 qt6-l10n-tools qt6-base-dev-tools qt6-tools-dev-tools
```

Debian 13 及以上、Ubuntu 24.04 及以上还需要：

```sh
sudo apt-get install systemd-dev
```

更早的版本需要：

```sh
sudo apt-get install udev
```

上游不支持 Debian 11 之前或 Ubuntu 20.04 之前的系统，在这些版本上可能无法正常运行。

Fedora 及其衍生发行版，使用 Qt5：

```sh
sudo dnf install cmake libusb libusb1 libusb1-devel qt5-qtbase-devel
sudo dnf install pkgconf-pkg-config qt5-linguist libftdi-devel
```

使用 Qt6：

```sh
sudo dnf install cmake gcc-c++ libusb1-devel pkgconf-pkg-config qt6-qtbase-devel
sudo dnf install qt6-qttools-devel qt6-linguist libftdi-devel
```

openSUSE 及其衍生发行版，使用 Qt5：

```sh
sudo zypper install cmake gcc-c++ libqt5-qtbase-devel
sudo zypper install libqt5-linguist-devel libusb-1_0-devel
sudo zypper install libftdi1-devel
```

使用 Qt6：

```sh
sudo zypper install cmake gcc-c++ libqt6-qtbase-devel
sudo zypper install libqt6-linguist-devel libusb-1_0-devel
sudo zypper install libftdi1-devel
```

### macOS

使用 Homebrew 时，通常会安装 Xcode Command Line Tools；否则可在终端手动安装：

```sh
xcode-select --install
```

通过 Homebrew 安装依赖：

```sh
brew install qt@6 libusb cmake pkgconf libftdi
```

不使用 [build_all.sh](build_all.sh) 时，需确保 `C_INCLUDE_PATH` 包含 `libusb` 的头文件目录：

```sh
export C_INCLUDE_PATH=/usr/local/opt/libusb/include
```

## 使用方法

macOS 上的快捷键使用 Command（⌘）代替 Ctrl。下述菜单名称保留英文，便于与界面对应。

### 启动

可在终端运行：

```sh
IMSProg
```

### 连接芯片与编程器

- 将芯片插入 CH341A 编程器的相应插座。`24xxx` 和 `25xxx` 根据 PCB 标记直接插入；`93xxx` 先插入 `93xxx` 转接板，再将转接板插入标记为 `25xxx` 的插座。
- DataFlash `AT45DBxxx` 先安装到 `SOP-8 DIP-8` 转接板，再插入 `45xxx` 转接板，最后插入编程器的 `25xxx` 插座。
- SPI NAND Flash 使用 `WSON-8 DIP-8` 转接板，并插入 `25xxx` 插座。

> [!NOTE]
> 上游当前版本的 CH347 编程器不支持 MicroWire（93Cxx）协议。CH347T v1.1 的速度低于 CH347T v1.0。

### CH347F 支持

在“编程器”菜单中选择 **CH347F**。CH347F 的 USB ID 为 `1A86:55DE`，SPI/I2C 位于 USB 接口 4（CH347T 为 `1A86:55DB`、接口 2），两者使用相同的 SPI/I2C 协议和批量端点 `0x06/0x86`。

- 选择 CH347F 时只打开 `55DE` 设备，选择 CH347T 时只打开 `55DB` 设备，两者不会互相回退。
- 默认 SPI 速度：SPI NOR 30 MHz，SPI NAND 30 MHz，25xx/95xx/45xx 5 MHz（可通过速度系数调整）。
- Linux 需要更新 udev 规则（`other/71-CH341.rules` 已加入 `55de`）。Windows 下 libusb 后端按端点自动选择接口；WCH 官方 DLL 后端要求 `ChipMode=4`（CH347F）。
- 与 CH347T 一样，CH347F 暂不支持 MicroWire。

![93xxx 转接板](img/93xxx_adapter.png)

供电为 1.8 V 的芯片必须使用 `1.8 volt adapter`，再接入相应插座。

![1.8 V 转接板](img/1_8_adapter.png)

将 CH341A 接入计算机 USB 接口并启动 `IMSProg`。未进行读取、校验或编程等操作时，可以插拔编程器。转接板的更多说明见[上游转接板项目](https://github.com/bigbigmdm/Tools_for_CH341A_programmer?tab=readme-ov-file#Homemade-Chip-adapters)。

### 选择芯片类型

在 `Type` 菜单中选择：

- `SPI_FLASH`：`25xxx` 系列 SPI NOR Flash。
- `SPI_NAND`：SPI NAND Flash。
- `24_EEPROM`：`24xxx` 系列 I2C EEPROM。
- `93_EEPROM`：`93xxx` 系列 MicroWire EEPROM。
- `45_EEPROM`：`AT45DBxxx` 系列 DataFlash。

`SPI_FLASH`、`SPI_NAND` 和 `45_EEPROM` 支持 `Detect` 按钮。点击 `Detect`、![检测](img/test64.png) 或按 `<Ctrl+D>`，会读取 JEDEC 信息并从数据库加载芯片参数。容量、页大小、块大小、VCC 和地址长度可以手动调整。手动选择 `Manufacture` 与 `Name` 也会自动加载其他参数。

`24xxx`、`93xxx`、`95xxx` 和非 NOR Flash 的 `25xxx` SPI EEPROM 需要手动选择厂商与型号。

### 文件操作

- ![保存](img/save64.png) 或 `<Ctrl+S>`：将缓冲区保存到文件。
- ![打开](img/open64.png) 或 `<Ctrl+O>`：将文件加载到缓冲区。
- ![保存片段](img/saveBlock64.png) 或 `<Ctrl+Shift-S>`：按输入的起始地址和长度保存缓冲区片段，可用于拆分二进制镜像。
- ![加载片段](img/loadBlock64.png)：将小文件加载到缓冲区指定地址，替换该区域的数据。上游说明将此操作的快捷键也记作 `<Ctrl+Shift-S>`；以界面实际显示为准。
- ![数据库编辑器](img/chipEdit64.png)：打开芯片数据库编辑器（快捷键见下表）。
- `File / Import from Intel HEX`：将 Intel HEX 文件加载到缓冲区。
- `File / Export to Intel HEX`：将缓冲区导出为 Intel HEX 文件。

### 编程器基本操作

- `Read`、![读取](img/read64.png) 或 `<Ctrl+R>`：将芯片数据读入缓冲区。
- ![写入](img/write64.png) 或 `<Ctrl+W>`：将缓冲区数据写入芯片。
- ![擦除](img/erase64.png) 或 `<Ctrl+E>`：擦除芯片。
- `Main Menu -> Chip -> Check erase` 或 `<Ctrl+J>`：检查擦除结果。
- ![校验](img/verify64.png) 或 `<Ctrl+T>`：比较芯片数据和缓冲区数据。
- `Auto`：依次执行擦除、擦除检查、写入和校验，可取消勾选不需要的步骤。
- ![停止](img/stop64.png) 或 `<Ctrl+I>`：中断读取、写入、擦除或校验。

进度条显示读取、擦除和校验等操作的进度。`i` 按钮显示芯片与 CH341A 的连接示意图。

`Main menu -> Chip -> Chip info` 或 `<Ctrl+P>` 显示 SPI NOR Flash 的 SFDP 与状态寄存器。

![SFDP](img/sfdp_unblock_en.png)

如果 SPI NOR Flash 能检测但无法读取，提示 `Error reading sector 0`，上游建议在 `Chip info` 中将 `BP4` 和 `BP3` 设为 0，点击 `Write registers` 后重试读取。

![SFDP](img/sfdp_unblock_en2.png)

如果每次读取校验和不同，且芯片有 `Status Register 2`，上游建议将 `DRV0` 和 `DRV1` 设为 0 并写入寄存器，以提高 `DO` 引脚输出驱动能力，然后重新读取。具体位定义以芯片数据手册为准。

只有一个状态寄存器的 `25xxx`、`95xxx` 和 `45xxx`，其信息窗口如下：

![单状态寄存器](img/sr0_en.png)

SPI NAND Flash 的信息窗口如下：

![NAND 状态寄存器](img/sr_nand_en.png)

`Main menu -> Chip -> Security Registers` 或 `<Ctrl+U>` 用于 SPI NOR Flash 和 SPI NAND Flash 的安全寄存器，支持读取、写入、擦除、保存和加载。

![安全寄存器](img/Security_registers_form.png)

窗口底部的 ![读取](img/read64.png)、![擦除](img/erase64.png)、![写入](img/write64.png)、![打开](img/open64.png)、![保存](img/save64.png) 与主窗口对应按钮的功能相同。`Security register number:` 用于选择当前芯片的有效寄存器。

安全寄存器可能被永久写保护。可通过 `Main Menu -> Chip -> Chip Information` 或 `<Ctrl+P>` 查看上游说明中的 `Status register 1` 的 `LB3`、`LB2`、`LB1`；详细位定义及锁定行为应查阅芯片数据手册。

NAND Flash 可以通过 `Main Menu -> Chip -> Bad block management` 或 `<Ctrl+B>` 扫描坏块：

![坏块扫描](img/bbm_chip_scan_en.png)

也可以在该菜单调整擦除和写入参数：

![坏块设置](img/bbm_settings_en.png)

> [!NOTE]
> 上游版本对 NAND Flash 执行 `Verify` 和 `Check erase` 时不会跳过坏块。如果芯片有坏块，上游建议不要使用这两项功能。

### 缓冲区与十六进制编辑器

右侧十六进制编辑器用于显示与修改缓冲区数据，提供 ![撤销](img/undo64.png)、![重做](img/redo64.png) 和 ![搜索替换](img/hexsearch64.png)。`<Ctrl+A>` 计算 CRC24，`<Ctrl+G>` 跳转地址，`<Ctrl+L>` 填充测试数据，`<Ctrl+K>` 填充用户指定值，`<Ctrl+M>` 比较最近两次读取或打开文件的结果。

![比较数据](img/compare_files_en.png)

### 选择编程器

`Main menu -> Programmer` 用于切换编程器类型。

### 快捷键

| 快捷键 | 功能 |
| :--- | :--- |
| Ctrl+A | 计算校验和 |
| Ctrl+B | NAND 坏块管理 |
| Ctrl+C | 复制选中字节（十六进制编辑器） |
| Ctrl+D | 检测芯片 |
| Ctrl+E | 擦除芯片 |
| Ctrl+F | 查找／替换（十六进制编辑器） |
| Ctrl+G | 跳转地址（十六进制编辑器） |
| Ctrl+H | 关于 IMSProg |
| Ctrl+I | 中断芯片操作 |
| Ctrl+J | 检查擦除 |
| Ctrl+K | 填充用户指定值（十六进制编辑器） |
| Ctrl+L | 填充测试数据（十六进制编辑器） |
| Ctrl+M | 比较文件（十六进制编辑器） |
| Ctrl+N | 编辑芯片数据库 |
| Ctrl+O | 打开文件 |
| Ctrl+P | 芯片信息（SFDP 与状态寄存器） |
| Ctrl+Q | 退出 |
| Ctrl+R | 读取芯片 |
| Ctrl+S | 保存文件 |
| Ctrl+T | 校验芯片 |
| Ctrl+U | 安全寄存器 |
| Ctrl+V | 粘贴字节（十六进制编辑器） |
| Ctrl+W | 写入芯片 |
| Ctrl+X | 退出（上游表格记录；以界面为准） |
| Ctrl+Y | 重做（十六进制编辑器） |
| Ctrl+Z | 撤销（十六进制编辑器） |

## 芯片数据库编辑器

![数据库编辑器](img/chip_editor_eng.png)

`IMSProg_editor` 是独立程序，可以从系统的 `Development` 菜单启动，也可以点击 IMSProg 中的 ![编辑](img/chipEdit64.png)。加载 `.Dat` 数据库后，编辑器的操作才会启用。

`File` 菜单：

- `Open`、`<Ctrl+O>` 或 ![打开](img/open.png)：加载数据库。
- `Save`、`<Ctrl+S>` 或 ![保存](img/save.png)：保存芯片参数表。
- `Exit`、`<Ctrl+X>` 或 ![退出](img/exit.png)：退出编辑器。
- `Export to CSV format` 或 ![导出](img/tocsv.png)：导出 CSV，可用 LibreOffice Calc、OpenOffice Calc、Google Sheets 等处理。上游将快捷键记录为 `<Ctrl+X>`；以界面为准。
- `Import from CSV file`、`<Ctrl+Shift+X>` 或 ![导入](img/import.png)：导入 CSV，并将其中的数据追加到当前表格。

`Edit` 菜单：

- `Delete Rows`、`Del` 或 ![删除](img/del.png)：删除一行或多行。
- `Add Row`、`Ins` 或 ![添加](img/plus.png)：在选中行下方插入新行，复制选中行的数据。
- `Move Up`、`<Ctrl+Up>` 或 ![上移](img/undo.png)：将选中行上移。
- `Move Down`、`<Ctrl+Down>` 或 ![下移](img/redo.png)：将选中行下移。
- `Import selected rows to CSV format` 或 ![导出所选行](img/import.png)：将选中行保存为 CSV（名称沿用上游）。

上游项目页面提供最新芯片数据库的 `.Dat` 与 `.csv` 格式。

## 芯片数据库更新器

运行 `IMSProg_database_update` 后，下载的数据库复制到 `~/.local/share/imsprog`，并显示更新前后的芯片数量。该目录是上游 Linux 的用户数据路径。

## 支持的芯片列表

### I2C EEPROM
24C01, 24C02, 24C04, 24C08, 24C16, 24C32, 24C64, 24C128, 24C256, 24C512,
24LC515*, 24C1024, 24LC1025*, 24M02

> [!NOTE]
> *24LC515 和 24LC1025 的第 3 脚需与地断开，再通过 4.7 kΩ 电阻连接到 VCC。*

### I2C FeRAM
- RAMXEED

MB85RC04, MB85RC16, MBRC64, MB85RC128, MB85RC256, MB85RC512, MB85RC1MT

- RAMTRON

FM24C04A, FM24C16A, FM24C32A, FM24C64A, FM24C128, FM24C256, FM24C512, FM24CL04A,
FM24CL16A, FM24CL32A, FM24CL64A


### MicroWire EEPROM（仅 CH341A 编程器）
93C46, 93C56, 93C66, 93C76, 93C86

### 串行 SPI EEPROM
- ST

M95010, M95020, M95040, M95080, M95160, M95320, M95640, M95128, M95256, M95512,
M95M01, M95M02

- ATMEL

AT25010, AT25020, AT25040, AT25080, AT25160, AT25320, AT25640, AT25128, AT25256,
AT25F512, AT25F1024, AT25F2048, AT25F4096

- MICROCHIP

25AA010, 25AA020, 25AA040, 25AA080, 25AA160, 25AA320, 25AA640, 25AA128, 25AA256,
25AAF512, 25AAF1024, 25LC010, 25LC020, 25LC040, 25LC080, 25LC160, 25LC320,
25LC640, 25LC128, 25LC256, 25LC512, 25LC1024

- ONSEMI

CAT25C01, CAT25C02, CAT25C04, CAT25C08, CAT25C16, CAT25C32, CAT25C64, CAT25C128,
CAT25C256, CAT25C512

### 串行 SPI FeRAM
- RAMXEED

MB85RS16N, MB85RD16LX, MB85RS64VY, MB85RS64V, MB85RS128TY, MB85RS128B,
MB85RS256TY, MB85RS256LYA, MB85RS256B, MB85RS512TY, MB85RS512LY, MS85RS1MLY,
MB85RS1MT, MB85RS2MTY, MB85RS2MLY, MB85RS4MTY, MB85RS4MLY

### DataFlash 45xx
- ATMEL

AT45DB011, AT45DB021, AT45DB041, AT45DB081, AT45DB161, AT45DB321

- ADESTO

AT45DB011, AT45DB021, AT45DB041, AT45DB081, AT45DB161, AT45DB321, AT45DB641

### SPI NOR Flash
- SPANSION

FL016AIF, S25FL016P, S25FL032P, FL064AIF, S25FL064P, S25FL256S, S25FL128P,
S25FL129P, S25FL204K, S25FL208K, S25FL116K, S25FL132K, S25FL164K

- XTX

XT25F02E, XT25F04D, XT25F08B, XT25F16B, XT25F32F, XT25F64F, XT25F128F,
XT25F256B, XT25W02E, XT25W04D, XT25Q08D(1.8V), XT25Q16D(1.8V), XT25Q64D(1.8V),
XT25F128D(1.8V)

- ACE

ACE25AC512G, ACE25AC100G, ACE25AC200G, ACE25AC400G

- EON

EN25B10T, EN25B20T, EN25B40T, EN25B80T, EN25B16T, EN25B32T, EN25B64T, EN25P64,
EN25Q40A, EN25Q80B, EN25Q16, EN25Q32C, EN25Q64, EN25Q128, EN25F10A, EN25F20A,
EN25F40, EN25F80, EN25F16, EN25F32, EN25S10A(1.8V), EN25S20A(1.8V),
EN25S40A(1.8V), EN25S80B(1.8V), EN25S16B(1.8V), EN25S64A(1.8V), EN25QE16A,
EN25QE32A, EN25E10A, EN25E40A, EN25SE16A(1.8V), EN25SE32A(1.8V), EN25T80,
EN25T16, EN25QA32B, EN25QA64A, EN25QA128A, EN25QW16A, EN25QW32A, EN25QH16,
EN25QH32B, EN25QH64A, EN25QH128A, EN25Q256, EN25QX64A, EN25QX128A, EN25QX256A,
EN25QY256A, EN25SX64A(1.8V), EN25SX128A(1.8V)

- ATMEL

AT25LS321(1.8V), AT25LS128A(1.8V), AT25LS641(1.8V), AT26DF161, AT25DF321,
AT25DF641

- MICRON

M25P10, M25P20, M25P40, M25P80, M25P016, M25P32, M25P64, M25P128, N25Q032A,
N25Q064A, MT25QL64AB, N25Q128A, MT25QL128AB, N25Q256A, MT25QL256AB, MT25QL512AB,
N25Q032A(1.8V), N25Q064A(1.8V), MT25QU64AB(1.8V), N25Q128A(1.8V),
MT25QU128AB(1.8V), MT25QU256AB(1.8V), MT25QU512AB(1.8V)

- XMC

XM25QH10B, XM25QH20B, XM25QH40B, XM25QH80B, XM25QH16C, XM25QH32B, XM25QH64C,
XM25QH128C, XM25QH256C, XM25QH512C, XM25LU64C(1.8V), XM25LU128C(1.8V),
XM25QU256C(1.8V), XM25QU512C(1.8V), XM25QW16C, XM25QW32C, XM25QW64C, XM25QW128C,
XM25QW256C, XM25QW512C, XM25QU41B(1.8V), XM25QU80B(1.8V), XM25QU16C(1.8V),
XM25LU32C(1.8V), XM25QH32A, XM25QH64A, XM25QH128A

- AMIC

AT25L05P, A25L10PU, A25L20PU, A25L40PU, A25L80PU, A25L16PU, A25L10PT, A25L20PT,
A25L40PT, A25L80PT, A25L16PT, A25L010, A25L020, A25L040, A25L040, A25L080,
A25L016, A25L032, A25LQ080, A25LQ16, A25LQ32, A25LQ64

- EXCELSEMI

ES25P10, ES25P20, ES25P40, ES25P80, ES25P16, ES25P32, ES25M40A, ES25M80A,
ES25M16A

- DOUQI

DQ25Q64AS

- Zbit

ZB25LD10A(1.8V), ZB25LD20A(1.8V), ZB25LD40B(1.8V), ZB25LD80(1.8V), ZB25D10A,
ZB25D20A, ZB25D40B, ZB25D80B, ZB25VQ16, ZB25VQ32, ZB25VQ64, ZB25VQ128,
ZB25LQ16(1.8V), ZB25LQ32(1.8V), ZB25LQ64(1.8V), ZB25LQ128(1.8V), ZB25VQ20A,
ZB25VQ40A, ZB25VQ80A, ZB25VQ16A, ZD25D20, ZD25D40, ZD25D80

- ONSemi

LE25U20AMB, LE25U40CMC

- Boya

BY25Q05AW, BY25Q10AW, BY25Q20BL(1.8V), BY25Q40BL, BY25Q80AW(1.8V),
BY25Q16BL(1.8V), BY25D05AS, BY25D10AS, BY25D20AS, BY25D40AS, BY25Q80BS,
BY25Q16BS, BY25Q32BS, BY25Q64AS, BY25Q128AS, BY25Q256ES, BY25Q10AL(1.8V),
BY25Q20AL(1.8V), BY25Q40AL(1.8V), BY25Q32AL(1.8V), BY25Q64AL(1.8V),
BY25Q128EL(1.8V)

- PFLASH

Pm25LQ512B, Pm25LQ010B, Pm25LD020, Pm25LQ020B, PM25LQ016, PM25LQ032, PM25LQ064,
PM25LQ128, Pm25LV010, Pm25LV020, Pm25LQ040B, Pm25LV040

- Puya

P25Q06H, P25Q40H, PY25Q128HA, PY25Q256HB, PY25Q512HB, P25Q11H, P25Q21H, P25Q10H,
P25Q20H, P25Q80H, P25Q16H, P25Q32H, P25Q64H, P25Q128H

- ESMT

F25L004A, F25L008A, F25L016, F25L032, F25L064, F25L16QA, F25L32QA, F25L64QA

- ISSI

IS25WQ020(1.8V), IS25WQ040(1.8V), IS25WQ080(1.8V), IS25LQ010, IS25LQ020,
IS25LP040E, IS25LP080D, IS25LP016D, IS25LP032D, IS25LP064D, IS25LP128F,
IS25LP256D, IS25LP512D, IS25WP512E(1.8V), IS25WP010E(1.8V), IS25WP020E(1.8V),
IS25WP040D(1.8V), IS25WP080D(1.8V), IS25WP016D(1.8V), IS25WP032D(1.8V),
IS25WP064D(1.8V), IS25WP128F(1.8V), IS25WP256D(1.8V), IS25WP512D(1.8V)

- Fudan

FM25W02, FM25W04, FM25W16, FM25W32, FM25W64, FM25W128, FM25F005, FM25F01B,
FM25F02A, FM25F04A, FM25Q04, FM25Q08, FM25Q16, FS25Q32, FS25Q64, FS25Q128

- Zetta

ZD25Q64B, ZD25LQ128(1.8V), ZD25LQ64(1.8V), ZD25WD20B, ZD25WD40B, ZD25Q80C,
ZD25Q16B, ZD25Q32C

- PCT

PCT25VF016B, PCT25VF032B, PCT25VF064C, PCT25VF020B, PCT25VF040B, PCT25VF080B,
PCT26VF016, PCT26VF032, PCT25VF010A

- MXIC

MX25L512E, MX25L1006E, MX25L2005, MX25L4005A, MX25L8005M, MX25L1605D,
MX25L3205D, MX25L6405D, MX25L12805D, MX25L25635E, MX25L51245G, MX25L5121E,
MX25L1021E, MX25L1633E, MX25L1635E, MX25U5121E(1.8V), MX25U1001E(1.8V),
MX25U2035F(1.8V), MX25U4035F(1.8V), MX25U80356(1.8V), MX25U1632F(1.8V),
MX25U3232F(1.8V), MX25U6432F(1.8V), MX25U12832F(1.8V), MX25U25643G(1.8V),
MX25U51245G(1.8V), MX25L1655D, MX25L6455E, MX25L12855E, MX25L25655E, MX25R1035F,
MX25R2035F, MX25R4035F, MX25R8035F, MX25R1635F, MX25R3235F, MX25R6435F,
MX25L3225D, MX25L3255D

- Giantec

GT25D05C, GT25D10C, GT25D20C, GT25Q40C, GT25D05E, GT25D10E, GT25D20E, GT25Q80A,
GT25Q16A, GT25Q32A-H, GT25Q64A-S

- GigaDevice

GD25F40, GD25F80, GD25D40, GD25D80, GD25D05C, GD25D10C, GD25Q20C, GD25Q40C,
GD25Q80C, GD25Q16C, GD25Q32, GD25Q64CSIG, GD25Q128CSIG, GD25Q256CSIG, GD25VQ21B,
GD25VQ40C, GD25VQ80C, GD25VQ16C, GD25LD05C(1.8V), GD25LD10C(1.8V),
GD25LD20C(1.8V), GD25LD40C(1.8V), GD25LQ80C, GD25LQ16C, GD25LQ32E,
GD25LQ64E(1.8V), GD25LQ128(1.8V), GD25LQ256D(1.8V), GD25WD05C, GD25WD10C,
GD25WD20E, GD25WD40E, GD25WD80C, GD25WQ20E, GD25WQ40E, GD25WQ80E, GD25WQ16E,
GD25WQ32E, GD25WQ64E, GD25WQ128E, GD25WB256E, GD25LB512ME(1.8V), YC25Q128

- Nantronix

N25S80, N25S32

- PARAGON

PN25F08, PN25F16, PN25F32, PN25F64, PN25F128

- DOSILICON

DS25Q64A, DS25Q4AA, DS25M4CB, DS25M64E, DS25M4AE, DS25M4AB, DS25M4BA

- WINBOND

W25P80, W25P16, W25P32, W25P64, W25X05, W25X10, W25X20, W25X40, W25X80, W25X16,
W25X32VS, W25X64, W25Q05CL, W25Q10Cl, W25Q20CL, W25Q40BV, W25Q80BL, W25Q16DV,
W25Q32BV, W25Q64BV, W25Q128BV, W25Q256FV, W25Q20BW(1.8V), W25Q80,
W25Q10EW(1.8V), W25Q20EW(1.8V), W25Q40EW(1.8V), W25Q80EW(1.8V), W25Q16JW(1.8V),
W25Q32FW(1.8V), W25Q64DW(1.8V), W25Q128FW(1.8V), W25Q256JW(1.8V),
W25M512JW(1.8V), W25Q512JV, W25M512JV, W25Q32JW(1.8V)

- Fidelix

FM25Q04A, FM25Q08A, FM25Q16A, FM25Q32A, FM25Q64A, FM25Q128A, FM25M04A(1.8V),
FM25M08A(1.8V), FM25M16A(1.8V), FM25M32A(1.8V), FM25M64A(1.8V), FM25M4AA(1.8V)

- UCUNDATA (UCUN Technology)

UC25HQ05, UC25HQ10, UC25HQ20, UC25HQ40, UC25HQ80, UC25HQ16, UC25HQ32, UC25HQ64,
UC25IQ128

### SPI NAND Flash

- ESMT

F50L2G41XA, F50L1G41LB(2M), F50L512M41A, F50L4G41XB

- MICRON

MT29F4G01ABBFD12, MT29F4G01ABAFD12, MX35LF1G24AD-Z41, MX35LF2G24AD-Z41

- MXIC

MX35LF4G24AD-Z41, MX35LF2G24AD-Z418, MX35LF4G24AD-Z418, MX35LF1GE4AB,
MX35UF1G24AD-Z4I, MX35UF2G24AD-Z4I, MX35UF4G24AD-Z4I, MX35UF2G24AD-Z4I8,
MX35UF4G24AD-Z4I8, MX35UF1G14AC, MX35UF2G14AC, MX35UF1GE4AC, MX35UF2GE4AC,
MX35UF1GE4AD, MX35UF2GE4AD, MX35UF4GE4AD, MX35LF2GE4AD, MX35LF4GE4AD

- WINBOND

W25N512GW, W25N512GV, W25N01KW, W25N01KV, W25N01JW, W25N01GW, W25N01GV,
W25N02KW, W25N02KV, W25N02JW, W25N04LW, W25N04KW, W25N04KV

- XTX

XT26G01C, XT26G01A, XT26G01D, XT26G11C, XT26G11D, XT26Q01D, XT26G02D, XT26G02C,
XT26G12D-B, XT26Q02D, XT26Q04D
XTX	XT26Q14D

- Dosilicon

DS35Q12B, DS35M12B, DS35Q12C, DS35M12C, DS35Q1GA, DS35M1GA, DS35Q1GB, DS35M1GB,
DS35Q2GA, DS35M2GA, DS35Q2GB, DS35M2GB, DS35Q2GBS, DS35M2GBS, DS35Q4GM,
DS35Q4GM, DS35Q4GB, DS35M4GB

- ZETTA

ZD35Q1GC-IB

- FORESEE

F35SQA001G, F35SQB004G, F35SQA512M, F35UQA002G, F35SQA002G, GD5F1GQ5UExxG

- GigaDevice

GD5F1GQ5RExxG, GD5F1GM7UExxG, GD5F1GM7RExxG, GD5F1GM9UExxG, GD5F1GM9RExxG,
GD5F1GQ4UFxxG, GD5F1GQ4RFxxG, GD5F1GQ4UAxxG, GD5F2GQ5UExxG, GD5F2GQ5RExxG,
GD5F2GM7UExxG, GD5F2GM7RExxG, GD5F4GQ6UExxG, GD5F4GQ6RExxG, GD5F4GM8UExxG,
GD5F4GM8RExxG, GD5F1GQ4UExxG, GD5F1GQ4RExxG, GD5F1GQ4UFxxS, GD5F1GQ4RFxxS,
GD5F2GQ4UExxG, GD5F2GQ4RExxG

最新芯片数据库可从[这里](https://antenna-dvb-t2.ru/dl_all/IMSProg.Dat)下载，并复制到上游 Linux 用户数据目录 `~/.local/share/imsprog`：

`cp IMSProg.Dat ~/.local/share/imsprog`

## 软件包

以下为上游发行与第三方打包信息，保留供参考；这些链接中的软件包不包含本分支新增的 Windows 源码修改。

- 上游正在推进进入 Debian 及其衍生发行版的软件仓库。部分 Ubuntu 版本可使用 `ppa:bigmdm/imsprog`：

```sh
sudo add-apt-repository ppa:bigmdm/imsprog
sudo apt update
sudo apt install imsprog
```

也可以使用上游 `debian/latest` 分支手动构建 deb 包。

- 部分 Fedora 版本提供官方包：

```sh
sudo dnf install imsprog
```

<a href="https://repology.amdmi3.ru/project/imsprog/versions">
    <img src="https://repology.amdmi3.ru/badge/vertical-allrepos/imsprog.svg" alt="软件包状态" align="right">
</a>

- [Red Soft](https://redos.red-soft.ru/) 的 RPM 包见[仓库](http://repo.red-soft.ru/redos/7.3/x86_64/updates/)。
- [ALT Linux RPM](https://packages.altlinux.org/en/sisyphus/srpms/IMSProg/)。
- [ROSA Linux RPM](https://mirror.rosalinux.ru/rosa/rosa2021.1/repository/x86_64/contrib/release/)。
- [kjkent](https://github.com/kjkent) 维护的 [Arch AUR 包](https://aur.archlinux.org/packages/imsprog)。
- [openSUSE 软件包](https://software.opensuse.org/package/IMSProg?search_term=imsprog)。
- [Debian 软件包](https://tracker.debian.org/pkg/imsprog)。
- [Anime4000](https://github.com/Anime4000) 提供的[另一 Windows 发行版](https://github.com/Anime4000/IMSProg/releases/latest)，与本分支独立。

## 项目结构

以下为上游 Linux 安装文件布局；Windows 路径以对应构建资料为准。

```
IMSProg _文件_							          _目录_
================================================================================
IMSProg                                                  /usr/bin
IMSProg_editor                                           /usr/bin
IMSProg_database_update                                  /usr/bin

IMSProg_programmer/database/IMSProg.Dat                  /usr/share/imsprog
                                                         ~/.local/share/imsprog

IMSProg_programmer/other/71-CH341.rules                  /lib/udev/rules.d

IMSProg_programmer/other/index.html                      /usr/share//doc/imsprog/html

IMSProg_programmer/language/chipProgrammer_ru_RU.qm      /usr/share/imsprog
IMSProg_programmer/language/chipProgrammer_es_ES.qm      /usr/share/imsprog
IMSProg_programmer/language/chipProgrammer_de_DE.qm      /usr/share/imsprog
IMSProg_programmer/language/chipProgrammer_zH_CN.qm      /usr/share/imsprog
IMSProg_programmer/language/chipProgrammer_uk_UA.qm      /usr/share/imsprog
IMSProg_programmer/language/chipProgrammer_hu_HU.qm      /usr/share/imsprog
IMSProg_programmer/language/chipProgrammer_pt_BR.qm      /usr/share/imsprog

IMSProg_editor/language/chipEditor_ru_RU.qm              /usr/share/imsprog
IMSProg_editor/language/chipEditor_de_DE.qm              /usr/share/imsprog
IMSProg_editor/language/chipEditor_es_ES.qm              /usr/share/imsprog
IMSProg_editor/language/chipEditor_zh_CN.qm              /usr/share/imsprog
IMSProg_editor/language/chipEditor_uk_UA.qm              /usr/share/imsprog
IMSProg_editor/language/chipEditor_hu_HU.qm              /usr/share/imsprog
IMSProg_editor/language/chipEditor_pt_BR.qm              /usr/share/imsprog

IMSProg_database_update/language/chipUpdater_ru_RU.qm    /usr/share/imsprog
IMSProg_database_update/language/chipUpdater_de_DE.qm    /usr/share/imsprog
IMSProg_database_update/language/chipUpdater_es_ES.qm    /usr/share/imsprog
IMSProg_database_update/language/chipUpdater_zh_CN.qm    /usr/share/imsprog
IMSProg_database_update/language/chipUpdater_uk_UA.qm    /usr/share/imsprog
IMSProg_database_update/language/chipUpdater_hu_HU.qm    /usr/share/imsprog
IMSProg_database_update/language/chipUpdater_pt_BR.qm    /usr/share/imsprog

IMSProg_programmer/other/IMSProg.desktop                 /usr/share/applications
IMSProg_programmer/other/IMSProg_editor.desktop          /usr/share/applications
IMSProg_database_update/other/IMSProg_database_update.desktop /usr/share/applications

IMSProg_editor/other/IMSProg_editor.1.gz                   /usr/share/man/man1
IMSProg_programmer/other/IMSProg.1.gz                      /usr/share/man/man1
IMSProg_database_update/other/IMSProg_database_update.1.gz /usr/share/man/man1

IMSProg_programmer/img/IMSProg64.png                     /usr/share/pixmaps
IMSProg_database_update/img/IMSProg_database_update.png  /usr/share/pixmaps
img/chipEdit64.png                        /usr/share/pixmaps

IMSProg_programmer/other/other/io.github.bigbigmdm.imsprog.metainfo.xml /usr/share/metainfo
IMSProg_editor/other/other/io.github.bigbigmdm.imsprog_editor.metainfo.xml /usr/share/metainfo
IMSProg_database_update/other/io.github.bigbigmdm.imsprog_database_update.metainfo.xml /usr/share/metainfo

```
## 芯片数据库格式

`IMSProg.Dat` 文件结构：

每条芯片记录长度：0x44（68）字节。以下保留所有字段偏移与编码。

```
偏移	 大小   含义
00 -     2F     ASCII 芯片类型、型号和厂商；以 0x00 填充
30        1     NOR FLASH 容量编码   / 0x00 - 其他芯片
31        1     NOR FLASH 芯片 ID 编码     / 0x00 - 其他芯片
32        1     NOR FLASH 厂商编码 / 0x00 - 其他芯片
33              0x00
34        4     芯片容量
35 	
36 	  
37
38        2     扇区大小
39
3A        1     芯片类型编码：
                - 0x00 - SPI NOR FLASH
                - 0x01 - 24xxx I2C
                - 0x02 - 93xxx MicroWire
                - 0x03 - 25xxx SPI EEPROM
                - 0x04 - 95xxx ST SPI EEPROM
                - 0x05 - AT45DBxxx SPI EEPROM
3B        1     算法编号：
                - SPI NOR Flash 固定为 0x00
                - I2C (24xxx) 0x?1 - 地址长度 1 字节
                - I2C (24xxx) 0x?2 - 地址长度 2 字节
                - I2C (24xxx) 0x?A - 地址长度 2 字节, 用于 24LC1025
                - I2C (24xxx) 0x?E - 地址长度 2 字节, 用于 24LC515
                - I2C (24xxx) 0x1? - 地址掩码 1
                - I2C (24xxx) 0x3? - 地址掩码 3
                - I2C (24xxx) 0x7? - 地址掩码 7
                - MicroWire (93xxx) - 0x1? - 16 位组织方式
                - MicroWire (93xxx) - 0x0? - 8 位组织方式
                - MicroWire (93xxx) - 0x?7 -  7 位地址
                - MicroWire (93xxx) - 0x?9 -  9 位地址
                - MicroWire (93xxx) - 0x?A - 10 位地址
                - MicroWire (93xxx) - 0x?B - 11 位地址
                - 95xxx ST SPI EEPROM - 0x01 - 8 位地址
                - 95xxx ST SPI EEPROM - 0x02 - 16 位地址
                - 25xxx SPI EEPROM  - 0x?0 - 8 位地址
                - 25xxx SPI EEPROM  - 0x?1 - 16 位地址
                - 25xxx SPI EEPROM  - 0x?2 - 24 位地址
                - 25xxx SPI EEPROM  - 0x0? - 通过填充擦除
                - 25xxx SPI EEPROM  - 0x1? - 命令的第 3 位用于地址高位
                - 25xxx SPI EEPROM  - 0x2? - 使用 CHIP FULL ERASE 命令擦除
                - 45xxx SPI EEPROM  - 0x0? - ATMEL 命令模式
                - 45xxx SPI EEPROM  - 0x1? - ADESTO 命令模式
                - 45xxx SPI EEPROM  - 0x?9 -  9 位扇区地址
                - 45xxx SPI EEPROM  - 0x?A - 10 位扇区地址
                - 45xxx SPI EEPROM  - 0x?B - 11 位扇区地址
                - 45xxx SPI EEPROM  - 0x?C - 12 位扇区地址
                - 45xxx SPI EEPROM  - 0x?D - 13 位扇区地址
                - 45xxx SPI EEPROM  - 0x?F - 15 位扇区地址
                - SPI NOR Flash - 访问安全寄存器的算法编号
                - SPI NAND Flash - 读取状态和安全寄存器的算法编号
3C        2     总线速度延迟系数：
3D              bus_speed = default_bus_speed * Delay / 1000
3E        2     SPI NOR Flash 4 字节地址类型（原文称 4bit）：
                - 0x?0 - 不使用（3 字节地址）
                - 0x?1 - 使用（4 字节地址）
                - 0x01 - 默认使用 4 字节地址命令
                - 0x11 - Winbond 算法
                - 0x21 - Spansion 算法
3F        1     0x00
40        1     SPI NOR FLASH 块大小，单位 KB（64K = 0x40）
41        1     0x00
42        1     - EEPROM 页编码 0x01 - 0x04
                - SPI NAND Flash：ECC Size / 64（数据库中的 OOB 大小编码）
43        1     VCC 00=>3.3V 01=>1.8V 02=>5.0V 03=>2.5V
末尾记录由 0x44（68）个零字节组成。
```
上游默认总线速度：

| 芯片类型／编程器 | CH341a v1.2 | CH341a v1.7 | CH347t v1.0 | CH347T v1.1 | FH232H v1.2 |
| :---                   |      :---:  |     :---:   |     :---:   |    :---:    | :---:  |
| SPI NOR Flash          |   1.6 Mhz   |     1.6 Mhz |     30 Mhz  |    15 Mhz   | 30 Mhz |
| 25xxx EPROM            |   1.6 Mhz   |     1.6 Mhz |      5  Mhz |    5  Mhz   |  5 Mhz |
| 24xxx EPROM            |   0.4 Mhz   |     0.4 Mhz |    0.4  Mhz |  0.4  Mhz   | 0.4 Mhz|
| 93xxx EPROM            |   1.6 Mhz   |     1.6 Mhz |      5  Mhz |    5  Mhz   |  5 Mhz |
| 45xxx DataFlash        |   1.6 Mhz   |     1.6 Mhz |      5  Mhz |    5  Mhz   |  5 Mhz |
| SPI NAND Flash         |   1.6 Mhz   |     1.6 Mhz |     60 Mhz  |    15 Mhz   | 30 Mhz |

## 许可证

Copyright (C) 2023 - 2026 Mikhail Medvedev.

本项目使用 [GPL-3+](https://www.gnu.org/licenses/gpl-3.0.html)，许可证正文见 [LICENSE](LICENSE)。来自 [SNANDer](https://github.com/McMCCRU/SNANDer/commits?author=McMCCRU) 的部分代码使用 [GPL-2+](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html)，[QHexEdit2](https://github.com/Simsys/qhexedit2) 使用 [LGPL-2.1](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html)，[CH347](https://github.com/981213/ch347-nor-prog/tree/master) 的部分代码使用 [BSD-1-Clause](https://github.com/spdx/license-list-data/blob/main/text/BSD-1-Clause.txt)。各文件中的原始版权与许可证声明保留。

## 翻译与贡献者

- 匈牙利语：[blackPantherOS](https://github.com/blackPantherOS)。
- 意大利语：Marino Pasquale 和 [Giovanni Scafora](https://github.com/giovanni69)。
- 巴西葡萄牙语：[Alexandre C. Leite](https://github.com/alexandrecleite)。
- 乌克兰语：[Sergey Golovko](https://github.com/cappelikan)。
- 简体中文：[Tian-HP](https://github.com/Tian-HP)。
- 德语：[wucke13](https://github.com/wucke13)。
- 繁体中文：[ystartgo](https://github.com/ystartgo)。

上游欢迎通过 Pull Request 新增或改进翻译。翻译文件位于 [IMSProg_programmer/language/](https://github.com/bigbigmdm/IMSProg/tree/main/IMSProg_programmer/language)、[IMSProg_editor/language/](https://github.com/bigbigmdm/IMSProg/tree/main/IMSProg_editor/language) 和 [IMSProg_database_update/language/](https://github.com/bigbigmdm/IMSProg/tree/main/IMSProg_database_update/language)。

还可以翻译以下桌面入口文件：[IMSProg.desktop](https://github.com/bigbigmdm/IMSProg/blob/main/IMSProg_programmer/other/IMSProg.desktop)、[IMSProg_database_update.desktop](https://github.com/bigbigmdm/IMSProg/blob/main/IMSProg_database_update/other/IMSProg_database_update.desktop)、[IMSProg_editor.desktop](https://github.com/bigbigmdm/IMSProg/blob/main/IMSProg_editor/other/IMSProg_editor.desktop)。

AppStream 元数据文件：[io.github.bigbigmdm.imsprog_editor.metainfo.xml](https://github.com/bigbigmdm/IMSProg/blob/main/IMSProg_editor/other/io.github.bigbigmdm.imsprog_editor.metainfo.xml)、[io.github.bigbigmdm.imsprog_database_update.metainfo.xml](https://github.com/bigbigmdm/IMSProg/blob/main/IMSProg_database_update/other/io.github.bigbigmdm.imsprog_database_update.metainfo.xml)、[io.github.bigbigmdm.imsprog.metainfo.xml](https://github.com/bigbigmdm/IMSProg/blob/main/IMSProg_programmer/other/io.github.bigbigmdm.imsprog.metainfo.xml)。
