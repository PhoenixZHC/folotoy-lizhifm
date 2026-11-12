# iPhone 关注接口验证

> 2026-11-12 / Ubuntu 开发电脑：本文是早期手机协议验证方案，当前正式1.0不要求用户抓包；关注同步与设备绑定已实现，见follow-sync.md及account-and-settings.md。

目的：观察用户自己在荔枝 App“我的 → 关注”中实际发出的请求，核实关注列表分页与鉴权。
官网主播平台扫码已成功，但尚未证明其会话可读取移动端关注。

网页版调查结果见 [网页版关注接口调查](web-follow-investigation.md)。已检查新版资源清单内的业务脚本及旧版入口，当前尚未找到关注接口；优先观察实际客户端请求。

## 只有蜂窝网络时

用户目前使用 iPhone 蜂窝网络并向设备提供热点，不能套用下文的同一路由器 Wi-Fi 代理流程。

可尝试 [Stream（中国区 App Store）](https://apps.apple.com/cn/app/stream/id1312141691)。已核实其商店说明：通过本机 Network Extension 抓取 HTTP/HTTPS，需允许本地 VPN 配置，HTTPS 还需要安装并信任 CA；应用免费下载并有应用内购买，不承诺所有能力免费。[Charles for iOS 官方说明](https://www.charlesproxy.com/documentation/ios/)也明确支持 Mobile / Cellular 网络抓包，此说明仅支持方法可行性，不代表已验证 Stream 在用户手机上的效果。

用户尚未确认执行手机配置。确认后按工具当前指引操作：

1. 安装工具，允许本地 VPN，并按其 HTTPS 指引安装临时 CA；“设置 → 通用 → 关于本机 → 证书信任设置”中启用相应证书。解释其能够解密经过工具的 HTTPS 请求。
2. 开始抓包，仅打开荔枝“我的 → 关注”，刷新并翻一页，随后立即停止。
3. 先确认是否可看到实际 JSON；不要在聊天中粘贴 Cookie、令牌或完整 HAR。后续只核对脱敏路径、字段名和分页结构。
4. 完成后停止本地 VPN，取消信任并移除本次证书。若热点与抓包冲突，可在用户方便时暂时关闭热点抓取手机自身请求。

本机抓包和热点同时运行尚未实测；若出现 TLS 错误或无法解密，记录真实失败，不绕过证书校验。暂未修改手机配置。

## 当前边界

- 仅在用户同意后，临时配置 iPhone Wi-Fi 代理并安装、信任本机生成的测试 CA。
- 手机和 Ubuntu 电脑需要网络互通。手机仅以蜂窝网络开热点时，不能直接套用 Wi-Fi 手动代理步骤。
- 代理只解密荔枝域名；其他域名透传。保留上游 TLS 证书校验，不使用 `ssl_insecure`。
- 只打开关注页面并翻页，不执行关注/取消关注、发布、付款或账号修改。
- 不导出原始抓包，不把 Cookie、Authorization、请求参数值或账号资料写入调查记录。
- 原始会话仅在工具内存中处理；固件不使用本机代理，手机也不是日常音频源。
- 若 App 不接受临时 CA，记录失败，不绕过 App 的证书校验，不声称抓包已完成。

## 本机准备

Ubuntu 开发电脑使用独立目录 `.local-backup/lizhi-capture-venv`，不修改 ESP-IDF 的 Python 环境。
已安装并确认 mitmproxy 12.2.1 / Python 3.12.15，pip依赖检查通过。

插件：`tools/lizhi_follow_capture.py`。仅记录域名、路径模板、查询/表单字段名、JSON结构、数组长度、HTTP状态。
长数字ID/不透明路径片段隐藏，不记录业务字符串值。
输出 `.local-backup/lizhi-auth-research/follow-capture-summary.jsonl`，文件权限0600。

脱敏验证：

```bash
python3 -m unittest discover -s tools -p test_lizhi_follow_capture.py
```

本机预检只绑定回环地址；未取得手机配置同意前，不开放局域网监听：

```bash
.local-backup/lizhi-capture-venv/bin/mitmdump \
  --listen-host 127.0.0.1 --listen-port 8085 \
  --set confdir=.local-backup/lizhi-capture-ca \
  --set flow_detail=0 --set termlog_verbosity=error \
  --set stream_large_bodies=1m --set ssl_insecure=false \
  --allow-hosts '(^|\.)(lizhi\.fm|lizhifm\.com|lz310\.com)(:[0-9]+)?$' \
  -s tools/lizhi_follow_capture.py
```

已核对12.2.1支持 `--allow-hosts` 正则参数；实际连通结果另见实施记录。

## 手机操作（需用户明确同意）

1. 手机和电脑接入同一路由器Wi-Fi，核对电脑当时的局域网IP。
2. 电脑监听该局域网IP后，手机“设置 → Wi-Fi → 当前网络 → 配置代理 → 手动”，填电脑IP和8085。
3. 在 Safari 打开 `http://mitm.it`，安装当前工具生成的iOS证书；在“设置 → 通用 → 关于本机 → 证书信任设置”中仅信任这张临时证书。
4. 打开荔枝“我的 → 关注”，下拉刷新，再向后翻页。对照返回结构、条目数量与手机真实列表，不混淆粉丝或推荐。
5. 完成后将此Wi-Fi的代理改回关闭，并删除/取消信任临时证书；停止本机代理。

临时证书允许本机代理解密经过它的HTTPS，必须先向用户解释这一影响并取得同意；不索要聊天凭证。
清理证书文件、环境目录或日志仍按用户授权执行，不自动删除项目文件。
