---
title: TypeTide：把 AI 翻译变成 macOS 与 Windows 的系统能力
date: 2026-08-18
tags:
  - TypeTide
  - AI 翻译
  - macOS
  - Windows
  - Ollama
---

# TypeTide：把 AI 翻译变成 macOS 与 Windows 的系统能力

> **Type in your language. Rewrite in place.** 选中一段外文，一个快捷键就地读懂；用母语输入，一次按键直接改写成目标语言。TypeTide 不要求你离开正在使用的应用，也可以让模型完全运行在自己的电脑上。

![TypeTide 图标](../branding/typetide-icon-master.png)

我们每天都在支付一笔不起眼的“翻译税”：复制文字、切换应用、粘贴到翻译工具、复制结果，再切回原来的窗口。真正打断工作的往往不是翻译本身，而是这一连串上下文切换。

![传统翻译流程与 TypeTide 的对比](assets/01-flowchart-translation-tax.svg)

TypeTide 想删掉的正是这个流程。它是一款开源、原生、双平台的系统级 AI 翻译工具：macOS 端常驻菜单栏，Windows 端常驻系统托盘，在邮件、聊天工具、浏览器、IDE、终端等应用中都能使用。

“Read”是选中已有内容并在原处读懂；“Write”则是直接用自己的语言输入，再在原输入框中改写。主标语聚焦第二个动作，因为它最能体现 TypeTide 的名字和与普通翻译工具的差别；划词翻译仍然是完整的一等功能。

## 两件事：读懂与写出

### 读：选中文字，译文就在旁边出现

在任意应用中选中外文，按下默认快捷键：

- macOS：`⌥ D`
- Windows：`Alt + D`

TypeTide 会在选区附近打开浮窗，流式显示译文。结果可以复制，也可以直接替换原文。除了快捷键，还可以启用选区旁的浮动图标或划词自动翻译。

![选中文字后流式显示译文](../screenshots/translate.gif)

### 写：用母语输入，原地改成目标语言

先用自己最熟悉的语言写完内容，再按：

- macOS：`⌥ R`
- Windows：`Alt + R`

原文会在当前输入框中直接变成目标语言，不需要把结果从另一个窗口搬回来。

![在当前输入框中原地改写](../screenshots/rewrite.gif)

替换通过系统粘贴完成，因此仍然保留应用原生的撤销能力：macOS 使用 `⌘ Z`，Windows 使用 `Ctrl + Z`。如果不希望立即替换，也可以先在浮窗里预览。

## 快捷键不是写死的

`⌥ D / Alt + D` 与 `⌥ R / Alt + R` 只是默认值。两个快捷键都能独立修改，也能分别决定翻译方向和结果动作。

现在还可以彻底清空其中任意一项。只保留“读”或只保留“写”，TypeTide 都能正常工作；如果两项都清空，设置页会明确提醒至少配置一个快捷键，键盘触发才能使用。浮动图标和自动翻译等非键盘触发方式不受影响。

这也解决了全局快捷键最常见的问题：与 IDE、启动器或窗口管理工具冲突时，不必被迫保留一个不会使用的组合键。

## 本地优先，但不限制模型选择

TypeTide 支持三类后端；Apple Silicon Mac 默认使用内置离线模型：

| 后端 | 适合场景 | 数据去向 |
|---|---|---|
| 内置 MLX 模型（Apple Silicon） | 下载约 2.22 GB 后离线翻译，无需 Key 或 Ollama | 本机 |
| Ollama | 隐私内容、离线使用、零按量费用 | 本机 `127.0.0.1` |
| OpenAI 兼容接口 | 更快的云模型或自建服务 | 用户配置的服务地址 |


Ollama 是一等公民，而不是附带选项。TypeTide 会列出本机已安装模型、根据机器内存给出建议，并在原配置模型不存在时选择可用模型。对于 qwen3 等思考型模型，还会关闭不必要的隐藏推理，避免一次简单翻译等待很久。

OpenAI 兼容模式使用标准 `/chat/completions` 与 SSE 流式响应，可连接 OpenAI、OpenRouter、DeepSeek、兼容服务或自己的网关。API Key 在 macOS 存入钥匙串，在 Windows 存入 Credential Manager，而不是明文写进设置文件。

## 为什么它能跨应用工作

