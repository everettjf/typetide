<h1 align="center">TypeTide</h1>
<p align="center"><b>用你的语言输入，在 macOS 和 Windows 的任何应用里原地改写。</b></p>

<p align="center">
  <img src="https://img.shields.io/badge/macOS-26+-black.svg" />
  <img src="https://img.shields.io/badge/Windows-10+-0078d4.svg" />
  <img src="https://img.shields.io/badge/Swift-5.9+-orange.svg" />
  <img src="https://img.shields.io/badge/C++-20-00599c.svg" />
  <img src="https://img.shields.io/badge/AI-Ollama%20%7C%20OpenAI--compatible-7c5cff.svg" />
  <img src="https://img.shields.io/badge/License-MIT-green.svg" />
  <a href="https://discord.gg/eGzEaP6TzR"><img src="https://img.shields.io/badge/Discord-加入-5865F2?logo=discord&logoColor=white" /></a>
</p>

<p align="center">
  <a href="https://xnu.app/typetide/">🌐 官网</a> ·
  <a href="docs/blog/typetide-project-introduction.zh.md">📝 项目介绍</a> ·
  <a href="README.md">English</a> · <b>简体中文</b>
</p>

TypeTide 把翻译变成一个原地完成的系统动作。它常驻菜单栏（macOS）或系统托盘（Windows），在**任何**应用里都能用：

- **读** — 选中外语文字，按下快捷键，译文就在旁边弹出。
- **写** — **用母语输入**，按下快捷键，文字原地改写成目标语言，直接可以发送。

不用打开浏览器标签页，也不用在翻译网站之间来回复制粘贴。Apple Silicon Mac 默认使用**内置离线模型**，下载一次即可使用，无需 Ollama 或 API Key。也可以选择 **Ollama** 或自己的 API；Intel Mac 和 Windows 使用后两种方式。

<p align="center">
  <img src="docs/screenshots/rewrite.gif" alt="输入中文，按下改写快捷键，原地改写为英文" width="760" />
  <br/><sub><i>写 · 用母语输入，在 macOS 按 <kbd>⌥R</kbd>，或在 Windows 按 <kbd>Alt+R</kbd>，原地改写成目标语言。</i></sub>
</p>

<p align="center">
  <img src="docs/screenshots/translate.gif" alt="选中文字，译文流式显示在弹窗里" width="760" />
  <br/><sub><i>读 · 选中文字，译文流式显示在弹窗里——可复制或替换。</i></sub>
</p>

<p align="center">
  <img src="docs/screenshots/icon.png" alt="选区旁的浮动翻译图标" width="520" />
  <br/><sub><i>可选的浮动图标，紧贴你的选区出现。</i></sub>
</p>

---

## ✨ 功能

- **读 · 翻译** — 在任意位置选中文字 → 弹窗显示译文。可用快捷键、选区旁的浮动图标或划词自动翻译触发。
- **写 · 原地改写** — 用母语写字，按改写快捷键，输入框内容被替换成译文。可直接替换，也可先预览。
- **每个应用都能用** — 通过 Accessibility API（macOS）/ UI Automation（Windows）读取选区，对 Electron/网页类应用自动降级为剪贴板复制，文字接口不可用时也能工作。原剪贴板内容总会被还原。
- **替换不破坏撤销** — 替换通过粘贴完成，保留每个应用原生的撤销栈（Ctrl+Z / ⌘Z 随时可撤销）。
- **本地或云端** — 可插拔后端：**内置 MLX 模型**（Apple Silicon Mac）、**Ollama**（离线、私密）或任意 **OpenAI 兼容** API（`/chat/completions`，流式）。设置里一键切换。
- **模型选择** — 设置里列出已安装的 Ollama 模型，根据机器内存推荐合适尺寸；配置模型没装时自动换成已安装模型。思考型模型（qwen3 系等）会自动关闭隐藏推理，翻译保持秒回。
- **风格** — 内置模型支持忠实翻译与原位替换；正式、口语和润色改写需要 Ollama 或 API 后端。
- **快捷键动作可组合** — 两个全局快捷键都能独立选择智能方向或固定母语/外语目标，并决定弹窗展示或原地替换。
- **内置可用性检查** — 首次设置会检查权限、后端翻译和快捷键注册；后端测试只发送固定合成文本，不读取剪贴板。
- **本地隐私诊断** — 运行结果、耗时、后端和取词方式只保存在本机，可查看、导出或清空；不会记录选中文字、译文、剪贴板、应用名、URL 或凭据。
- **只住菜单栏 / 托盘** — 不占 Dock、不占任务栏。全局快捷键、开机自启、按应用跳过列表。
- **双平台全原生** — macOS 用 SwiftUI，Windows 用 Win32 C++。没有 Electron，没有运行时。

## 💡 为什么选 TypeTide

