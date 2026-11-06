Windows x64 便携版。解压整个 ZIP 后运行 `IMSProg.exe`（未签名，SmartScreen 可能提示）。

## 新功能与修复

**SPI NAND 片内 ECC**
- ECC 开启时，无法纠正的页**不再中断**读取/校验/查空：按原样读出，结束后汇总报告涉及的块。大部分页无法纠正时提示改用 ECC 关（数据由主控 ECC 写入）。
- ECC 状态解码覆盖 Winbond（全系 W25N，自动 BUF=1）、Micron 及通用规则（GigaDevice、Macronix、Kioxia、XTX、Foresee 等），不再只认 W25N01GW。
- 擦除按实际每块页数寻址；切换布局时 ECC 自动设为常用值（主数据+OOB→关，仅主数据→开）；“仅主数据 + ECC 关”写入前确认（自动流程中在擦除前确认）。

**Windows 编程器与驱动**
- 恢复原版 IMSProg 界面风格。
- 编程器菜单改为 CH341A/B、CH347T、CH347F，二级菜单选择固定 SPI 时钟（CH347：自动/60/30/15/7.5/3.75/1.875 MHz，SPI NOR 最高 30 MHz；CH341 时钟由芯片固定）。移除 FT232H。旧设置自动迁移。
- 附带沁恒官方、微软签名的驱动包（ch341wdm.inf 2.6.2025.4，CH341A/B、CH347T、CH347F 的 SPI/I2C 接口）：**编程器 > 驱动程序...** 一键安装，状态栏显示驱动是否已安装。CH347 串口驱动不受影响。
- 修复使用 WCH 官方驱动时 CH341/CH347 无法连接：WCH DLL 可从程序目录、`driver\` 或 System32 加载；CH347F 兼容 WCH 驱动 2.6.2025.x（FuncType=3、ChipMode=0）。
- 简体中文翻译更新。

## 验证
- 硬件无关回归：NAND 模拟 SPI、CH341/CH347 后端（含 ASan/UBSan）、Qt 主窗口集成测试，Windows 启动冒烟测试。
- 实机：Windows 11 + WCH 驱动，CH341B 读取 SPI NOR ID、CH347F SPI 初始化通过。

## 注意
- 1.8 V 芯片（如 W25N01GW）需要 1.8 V 供电**和信号电平转换**，软件电压选项不能替代硬件。
- 读写 SPI NAND 前请先做多次只读比对并保留备份。
- `driver\` 中的驱动和 DLL 版权归沁恒（WCH）所有，按其“随产品发行到最终用户”的用途随附；程序运行时加载，不与其链接。源码见 `SOURCE\`。

SHA-256：见同名 `.sha256` 附件
