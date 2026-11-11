# QFlyST

STM32在线编程工具 - 跨平台串口烧录软件

[![Build](https://github.com/Jin-OK/QFlyST/actions/workflows/build.yml/badge.svg)](https://github.com/Jin-OK/QFlyST/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/Jin-OK/QFlyST)](https://github.com/Jin-OK/QFlyST/releases)
[![License](https://img.shields.io/github/license/Jin-OK/QFlyST)](LICENSE)

## 功能特点

- ✅ 支持 STM32 内置 Bootloader 串口烧录
- ✅ 支持 Intel HEX 和 BIN 格式固件
- ✅ 自动进入 Bootloader 模式 (RTS/DTR 控制)
- ✅ 支持 RTS/DTR 电平模式选择
- ✅ 支持擦除、烧录、校验、读器件信息
- ✅ 读取芯片唯一序列号(96位UID)
- ✅ 配置持久化保存
- ✅ 实时进度显示
- ✅ 停止按钮即时响应

## 截图

![QFlyST Main Window](docs/screenshot.png)

## 下载

从 [Releases](https://github.com/Jin-OK/QFlyST/releases) 页面下载对应平台的安装包：

| 平台 | 文件 | 说明 |
|------|------|------|
| Windows x64 | QFlyST-windows-x64.zip | Windows 64位，解压后运行 QFlyST.exe |
| macOS ARM64 | QFlyST-macos-arm64.dmg | M1/M2/M3/M4芯片 Mac |
| Linux x64 | QFlyST-linux-x64.tar.gz | Linux 64位，解压后运行 |
| Linux ARM64 | QFlyST-linux-arm64.tar.gz | 树莓派等ARM设备 |

## 使用方法

### Windows
1. 下载并解压 `QFlyST-windows-x64.zip`
2. 运行 `QFlyST.exe`

### macOS
1. 下载并打开 `QFlyST-macos-arm64.dmg`
2. 将 QFlyST 拖入 Applications 文件夹
3. 首次运行如提示"已损坏"，请在终端执行：
   ```bash
   sudo xattr -cr /Applications/QFlyST.app
   ```

### Linux
1. 下载并解压 `QFlyST-linux-x64.tar.gz`
2. 在终端运行：
   ```bash
   chmod +x QFlyST
   ./QFlyST
   ```

## 硬件要求

- USB转串口模块 (CH340C/CP2102/FT232等)
- RTS 连接 BOOT0 (通过三极管控制)
- DTR 连接 RESET

### 典型连接方式

```
USB转串口模块          STM32
-------------          -------
TX    ---------------- RX
RX    ---------------- TX
RTS   --[三极管]--> BOOT0  (高电平进入Bootloader)
DTR   --[三极管]--> NRST   (低电平复位)
GND   ---------------- GND
```

## 编译

### 依赖
- Qt 6.5+
- CMake 3.16+
- 编译器：MSVC / GCC / Clang

### Windows (MSVC)
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### macOS / Linux
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### 运行
```bash
./build/QFlyST
```

## 支持芯片

- STM32F0 系列
- STM32F1 系列 (F103等)
- STM32F2 系列
- STM32F3 系列
- STM32F4 系列 (F401/F407/F411/F446等)
- STM32F7 系列
- STM32L0 系列
- STM32L4 系列

## 配置文件

配置文件自动保存，位置：
- Windows: `%APPDATA%\QFlyST\QFlyST.conf`
- macOS: `~/Library/Preferences/QFlyST/QFlyST.conf`
- Linux: `~/.config/QFlyST/QFlyST.conf`

保存内容：
- 固件路径
- 波特率设置
- 启动模式 (RTS/DTR配置)
- 校验选项
- 固件起始地址

## 致谢

- [Qt](https://www.qt.io/) - 跨平台应用框架
- 灵感来自 FlyMCU 烧录工具

## 许可证

MIT License

## 反馈

如有问题或建议，请提交 [Issue](https://github.com/Jin-OK/QFlyST/issues)。
