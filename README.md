# FoloToy 荔枝 FM 播客播放器

让 FoloToy AI Passport 直接连接荔枝，浏览关注的播客并收听节目。当前正式版本 **1.0（含电量显示）**，适用于 **ESP32-C3 / 8 MiB Flash / 无 PSRAM** 的对应硬件。

设备直连荔枝，不需要自建服务器，也不需要手机转发音频。手机仅用于荔枝 App 扫码确认账号；收听期间设备需要联网。

## 下载与安装

| 固件 | 写入地址 | 用途 |
| --- | --- | --- |
| [完整 BIN](dist/FoloToy-Lizhi-1.0-full-8MiB.bin) | `0x0` | 首次安装，严格 8 MiB，包含引导程序、分区表及应用；**会清空原有设置、账号、网络和播放历史** |
| [应用 BIN](dist/FoloToy-Lizhi-1.0-app.bin) | `0x10000` | 本项目相同分区布局的日常更新，保留设置；首次安装请用完整 BIN |

下载后可用 [SHA256 清单](dist/SHA256SUMS.txt) 校验。不要将固件写入不匹配的硬件。

使用支持 ESP32-C3 的烧录工具，选择设备串口，按上表填写地址。如果已安装 ESP-IDF 5.5.4 的 esptool，在仓库根目录执行（将 `/dev/ttyACM0` 换成实际串口）：

```bash
# 首次安装：写入完整镜像，清空原设置
python -m esptool --chip esp32c3 --port /dev/ttyACM0 write_flash 0x0 dist/FoloToy-Lizhi-1.0-full-8MiB.bin
```

写入完成后重启设备。首次启动需要按下文配网并绑定荔枝账号。

## 首次使用

### 1. 准备网络

准备可上网的 **2.4 GHz Wi-Fi**，也可以使用设置为 2.4 GHz 的手机热点。5 GHz 网络不适用。本固件在设备上直接选网、输入密码；不提供配网小程序、设备热点或网页配网入口。

### 2. 在设备上配网

1. 首页短按上/下选择 **设置**，短按 **OK** 进入，再选择 **无线网络**。
2. 等待扫描，选择自己的 Wi-Fi 或手机热点，短按 OK。
3. 密码页短按上/下选择字符，短按 OK 输入；长按上/下切换字符组。
4. 输入完成后长按 OK 打开操作菜单，选择 **连接此网络**，短按 OK 提交。输错可在同一菜单选择 **删除末尾字符**，也可切换密码显示。
5. 看到 **“网络连接成功，配置已保存”** 表示完成。长按 OK 返回，继续绑定账号。

若失败，检查密码、热点是否开启及网络频段，返回网络列表重新扫描、选择并连接。更换网络失败会尝试恢复原网络配置；保存失败会明确提示。手机热点恢复后若未自动连上，可在播放失败页长按 OK 手动重试。

### 3. 绑定荔枝账号

1. 手机安装并登录官方 **荔枝 App**。
2. 设备首页选择 **绑定荔枝账号**，或进入 **设置 → 荔枝账号 → 扫码绑定账号**。
3. 用荔枝 App 扫描设备屏幕二维码，在手机上确认登录。二维码过期可短按 OK 刷新。
4. 绑定成功后返回首页，进入 **我的关注**，即可查看与账号对应的关注博主。需要更换账号时进入 **设置 → 荔枝账号 → 切换账号**。

### 4. 开始收听

进入 **我的关注 → 博主 → 节目**，短按 OK 开始播放。博主与节目列表每屏四条，上下移动到边缘可翻页，选中的长节目标题会滚动。首页 **最近播放** 保留最近四集的名称与收听时间，选择后从记录位置继续。

## 按键说明

| 场景 | 操作 | 功能 |
| --- | --- | --- |
| 首页、列表、设置 | 短按上/下，短按 OK | 移动、选择 |
| 列表、设置 | 长按 OK | 返回；密码页为操作菜单 |
| 播放中 | 短按 OK | 暂停 / 继续 |
| 播放中 | 短按上/下 | 上一集 / 下一集 |
| 播放中 | 长按上/下约 0.6 秒后松手 | 进入音量页并增减音量 |
| 音量页 | 短按上/下，按 OK | 调整音量、保存并返回 |
| 正常播放页 | 长按 OK（默认约 1.5 秒） | 打开快捷菜单 |
| 播放失败页 | 长按 OK | 重连并保留可验证位置重试 |
| 熄屏时 | 任意按键第一次操作 | 只唤醒屏幕 |