| | TypeTide | 浏览器翻译网站 | 系统自带翻译 |
|---|---|---|---|
| 任何应用里都能用（邮件、聊天、IDE、终端） | ✅ | ❌（来回粘贴） | ⚠️ 仅菜单 |
| 回复场景**原地改写** | ✅ | ❌ | ❌ |
| 100% 离线 / 私密 | ✅（内置模型或本地 Ollama） | ❌ | ⚠️ |
| 自选模型 / 接口 | ✅ | ❌ | ❌ |
| 流式输出 | ✅ | ⚠️ | ❌ |

## 🌍 语言

macOS 和 Windows 使用完全相同的 10 种检测与翻译语言：**English（英语，`en`）、中文（`zh`）、हिन्दी（印地语，`hi`）、Español（西班牙语，`es`）、Français（法语，`fr`）、العربية（阿拉伯语，`ar`）、বাংলা（孟加拉语，`bn`）、Русский（俄语，`ru`）、Português（葡萄牙语，`pt`）、Bahasa Indonesia（印尼语，`id`）**。

在**设置 → Language** 里选好你的母语和外语。读/写快捷键（macOS 为 `⌥D / ⌥R`，Windows 为 `Alt+D / Alt+R`）各有独立方向：**自动**检测选中文字的语言并翻译成另一种，混合语言容易误判时也可以钉死**固定**方向。

## 🚀 快速开始

**1. 安装 TypeTide**

