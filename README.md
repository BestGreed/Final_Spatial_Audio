# Final Spatial Audio

Windows 11 x64 常驻托盘音频模式管理工具，使用纯 C11 / Win32 实现。

支持手动切换 Stereo、5.1、7.1、Dolby Atmos for Home Theater、DTS:X for home theater，以及基于前台窗口或运行中进程的优先级自动切换。

## 下载与运行

从 [Releases](https://github.com/BestGreed/Final_Spatial_Audio/releases) 下载 Windows x64 便携包，完整解压后运行 `bin/FinalSpatialAudio.exe`。程序首次启动为手动锁定，不主动改变当前声音。请保留目录结构，并放在当前用户可写的位置。

默认标准模式配置为 24 bit / 48 kHz；首次使用会为当前设备单独生成快照。空间音频需 Windows、连接设备和相应编码器支持。发布包不含个人设备快照、日志或已启用的个人规则。

## 功能

- Foreground / Running 规则，全局按优先级仲裁，支持高优先级后台应用保持模式。
- 自动切换勾选项、手动锁定、外部修改检测、防抖、重复切换抑制与失败回滚。
- 意图捕获：把当前系统实际配置保存为对应模式的快照。
- 设备能力检测、不可用模式置灰、当前用户开机自启。
- Win32 托盘菜单、半透明材质、圆角、DPI 缩放和系统 Segoe Fluent Icons。

使用方法、规则字段、备份与卸载见 [中文使用说明](docs/使用说明.txt)。架构见 [ARCHITECTURE.md](ARCHITECTURE.md)。

## 构建

准备 Windows x64 与 Zig 0.14.1，然后在 PowerShell 执行：

```powershell
./native/build.ps1 -Zig <zig.exe完整路径>
```

构建运行基础测试并产生 `native/bin/FinalSpatialAudio.exe`；如需从源码目录运行，可将 `native/final-spatial-audio.example.ini` 复制为 `native/final-spatial-audio.ini`。示例规则默认禁用。

生成与校验便携包：

```powershell
./release.ps1 -Zig <zig.exe完整路径> -Version 0.1.0
```

测试范围见 [TESTING.md](TESTING.md)。无需另装 WinUI、Windows App SDK、AutoHotkey 或音频辅助程序。

## 兼容性

目标为 Windows 11 x64；部分音频策略和材质路径使用非公开系统 ABI，不能承诺覆盖所有 Windows 更新、设备及驱动组合。切换失败会尝试恢复，仍建议先在自己的设备上验证。当前构建未签名。后台运行在用户会话中，不是 Windows 系统服务；尚无 AudioActive / WASAPI Session 规则。

## 许可

原创源码及文档采用 [Unlicense](LICENSE)。图标不属于公共领域授权范围，详见 [图标许可说明](native/assets/LICENSE.txt)。编译运行时及系统资源说明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 和 `licenses/`。