TypeTide 首先使用系统提供的文字接口读取选区：

- macOS：Accessibility API
- Windows：UI Automation TextPattern

如果网页、Electron 应用或其他特殊控件没有暴露选中文字，TypeTide 会临时模拟复制来读取内容，完成后恢复原剪贴板。写回结果同样使用系统粘贴，因此兼容范围更广，也不会破坏撤销栈。

![系统文字接口与剪贴板兜底](assets/03-framework-capture-everywhere.svg)

macOS 需要用户授予辅助功能权限；Windows 不需要额外的系统权限。两端都可以配置跳过的应用，避免在密码管理器或其他敏感软件中触发。

## 双端原生，而不是套一层网页


两端共享相同的产品逻辑，但遵循各自平台习惯：

| | macOS | Windows |
|---|---|---|
| 技术栈 | SwiftUI / Swift | Win32 / C++20 |
| 常驻位置 | 菜单栏 | 系统托盘 |
| 取词接口 | Accessibility | UI Automation |
| 网络接口 | URLSession | WinHTTP |
| 开机启动 | SMAppService | HKCU Run |
| 模型运行 | 内置 MLX（Apple Silicon）；可选 Ollama/API | Ollama/API |

核心链路保持一致：触发 → 捕获选区 → 路由模型 → 流式翻译 → 弹窗显示或原地替换。Windows 端特别保证主消息循环不会被网络和剪贴板操作阻塞；macOS 端则对辅助功能授权、快捷键注册和选区捕获做了分层处理。

## 不记录文字的本地诊断

跨应用工具最难排查的问题通常是“为什么这次没有取到词”或“慢在模型还是慢在捕获”。TypeTide 会在本机记录开发诊断信息，包括：

- 选区捕获是否成功
- 替换是否成功
- 首个译文 token 的耗时与总耗时
- 取消与后端失败
- 使用的后端和捕获方式

这些记录不包含选中文字、译文、剪贴板内容、应用名、URL 或凭据，也不会上传到遥测服务。用户可以在设置中查看、导出或清空。

## 十种语言，四种表达风格

macOS 和 Windows 使用完全相同的十种检测与翻译语言：

- English（英语，`en`）
- 中文（`zh`）
- हिन्दी（印地语，`hi`）
- Español（西班牙语，`es`）
- Français（法语，`fr`）
- العربية（阿拉伯语，`ar`）
- বাংলা（孟加拉语，`bn`）
- Русский（俄语，`ru`）
- Português（葡萄牙语，`pt`）
- Bahasa Indonesia（印尼语，`id`）

内置模型支持忠实翻译与原位替换；正式、随意和润色需要 Ollama 或 API。可选风格为：

- 忠实：尽量准确保留原意
- 正式：适合工作邮件和专业沟通
- 随意：更自然的日常表达
- 润色：保留原意，同时让表达更像母语者

每个快捷键还能独立选择自动判断方向，或固定翻译到母语/外语，以减少混合语言文本的误判。

## 三步开始使用

1. **安装应用。** 从 [官方下载页](https://xnu.app/typetide/#top) 下载 macOS DMG、Windows 安装版 EXE 或便携版 ZIP。macOS 用户也可运行 `brew install --cask everettjf/tap/typetide`。
2. **选择后端。** Apple Silicon Mac 使用默认的 **Built-in (Offline)**，下载约 2.22 GB 模型后即可离线使用，无需 Key 或 Ollama。内置模型的 2K 输入 token 限制意味着长文本需要分段。Intel Mac 和 Windows 使用 Ollama 或自己的 API。
3. **完成首次检查。** macOS 授予辅助功能权限，Windows 无需额外系统权限；然后在设置中测试后端并验证已配置的快捷键。选中文字按“读”，输入文字按“写”。

目前系统要求为 macOS 26+ 或 Windows 10+。macOS 安装包经过 Developer ID 签名与 Apple 公证；Windows 提供无需管理员权限的按用户安装包和便携版。

TypeTide 是一个 MIT 许可的开源项目。它不试图成为另一个需要长期停留的翻译工作台，而是希望翻译像复制、粘贴一样，成为操作系统中随手可用的能力。

项目地址：[github.com/everettjf/typetide](https://github.com/everettjf/typetide)

产品主页：[xnu.app/typetide](https://xnu.app/typetide/)