macOS——推荐用 [Homebrew](https://brew.sh)：
```bash
brew install --cask everettjf/tap/typetide
```
……或者从 [官方下载页](https://xnu.app/typetide/#top) 下载最新 `.dmg`，拖进「应用程序」后启动。安装包使用 Developer ID 签名，并通过 Apple 公证。

Windows——从 [官方下载页](https://xnu.app/typetide/#top) 下载最新的 `TypeTide-Setup-x.y.z.exe` 安装包运行即可（按用户安装，无需管理员权限；也提供便携版 `TypeTide-Windows-x.y.z.zip`）。如果 SmartScreen 提示无法识别的应用，点**更多信息 → 仍要运行**。

**2. 选一个后端**

**Apple Silicon Mac：**保留默认的 **Built-in (Offline)**，在首次设置或 **Settings → Backend** 中下载模型（约 2.22 GB）。下载后可离线翻译，无需账号、API Key、Ollama 或 Python。支持忠实翻译；超过 2K 输入 token 的长文本需要分段。模型遵循 [Gemma 使用条款](https://ai.google.dev/gemma/terms)。

**Intel Mac、Windows，或希望使用其他本地模型：**先安装 [Ollama](https://ollama.com/download)，然后：
```bash
# macOS: brew install ollama · Windows: winget install Ollama.Ollama
ollama pull qwen3.5:4b   # 或任何你喜欢的对话模型
ollama serve             # 通常已作为服务在运行
```
……或者用云端：在**设置 → Backend** 里选 *OpenAI-compatible*，填入 base URL、API key 和模型名。

TypeTide 会在**设置 → Backend** 里列出你已安装的模型；配置的模型没装时会自动换成已装的——拉取*任意*一个对话模型就能跑起来。

**3. 首次运行**

macOS：在**系统设置 → 隐私与安全性 → 辅助功能**里允许 TypeTide（读取选区和替换文字需要）。Windows 无需任何特殊权限。然后：

- 选中文字 → **⌥D**（macOS）/ **Alt+D**（Windows）→ 看译文。
- 用母语输入 → **⌥R** / **Alt+R** → 原地改写。

快捷键及其动作、触发方式、风格、语言都可以在**设置**里改；Windows 组合键也可以加入 Windows 键。可用**测试连接**和**验证快捷键**在正式使用前确认整条链路。

## ⌨️ 默认快捷键

| 操作 | macOS | Windows |
|---|---|---|
| 翻译选中文字（读） | <kbd>⌥</kbd><kbd>D</kbd> | <kbd>Alt</kbd><kbd>D</kbd> |
| 改写并替换（写） | <kbd>⌥</kbd><kbd>R</kbd> | <kbd>Alt</kbd><kbd>R</kbd> |

这只是默认值，并非强制设置。你可以在**设置 → 快捷键**中彻底清空任意一项；如果两项都清空，TypeTide 会提醒键盘触发已不可用，建议至少保留一个。

## 🧠 工作原理

```
选中 / 输入  ─►  快捷键 · 浮动图标 · 自动
                        │
        取词（AX / UI Automation 选区 ─► 剪贴板兜底）
                        │
          翻译（内置 MLX / Ollama / OpenAI 兼容，流式）
                        │
      读：弹窗展示  ·  写：原地粘贴（可撤销）
```

## 🔒 隐私与系统权限

TypeTide 能跨应用读取和替换选中文字，因此 macOS 需要辅助功能权限；Windows 通过 UI Automation 工作，无需单独授权。只有在触发快捷键、点击浮动图标或开启划词自动翻译后，应用才会取词。可在**设置 → Excluded apps** 中停用包含敏感信息的应用；如果不希望监听选区，请使用**仅快捷键**模式。

- 使用 Apple Silicon 上的**内置离线模型**时，文字在 Mac 上处理；首次使用需联网下载模型。
- 使用 **Ollama** 时，取到的文字只会发往本机 `127.0.0.1` 上的 Ollama 服务；TypeTide 没有中转服务器或遥测服务。
- 使用 **OpenAI 兼容后端**时，触发翻译后，取到的文字会发往你配置的 base URL，并受该服务商的数据保留与隐私政策约束。密码、密钥和敏感对话不应使用云端后端处理。
- 某些应用不通过辅助功能接口暴露选区。TypeTide 会临时复制选区，读取后恢复原剪贴板；剪贴板历史或第三方剪贴板管理器仍可能记录这次临时复制。
- API key 在 macOS 中存入钥匙串，在 Windows 中存入 Windows Credential Manager。

## 🏗️ 项目结构

```
macos/                 # SwiftUI 应用（Xcode 工程）
├── TypeTide/
│   ├── Core/          # SelectionCapture、TextReplacer、TriggerController、
│   │                  # SelectionMonitor、PopupPositioner、LaunchAtLogin 等
│   ├── Translation/   # TranslationProvider、Ollama / OpenAI provider、
│   │                  # TranslationService、缓存、模型自动纠正
│   ├── UI/            # 翻译弹窗 + 浮动选区图标
│   ├── Config/        # AppSettings、LanguageConfig
│   ├── Services/      # GlobalShortcutCenter、UpdateChecker、系统服务
│   └── Views/         # 菜单栏 + 设置界面
└── scripts/           # 发布 / 公证脚本

windows/               # 原生 Win32 C++20 应用（CMake）
├── src/               # 托盘、热键、UIA 取词、弹窗、provider、
│                      # 设置窗口、替换器、语言检测
└── assets/            # 图标

docs/                  # GitHub Pages 站点 + 博客 + 截图
```

## 🔨 构建与发布

macOS：
```bash
cd macos
open TypeTide.xcodeproj          # ⌘R 运行

# 签名发布 + 公证 DMG（先导出 APPLE_ID、APPLE_SPECIFIC_PASSWORD、APPLE_TEAM_ID）
./scripts/build-release.sh      # → build/TypeTide-x.y.z.dmg
```

要求：macOS 26+、Xcode 15+。应用**未**沙盒化（需要辅助功能权限 + 合成键盘事件）。本地开发构建请用你的 Apple Development 团队签名，这样辅助功能授权在重新构建后依然有效。

Windows：
```powershell
cd windows
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build             # → build/TypeTide.exe
```

要求：Windows 10+、Visual Studio 2022+（MSVC、CMake、Ninja）。零第三方依赖——纯 Win32 + WinHTTP + UI Automation。架构与测试体系见 [windows/README.md](windows/README.md)。

**发版** —— 双端共享一个版本号（根目录 `VERSION` 文件），可以以任意先后顺序附着到同一个 GitHub release：

```bash
./scripts/bump-version.sh            # patch +1，同步 pbxproj / CMake / rc / manifest
# 提交后，在各自平台上：
./scripts/release-windows.ps1        # Windows：构建安装包 + zip，创建/补传 release
./scripts/release-macos.sh           # macOS：构建公证 DMG，创建/补传 release
```

哪个平台先发就由它创建 `vX.Y.Z` tag，另一个平台稍后把自己的产物传到同一个 release 上。需要先 `gh auth login` 一次。

## 🔧 常见问题

- **快捷键没反应** → macOS：确认辅助功能已授权（设置 → General 显示 *Granted*）且菜单栏里 TypeTide 处于开启状态。Windows：可能有其他应用占用了该热键——在**设置 → Shortcuts** 里换一个。
- **没有译文** → Ollama：`ollama serve` 在运行吗？模型装了吗？（配置的模型没装时 TypeTide 会自动换成已装的。）OpenAI：检查 base URL / key / 模型名。
- **qwen3 之类的模型翻译特别慢** → TypeTide 已为 Ollama 模型自动关闭隐藏「思考」；如果还是慢，可能是模型对你的硬件太大了——在**设置 → Backend** 的下拉框里换个小的。
- **某些应用里弹窗位置不准** → 这些应用不暴露文字边界；弹窗会退回到光标位置。
- **Sequoia 或更早的 macOS 提示无法打开** → TypeTide 要求 **macOS 26+**。它基于当前的 SwiftUI 菜单栏和设置 API 构建，保持单一现代基线是小项目可靠性的前提。暂无支持旧版 macOS 的计划。
- **哪个语言填哪里？** → 在**设置 → Language** 里选你的**母语**和**外语**（没有容易搞反的“源/目标”概念）。每个快捷键有自己的方向；**自动**会检测选中文字的语言并翻译成另一种。

## 🤝 参与贡献

欢迎提 Issue 和 PR。目前持续维护英文和简体中文 README；如果贡献者愿意一起保持内容同步，也欢迎增加其他语言翻译。来 [Discord](https://discord.gg/eGzEaP6TzR) 聊聊。

## 📄 许可证

[MIT](LICENSE) · 为 macOS 和 Windows 用 ❤️ 打造。如果 TypeTide 帮到了你，点个 ⭐️ 就是最好的支持！
