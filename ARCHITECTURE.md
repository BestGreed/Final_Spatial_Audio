# 架构

简体中文 | [English](ARCHITECTURE.en.md)

单一用户进程：UI 线程处理托盘菜单，工作线程串行执行设备读取、规则仲裁和音频写入。系统声音模式不会因菜单绘制而改变。

Foreground 事件和 Running 观察统一触发 Resolver，所有有效规则按最高优先级决策，同分按配置顺序。切换经过防抖、状态比较和结果验证；检测外部修改时回到手动锁定。

PCM 通过 MMDevice / WASAPI 和 PolicyConfig 获取完整端点快照。空间选择使用独立空间策略接口；不重放编码载体格式。Profile 按端点保存，文件采用版本检查与临时文件替换。

| 文件 | 职责 |
|---|---|
| native/final_spatial_audio.c | 应用入口、工作线程、托盘和调度 |
| native/audio_layout.c/.h | PCM 快照、预检、写入、校验与恢复 |
| native/spatial_policy.c | 空间音频接口封装和诊断入口 |
| native/profile_store.h | 配置生成、保存和校验 |
| native/resolver.h | 规则优先级、防抖与等待时间 |
| native/fluent_menu.c/.h | 菜单绘制、键鼠导航与子菜单交互 |
| native/language.h | 语言选择与中英文界面文字表 |

运行时仅依赖 Windows 系统 DLL；二进制仍包含编译器提供的启动或运行时支持代码，其许可独立列出。源码仓库与便携包均不携带用户配置数据。

语言在工作线程启动前确定：读取 Language 覆盖项和 Windows 用户界面语言。只读文字表供菜单、托盘及提示框使用，标准辅助功能菜单使用同一语言。运行中不轮询语言，不增加线程或依赖；修改语言重启生效。