快捷菜单包含停止返回列表、音量、快退 15 秒、快进 30 秒和从头播放。正常听完后顺序播放下一集。快进退依赖已验证的音频帧锚点，无法定位时会明确提示。

## 已有功能与限制

- 官方 App 扫码绑定、切换与退出账号，真实关注同步及缓存。
- 播客专用播放界面、长标题滚动、四行列表、最近四集及断点续播。
- 暂停、音量、快进退、自然下一集、网络失败手动重试。
- 屏幕亮度、自动熄屏、右上角电量显示；电量不可读时显示 `--%`。

进度在暂停、停止和播放期间每分钟保存，突然断电可能损失最近一分钟。需要联网获取和播放节目，不支持离线下载。付费或无权限节目不会绕过服务端限制。部分手机热点恢复后需要手动重试；另一真实热点及另一账号切换尚未完成实机覆盖。电量采用参考项目默认参数，未做本电芯完整标定。

已有用户验收包括真实关注、四行翻页、音量、最近播放及 20 分钟连续播放；续播与自然切集有设备日志证据。详细范围见 [交付验证](docs/delivery-validation.md)，不以编译或主机测试代替实机验收。

## 源码目录

```text
dist/                       当前 1.0 电量版 BIN 与校验清单
firmware/lizhi-player/       荔枝生产工程，ESP-IDF 构建入口
firmware/ai-passport/        共用 UI、BSP、字库、测试及保留的网易云参考入口
firmware/components/         荔枝协议、音频流及参考组件
tools/                      主机测试、打包与诊断工具
docs/                       使用、架构说明、开发记录与验证证据
```

生产工程通过 CMake 引用共用模块，因此保留参考目录，不能直接删除。`release/` 为本地历史交付包，`.local-backup/` 为本地日志与测试产物，两者以及构建缓存、托管依赖、个人配置均不提交。

## 从源码构建

准备 Git、Python、CMake、Ninja 及 **ESP-IDF 5.5.4** 的 ESP32-C3 工具链。首次构建需联网恢复锁定的组件依赖。

```bash
git clone https://github.com/PhoenixZHC/folotoy-lizhifm.git
cd folotoy-lizhifm
# 按实际安装位置加载 ESP-IDF 5.5.4
source /path/to/esp-idf-v5.5.4/export.sh
idf.py --version
cd firmware/lizhi-player
idf.py -B build -D SDKCONFIG=build/sdkconfig build
cd ../..
python3 tools/package_lizhi.py firmware/lizhi-player/build release/lizhi-1.0
```

构建及打包不会刷写设备。正常固件必须关闭 `CONFIG_LIZHI_SERIAL_TEST`，不嵌入开发账号；打包器会检查这些条件。不要将 `firmware/ai-passport` 的网易云参考产物当成荔枝固件。

主机检查（需要可用 C11 编译器及相应 IDF 头文件）：

```bash
CC=cc IDF_PATH=/path/to/esp-idf-v5.5.4 python3 tools/run_lizhi_tests.py
python3 -m unittest tools/test_package_lizhi.py
```

[构建与验证细节](docs/build-and-validation.md) 中的绝对路径仅适用于原 Ubuntu 开发电脑，历史章节不代表当前功能缺失。原始来源完整性检查会报告有意修改，不能将旧来源清单重写成当前哈希。

## 项目文档与来源

- [使用说明](docs/user-guide.md) · [当前状态](CONTEXT.md) · [架构](ARCHITECTURE.md)
- [账号与设置](docs/account-and-settings.md) · [关注同步](docs/follow-sync.md) · [播放定位](docs/playback-position.md)
- [版本说明](docs/release-1.0.md) · [完整功能版验收记录](docs/full-version-plan.md)
- [开发方案（历史）](docs/development-plan.md) · [实施历史](docs/implementation-progress.md)
- [代码复用说明](docs/code-reuse.md) · [原始来源清单](docs/source-manifest.json) · [第三方许可说明](THIRD_PARTY_NOTICES.md)

基于 FoloToy AI Passport 与 Folo Music 参考项目开发，保留上游 [MIT 许可](firmware/ai-passport/LICENSE) 及字库许可。各组件许可分别适用，不将整个固件统一声明为 MIT。荔枝账号与音频内容权利属于相应权利人，本项目不代表荔枝官方。
