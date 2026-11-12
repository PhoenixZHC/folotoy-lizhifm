# 荔枝直连设备诊断固件

此目录是独立验证入口，使用项目真实 HTTP/解析组件。不是正式播放器，不是正式交付，不含测试夹具或假业务数据。目标 ESP32-C3 / 8 MiB / 无 PSRAM / ESP-IDF 5.5.4，控制台使用 USB Serial/JTAG，不操作 UART0/背光引脚。

## 验证内容

1. 从已有 Wi-Fi 配置读取 SSID/密码，仅使用 NVS 只读句柄；不读账号会话、不写账号或进度、不主动擦除 NVS。
2. 连接 Wi-Fi，校时后验证公共 HTTPS 接口：每页 2 条，读取第 1/2/227/228 页；所有页号都是该真实样本的探测点，不把它们当通用分页算法。
3. 读取用户提供的真实节目详情，再对播放来源生成同主机同路径 HTTPS 候选；只有该候选的首段实际返回合法 206/Content-Range 和 ID3 或 MPEG 帧头线索才报告通过。不转 HTTP、不走代理，不跟随未验证重定向。
4. 每次请求结束释放 HTTP/TLS，记录 HTTP/业务/解析状态、响应字节、空闲堆、最小空闲堆、最大连续块和任务栈余量。不输出密码、token、Cookie 或媒体 URL。

每次最多读取 16 KiB API 正文或 4096 字节媒体；默认不下载整集、不播放、不提供 seek，不代表包含音频/LVGL 后的峰值内存。6 个探测项成功只表示本轮网络响应满足当前检查，不代表账号关注、持续播放、权限枚举或全量节目验收。

配置在 `main/Kconfig.projbuild`：默认作者/节目是用户提供的公开样本；Wi-Fi 命名空间默认 `netease_wifi`，对应参考播放器原有配网。若设备运行其他固件或尚无该配置，会明确失败；不自动尝试其他账号命名空间、导入密码或擦除恢复。设备当前固件需先确认。

## 构建

本机环境激活见项目 `docs/build-and-validation.md`，然后从本目录执行：

```bash
idf.py --version
idf.py -B build -D SDKCONFIG=build/sdkconfig build
```

输出 `build/Lizhi-Direct-Probe.bin`。配置和构建缓存被根目录 `.gitignore` 排除；不在源码中设置 Wi-Fi 密码。使用与参考工程相同的分区 CSV，但仍需核对真实设备分区，不能从本机一致推出设备一致。

## 设备操作边界

2026-11-11 用户已明确授权全新完整烧录，已完成 ESP32-C3 / 8 MiB 核对、全片擦除和三段写入；随后按授权配置 Wi-Fi，网络诊断6通过/0失败。后续仅更新已核对的应用区并保留 NVS。此授权不扩展至其他设备或发布；不复制设备 NVS。诊断没有初始化显示，当前黑屏不代表屏幕故障。

串口日志与必要的应用区备份只放项目 `.local-backup/`，不公开、不复制 NVS。若无明确批准或设备/分区不匹配，停止烧录并说明问题。日志失败项按实际原因处理，不通过重复请求掩盖 403，不跳过校时或关闭证书校验。

本轮构建与哈希、测试证据统一记录在 `docs/implementation-progress.md`。

## 可选真实试听

`CONFIG_LIZHI_PROBE_PLAYBACK=y` 开启有时限的真实试听，默认关闭。依赖复用本项目 `audio_stream` / BSP 以及锁定的 Helix、codec 组件。只有详情和媒体HTTPS首段验证通过才启动，音量25；约10秒暂停1.5秒并检查进度停止，再恢复至约30秒，最长等待60秒后请求停止，并等待下载和解码任务结束才关闭网络。未写账号或进度，不提供浏览UI；不把公开样本当账号关注列表。串口采样进度和I2S写入成功仍需要用户听音确认。

播放模式会额外连续下载音频到有上限的内存队列，读取量不再限于4096字节；不是整集下载，也不保存媒体文件。

试听构建（使用独立配置，避免切换LVGL版本残留无效配置）：

```bash
idf.py -B build -D SDKCONFIG=build/sdkconfig.playback \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.playback.defaults' build
```

依赖由本目录 `dependencies.lock` 固定；诊断入口另外精确约束LVGL9.5.0和cmake_utilities1.1.1，与参考工程一致。
