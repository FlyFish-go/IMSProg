本版主要优化主窗口 UI，提供 Windows x64 便携包。解压后运行 `IMSProg.exe`，无需安装。

### 界面改进

- 芯片参数区支持滚动，检测、读取及操作序列保持可见。
- 增大参数控件，统一按钮、分组和间距，改善高 DPI 下的显示。
- ECC 与 OOB 继续独立选择，补充纠错开关、镜像布局和 OOB 大小的说明。
- 电压参数提示明确说明：软件选择不会改变编程器的实际供电或信号电平。
- 更新简体中文文案，十六进制编辑器明确使用等宽字体。

### 芯片数据库工具

- 补齐 `IMSProg_database_update.exe`（在线更新芯片库）和 `IMSProg_editor.exe`（手工编辑芯片信息）。
- 主窗口“文件 → 更新芯片数据库”可打开更新程序；更新成功后重启主程序。
- 三个程序共享用户目录中的 `IMSProg.Dat`，并附带更新器需要的 Qt Network / Windows TLS 运行库。
- 下载内容校验通过后再原子替换芯片库，下载或保存失败时保留原有数据库。
- 在线地址沿用源项目，实际联网更新取决于上游服务可用性。

### 下载与验证

下载 `IMSProg-Windows-x64-*.zip`；同名 `.sha256` 文件用于核验压缩包。
便携包附带数据库、Qt 运行库、USB 运行库及对应许可证和源码说明。

发布流程要求 Linux 无硬件回归、Qt 界面集成测试，以及 Windows x64 构建、依赖检查和启动检查通过。
本版没有修复或验证 CH347F + 1.8V SPI NAND 开启 ECC 后的实机报错；该问题仍需设备现场复现与排查。

---

UI refresh with scrollable chip settings, persistent operation controls, clearer independent ECC/OOB hints, updated Simplified Chinese text, and a fixed-width editor font.

Windows x64 portable package with SHA-256 checksum. Publication requires passing Linux hardware-free regressions and Windows build/bundle/startup checks. Physical CH347F / 1.8V NAND ECC behavior has not been validated by this release.
