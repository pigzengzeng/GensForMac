# Gens for Mac

一个原生 macOS（Intel x86_64）的世嘉 Mega Drive / Genesis 模拟器。

以 **Genesis Plus GX** 的成熟 C 内核为基础，配上一个从零编写的 macOS 前端：SDL2 视频/音频/输入 + Cocoa 原生菜单栏 + 屏幕自绘设置界面，最终打包成一个**完全独立、零外部依赖**的 `Gens.app`——拷到任何一台 Intel Mac 上双击即玩。

![platform](https://img.shields.io/badge/platform-macOS%2010.13%2B%20(Intel)-blue)
![lang](https://img.shields.io/badge/language-C%20%2F%20Objective--C-green)

---

## 项目来龙去脉

这个项目起源于一个朴素的愿望：**在 Mac 上重温 Gens 模拟器的使用体验**。

Gens 是 2000 年代最经典的世嘉 MD 模拟器之一，但它是 Windows/Linux（GTK）程序，从未有过 macOS 版本。直接移植旧版 Gens 代码并不现实——大量 x86 汇编、32 位假设和 GTK 依赖在现代 64 位 macOS 上寸步难行。

于是本项目采取了「老壳新核」的路线：

1. **内核**：采用 Genesis Plus GX 血统的 C 内核（`core/`）。它源自 Charles Mac Donald 的原始代码，由 Eke-Eke 长期维护，64 位干净、精度极高，支持 MD / Master System / Game Gear / Sega CD 等硬件的模拟（本前端聚焦 MD 卡带游戏）。
2. **前端**：`src/` 下的全部代码为本项目原创编写，包括：
   - SDL2 视频渲染（含拉伸/比例、扫描线、灰度/亮度/对比度等滤镜）、音频输出、输入轮询；
   - **macOS 原生菜单栏**（Objective-C / Cocoa），画面、音频、控制等设置直接从菜单操作；
   - **屏幕自绘控制设置界面**（内置 8×8 点阵字体，零第三方依赖），键盘与手柄按键均可在屏幕上交互式重定义；
   - 拖放 ROM 加载、设置持久化（`~/.gensmacrc`）。

### 开发过程中踩过并解决的坑（真实记录）

| 问题 | 根因与解法 |
|---|---|
| 手柄完全没反应 | 内核只读 `input.pad[0]`（玩家1）和 `input.pad[4]`（玩家2），`pad[1..3]` 是 Team Player 副位，普通游戏不读。手柄曾被错误分配到死槽位；修正为菜单端口 → 真实槽位映射表 |
| 手柄方向键失灵 | 廉价手柄（如 BETOP C3）的十字键接在 **hat** 上而非标准 DPAD 按钮，sdl2-compat 的 hat→按钮翻译不可靠；改为直读底层 joystick 的 hat 0 + 左摇杆轴 |
| LT/RT 扳机无法绑定 | 扳机在 SDL 中是**模拟轴**不是按钮，不产生按钮事件；引入"伪按钮"编码让扳机进入按键映射表，拉动 25% 即算按下 |
| .app 启动即崩溃 | `snprintf` 漏传一个 `%s` 实参导致野指针，交互模式才触发 |
| Homebrew 的 sdl2 是 sdl2-compat | 它的构造函数在 dylib 被搬入 .app 后会 abort，无法随包分发；最终改为**从源码静态链接真正的 SDL2**，实现完全独立的 .app |

## 功能特性

- 世嘉 Mega Drive / Genesis 游戏模拟（`.bin` / `.md` / `.gen` / `.smd`，支持 zip）
- 拖放 ROM 即玩；关联常见 ROM 扩展名
- macOS 原生菜单栏：画面比例/拉伸、VSync、渲染滤镜、扫描线、色彩调整、控制器设置入口
- 屏幕交互式按键设置：
  - 键盘 + 手柄双列映射，任意功能键可重绑定
  - 支持 3键/6键手柄类型切换、8 个逻辑端口（Team Player）
  - 手柄全输入识别：按钮、十字键（hat）、左摇杆、LT/RT 扳机
  - 底部实时手柄诊断行（检测数量、名称、目标端口、按下即亮）
  - 一键 RESET DEFAULTS 恢复出厂映射
- 非标准手柄兜底：未被识别为 GameController 的裸摇杆也可用、可绑键
- 设置自动持久化到 `~/.gensmacrc`
- `Gens.app` 完全独立：SDL2 已静态链接，无需安装任何依赖

## 快速开始

### 直接运行

下载/构建得到 `Gens.app`，双击打开，把 ROM 文件拖进窗口即可开始游戏。

### 默认键盘操作（玩家1）

| 功能 | 按键 |
|---|---|
| 方向 | 方向键 |
| A / B / C | A / S / D |
| X / Y / Z | Q / W / E |
| START | 回车 |
| MODE | 右 Shift |

手柄插上即用（十字键/左摇杆 = 方向，X/A/B = ABC，LB/RB = X/Z，START/BACK = 开始/MODE）。所有按键均可在 **菜单栏 Controllers → Configure Controls…** 中重新绑定。

## 从源码构建

要求：Xcode 命令行工具（Apple clang）。SDL2 无需预装——首次构建时脚本会自动下载 SDL2 源码并静态编译。

```sh
# 1. 构建静态 SDL2（仅首次需要，之后缓存复用）
./build_sdl2.sh

# 2. 构建模拟器
make

# 3. 打包成独立的 Gens.app
make app
```

产物：项目根目录下的 `Gens.app`。

## 项目结构

```
GensForMac/
├── core/           # Genesis Plus GX 模拟内核（C，64位干净）
│   ├── m68k/ z80/  # CPU 内核
│   ├── sound/      # YM2612/PSG 等音频芯片模拟
│   ├── input_hw/   # 手柄/外设协议模拟
│   └── cd_hw/      # Sega CD 硬件（含 libchdr）
├── src/            # macOS 前端（本项目原创）
│   ├── gens_mac.c  # 主程序：SDL2 视频/音频/输入、主循环
│   ├── cocoa_menu.m# macOS 原生菜单栏（Objective-C）
│   ├── ui.c/font.c # 屏幕自绘设置界面 + 内置点阵字体
│   ├── settings.c  # 设置模型与 ~/.gensmacrc 持久化
│   └── video_fx.c  # 扫描线/灰度/亮度等视频滤镜
├── vendor/         # 静态 SDL2 构建目录（脚本自动生成）
├── Makefile
├── make_app.sh     # .app 打包脚本
└── build_sdl2.sh   # SDL2 源码下载与静态编译脚本
```

## 许可

- **`core/`（模拟内核）**：沿用 Genesis Plus GX 的许可条款 —— Copyright (C) 1998-2003 Charles Mac Donald，Copyright (C) 2007-2026 Eke-Eke。**允许自由使用与再分发，但禁止出售或用于任何商业用途**；修改后的再分发必须附带完整源代码。详见各源文件头部声明。
- **`src/`（macOS 前端）**：本项目原创代码，遵循与内核相同的非商业条款发布。
- **SDL2**：zlib 许可。

本项目仅供学习与怀旧用途。请仅使用你合法拥有的游戏 ROM。

## 致谢

- **Charles Mac Donald** — Genesis Plus 原作者
- **Eke-Eke** — Genesis Plus GX 的长期维护者，本项目内核的直接来源
- **Stéphane Dallongeville** — Gens 原作者，本项目名称与体验的致敬对象
- **SDL 社区** — 跨平台底层库
