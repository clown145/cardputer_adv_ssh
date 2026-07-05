# cardputer_adv_ssh

[English](README.md) | 中文

这是给 M5Stack Cardputer-Adv（`cardputer_adv`）使用的原生 ESP-IDF 固件。它把设备变成一个小型 Wi-Fi SSH 终端，支持保存服务器配置、保持 SSH 会话、设备端设置，以及按需开启的局域网 WebUI。

## 功能

- 图标式启动器，Terminal 放在第一位，另有 Wi-Fi、WebUI、SSH Profiles、Status 页面。
- Wi-Fi 扫描、凭据保存、自动连接和重连。
- SSH 服务器配置保存，支持默认服务器和临时选择服务器。
- SSH 终端支持回看模式、临时缩放、ANSI 颜色、主题预设和中文字体预设。
- 支持密码登录和设备生成 SSH 密钥登录。
- WebUI 按需开启，可编辑 SSH 配置、选择终端设置、生成/复制设备公钥、设置默认服务器。
- 顶部状态图标显示 Wi-Fi、SSH 状态和电量。
- GitHub Actions 工作流支持 CI 编译和按 tag 发版打包。

## 支持语言

- 文档：默认英文，并提供简体中文版本。
- 设备 UI 和 WebUI：英文。
- 终端显示：SSH 会话输入按 UTF-8 处理，支持 ASCII/英文显示，并通过内置 M5GFX `efont_cn` 中文字体预设显示中文/CJK 字形。不支持的字形会显示为 `?`。
- 键盘输入：支持 ASCII 字母、数字、符号，以及通过 Fn 层输入控制键。设备端目前没有中文输入法。

## 硬件与固件目标

- 设备：M5Stack Cardputer-Adv
- 固件/项目名：`cardputer_adv_ssh`
- MCU 目标：ESP32-S3
- Flash：8 MB
- 框架：ESP-IDF 5.4 或更新版本

项目通过 `main/idf_component.yml` 使用 ESP-IDF 组件依赖，包括 M5Unified/M5GFX 和 ESP 版 libssh2。

## 本地编译

安装 ESP-IDF 5.4 或更新版本后运行：

```bash
idf.py set-target esp32s3
idf.py build
```

编译生成的主固件产物是 `build/cardputer_adv_ssh.bin`。

## 刷写

使用标准 ESP-IDF 刷写命令：

```bash
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

Cardputer-Adv 使用原生 USB。如果不能自动进入 ROM 下载模式：

1. 按住背面的 `BtnG0`。
2. 保持按住 `BtnG0`，轻按一下背面的 `BtnRST`。
3. 先松开 `BtnRST`，继续按住 `BtnG0` 约 1 秒。
4. 松开 `BtnG0`，屏幕应保持全黑。

然后用检测到的串口重新执行刷写命令。

## 基本使用

开机后会进入启动器，Terminal 位于第一项。

1. 打开 `Wi-Fi`，扫描并选择网络，输入密码后保存。
2. 打开 `SSH Profiles`，用 `user@host` 或 `user@host:port` 格式添加服务器，并设为默认。
3. 打开 `Terminal`，需要连接时会连接默认 SSH 配置。
4. 退出 Terminal 会回到启动器，SSH 连接会保持，不会因为退出终端页面自动断开。

如果当前已有 SSH 连接，而你选择了另一个服务器，再进入 Terminal 时会询问是否切换服务器或保留当前会话。

## WebUI

WebUI 默认关闭。需要使用时，在设备启动器中打开 `WebUI` 页面，设备屏幕会显示局域网 URL 和临时密码。

WebUI 可以用于：

- 添加、编辑、删除 SSH 配置，并设置默认服务器。
- 修改终端标签栏、主题和字体预设。
- 在设备上生成 SSH 密钥对。
- 复制设备公钥，方便添加到服务器的 `authorized_keys`。

私钥会保存在设备上。公钥会显示在 WebUI 中，方便复制到服务器。

## SSH 密钥

在 WebUI 中生成设备密钥后：

1. 复制公钥。
2. 添加到目标服务器对应用户的 `~/.ssh/authorized_keys`。
3. 保存该服务器的 SSH 配置。

固件连接时会优先尝试已保存的私钥，必要时再回退到配置中的密码。如果服务器已经接受设备密钥，密码可以留空。

## 终端按键

- `Esc`：有回看内容时进入回看模式。
- 快速按两次 `Esc`：退出 Terminal 回到启动器。
- 回看模式中，`Up`/`Down` 滚动历史。
- 回看模式中，`Left`/`Right` 临时调整终端缩放。
- `Enter` 退出回看模式，返回实时终端。

终端标签栏、配色主题、中文字体预设会保存。临时缩放不会保存。

## Fn 层按键

`Fn` 可以按住再按另一个键，也可以轻按一次，让它作用到下一个按键。

- `Fn` + `;`：上
- `Fn` + `,`：左
- `Fn` + `.`：下
- `Fn` + `/`：右
- `Fn` + 字母：发送对应的 Ctrl 字符，例如 `Fn` + `C` 是 Ctrl-C，`Fn` + `D` 是 Ctrl-D，`Fn` + `L` 是 Ctrl-L。
- `Fn` + `[`：Esc
- `Fn` + `\` / `]`：`Ctrl-\` / `Ctrl-]`
- `Fn` + `Shift` + `6`：Ctrl-^
- `Fn` + `Shift` + `-`：Ctrl-_

## 发版工作流

`.github/workflows/firmware-release.yml` 会在 push 和 PR 时编译固件。推送版本 tag 时会发布 GitHub Release：

```bash
git tag v0.1.0
git push origin v0.1.0
```

Release 产物包括：

- 固件：`cardputer_adv_ssh.bin`
- SHA-256 校验文件
- Release README

GitHub 会自动为每个 tag 提供标准的源码 ZIP 和 TAR.GZ 压缩包。

也可以在 GitHub Actions 页面手动运行 workflow，并可选填写 `release_tag`。

## 仓库结构

- `main/` - 固件应用代码
- `components/libvterm/` - 内置终端模拟器依赖
- `partitions.csv` - 8 MB Flash 分区表
- `sdkconfig.defaults` - 默认 ESP-IDF 配置
- `docs/` - 设计与架构文档
- `.github/workflows/` - CI 与发版自动化

## 许可证

本项目使用 GNU General Public License v3.0。见 [LICENSE](LICENSE)。

## 备注

本固件专门面向 Cardputer-Adv。普通 Cardputer 外形相似，但不要在未确认硬件变体前直接假定可以使用同一固件。
