<div align="center">

# 🔊 VolumeBooster — 系统音量增强器

**Windows 10/11 系统级音量增强工具**

将系统音量提升至 200%、300% 甚至 500%

[![Windows](https://img.shields.io/badge/Platform-Windows%2010%2F11-blue.svg)](https://github.com/ya-chang/VolumeBooster)
[![License](https://img.shields.io/badge/License-GPL%20v3-green.svg)](LICENSE)
[![Language](https://img.shields.io/badge/Language-C%2B%2B%20%7C%20C%23-orange.svg)](https://github.com/ya-chang/VolumeBooster)

</div>

---

## ✨ 功能特性

- **系统级增强** — 所有应用程序生效（微信、浏览器、游戏等）
- **超 100% 音量** — 支持 100% ~ 500% 增益调节
- **Per-App 增益** — 对单个应用设置独立音量增强
- **软限幅器** — tanh 算法防止削波失真，保护扬声器
- **实时电平表** — 显示当前输出 dB 值，防止过载
- **预设管理** — 音乐/微信通话/电影/游戏一键切换
- **全局快捷键** — Ctrl+Alt+↑/↓ 快速调节
- **系统托盘** — 最小化到托盘，不占任务栏

---

## 🏗️ 技术架构

```
┌─────────────────────────────────────────────────┐
│  微信 / 浏览器 / 游戏 / 任意应用                   │
└──────────────────┬──────────────────────────────┘
                   ▼
┌─────────────────────────────────────────────────┐
│  Windows 音频引擎 (WASAPI)                        │
│  ┌───────────────────────────────────────┐       │
│  │  Stream Effect APO (sAPO)             │  核心 │
│  │  - 增益: 100% ~ 500%                  │       │
│  │  - 软限幅: tanh 防削波                 │       │
│  │  - 电平: Ring Buffer 异步写入          │       │
│  └───────────────────────────────────────┘       │
└──────────────────┬──────────────────────────────┘
                   ▼
┌─────────────────────────────────────────────────┐
│  扬声器 / 耳机 / 蓝牙设备                         │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│  GUI 控制面板 (WPF)        ←→  Named Pipe        │
│  - 增益滑块                                     │
│  - 实时电平表                                    │
│  - Per-App 增益                                  │
│  - 预设 / 快捷键 / 托盘                           │
└─────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────┐
│  设备监听服务 (Windows Service)                    │
│  - 监听设备变更                                   │
│  - 自动注册 APO                                  │
│  - 蓝牙检测提示                                   │
└─────────────────────────────────────────────────┘
```

---

## 📦 安装

### 方法一：使用安装程序（推荐）

1. 从 [Releases](https://github.com/ya-chang/VolumeBooster/releases) 下载 `VolumeBooster-Setup.exe`
2. **以管理员身份运行**安装程序
3. 安装完成后程序自动启动

### 方法二：从源码构建

```bash
# 克隆仓库
git clone https://github.com/ya-chang/VolumeBooster.git
cd VolumeBooster

# 一键构建（需要 Visual Studio 2022 + .NET 8 SDK）
build.bat
```

---

## 🔧 开发环境

| 依赖 | 版本 |
|------|------|
| Visual Studio | 2022 |
| Windows SDK | 10.0+ |
| .NET SDK | 8.0+ |
| NSIS | 3.0+ (可选，用于构建安装程序) |
| CMake | 3.20+ |

---

## 📁 项目结构

```
VolumeBooster/
├── CMakeLists.txt              # C++ 构建配置
├── build.bat                   # 一键构建脚本
├── src/
│   ├── APO/                    # 音频处理核心 (C++ DLL)
│   │   ├── VolumeBoosterAPO.h  # COM 接口 + 数据结构
│   │   ├── VolumeBoosterAPO.cpp# 增益 + 限幅 + 电平
│   │   └── DllMain.cpp         # DLL 入口 + COM 工厂
│   ├── DeviceListener/         # 设备监听服务 (C++)
│   │   └── DeviceListener.cpp  # 设备变更监听 + APO 注册
│   ├── GUI/                    # 控制面板 (C#/WPF)
│   │   ├── *.xaml              # 界面布局
│   │   ├── *.cs                # 业务逻辑
│   │   └── *.csproj            # .NET 项目文件
│   └── Installer/              # 安装程序 (NSIS)
│       └── installer.nsi       # 安装/卸载脚本
└── 音量增强器-设计方案.md        # 详细设计文档
```

---

## ⚠️ 注意事项

- **签名问题**：APO 运行在 `audiodg.exe`（SYSTEM 进程）中，需要代码签名。开发阶段需开启测试模式：`bcdedit /set testsigning on`
- **扬声器保护**：长时间高增益（>300%）可能损坏低质量扬声器，请适度使用
- **蓝牙延迟**：蓝牙设备本身有 100-300ms 延迟，音量增强无法改善此延迟
- **独占模式**：使用 ASIO 等独占模式的应用不经过 APO 处理

---

## 📜 License

[GPL v3](LICENSE)

---

<div align="center">

**如果觉得有用，请给个 ⭐**

</div>
