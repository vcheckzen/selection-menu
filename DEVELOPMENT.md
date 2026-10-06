# Selection Menu 开发文档

本文面向维护者，包含架构、构建、兼容性和诊断信息。终端用户请阅读 [README.md](README.md)。

## 项目结构

```text
src/
  app.h          公共声明、配置结构、消息定义
  theme.h        VS Code Dark Modern 配色
  typography.h   字体与字号阶梯
  main.c         入口、消息窗口、全局钩子、消息循环
  popup.c        1–10 图标分层窗口（圆角、阴影、命中测试）
  settings.c     自绘设置窗口
  tray.c         托盘图标与右键菜单
  config.c       INI 读写与默认值
  detect.c       WM_GETSEL、手势调度与误触发排除
  uia.c          纯 C COM/UIA、MSAA、剪贴板异步识别
  hotkey.c       组合键格式化、名称映射、按键合成
  icons.c        翻译 / 截图图标和文件图标加载
  util.c         字体缓存、圆角绘制、分层位图、注册表、日志
  app.rc         图标、版本信息、manifest 嵌入
res/app.ico      多尺寸应用图标
tools/           图标生成等维护脚本
docs/            用户文档截图
build_mingw.bat  MinGW-w64 开发构建
build_msvc.bat   MSVC / XP 兼容构建
```

## 架构

程序采用全事件驱动，不在主循环中轮询：

- `WH_MOUSE_LL` 记录按下、抬起、拖动距离和双击计数，再投递到主消息队列。
- `WH_KEYBOARD_LL` 处理 `Ctrl+A`、`Ctrl+C`、`Shift+方向键`，并负责收起面板。
- `WM_TIMER` 完成选区延迟检测、组合键 key-up 和图标动作发送。
- 选区识别在独立工作线程执行，结果通过 `WM_APP_UIA_RESULT` 回到主线程。
- 低级钩子只记录事件，不执行跨进程 COM、剪贴板或阻塞等待，避免钩子超时。

## 选区判定

检测结果只有三态：

- `SELECT_SELECTION`：明确存在非空文本选区，允许显示面板。
- `SELECT_NONE`：明确没有选区，不显示。
- `SELECT_UNKNOWN`：目标不支持或查询失败，不显示。

识别顺序：

1. 焦点、光标、鼠标下窗口及其父窗口的 `WM_GETSEL`。
2. UI Automation `TextPattern::GetSelection`。
3. MSAA / `IAccessible::accSelection`。
4. `WM_COPY`、`Ctrl+Insert`、`Ctrl+C` 剪贴板兜底。

工作线程带超时和令牌，过期结果会被丢弃。触发手势本身不等于已选中，必须得到 `SELECT_SELECTION`。

## 触发方式

- **精确模式（默认）**：经过窗口和时间校验的拖选、双击。
- **兼容模式**：双击、三击、拖选（拖选可通过设置开关控制）。
- 两种模式共同支持 `Ctrl+A`、`Ctrl+C` 和 `Shift+方向键`。
- 系统桌面、任务栏、资源管理器以及 Chromium 工具栏会被前置过滤，避免误触发。

## 构建

### MinGW-w64（开发 / Windows 7+ 冒烟测试）

```bat
build_mingw.bat
```

产物依赖 UCRT，不适合直接发布到 Windows XP。

### MSVC（Windows XP 兼容版本）

```bat
build_msvc.bat
```

使用 x86 工具链和 `/MT` 静态 CRT，目标机器无需安装 VC++ 运行库。

## XP 兼容要点

- 目标宏为 `WINVER=0x0501`、`_WIN32_WINNT=0x0501`。
- 界面使用 GDI 自绘，不依赖 DWM、DPI API 或新版视觉样式。
- 使用传统 `Shell_NotifyIcon`，图标资源使用 XP 可读的 BMP 帧。
- 不静态链接 `uiautomationcore.dll`；无 UIA 的系统将识别结果视为 `UNKNOWN`。
- `dpiAware=false`，高分屏由系统进行 DPI 虚拟化。

## 命令行诊断参数

| 参数 | 作用 |
| --- | --- |
| `--settings` | 启动后直接打开设置窗口 |
| `--demo` | 在屏幕中央弹出一次图标面板 |
| `--dump-popup` | 输出 `popup-preview.bmp` 后退出 |

## 日志与产物

- `settings.ini`：与可执行文件同目录的用户配置。
- `trace.log`：设置 → 关于 → 触发日志开启后生成，可删除后重新生成。
- `popup-preview.bmp`：`--dump-popup` 生成的临时预览，可删除。
- `build/` 中的可执行文件、配置和用户选择的图标应保留；其它生成物可清理。
