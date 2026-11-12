# 来源与第三方说明

## 本次复制

- 直接来源：[Folo Music / PhoenixZHC](https://github.com/PhoenixZHC/FoloToy-wyy-musicplayer)，提交 `f369b3b5f46fa325bca5c81952bd547bb68aec23`，版本 2.1.1。
- 直接上游：[FoloToy AI Passport](https://github.com/FoloToy/ai-passport)。原 MIT 许可全文保留在 `firmware/ai-passport/LICENSE`，未替换版权声明。
- 本次复制源路径、目标路径和哈希见 `docs/source-manifest.json`；未复制宣传图片、账号会话或设备镜像。
- 字库许可原文保留在 `firmware/ai-passport/main/fonts/font-licenses/`，包括 Noto CJK 与 Nimbus Roman。生成字库也保留原文件名。
- ESP-IDF、LVGL、Helix MP3、BSP 依赖通过原组件声明与 `dependencies.lock` 锁定。没有把托管组件打包进源码；恢复依赖和发布固件时必须保留各依赖及底层解码器的许可证，不能把整个固件一概称为 MIT。
- 网易云协议模块是迁移参考，原项目注明参考 [api-enhanced](https://github.com/neteasecloudmusicapienhanced/api-enhanced)。荔枝的认证协议必须独立验证，不能沿用网易云 EAPI/WEAPI。

## 荔枝接入的外部研究来源

- [RSSHub 荔枝路由](https://github.com/DIYgod/RSSHub/blob/master/lib/routes/lizhi/user.ts)：本次仅参考接口形状、字段映射与分页参数，没有复制其 TypeScript 实现。
- RSSHub 仓库采用 AGPL-3.0。后续若决定复制、改写其代码，需要先评估适用义务并保留许可；不要把参考接口事实等同于可以任意搬运实现。
- 不依赖 RSSHub 运行服务，不把其公开实例作为设备直连失败后的备用入口。
- 开源代码许可不授予荔枝账号或音频内容使用权。首阶段只验证账号可正常访问的公开节目，不包含付费内容解锁。

本仓库按用户授权整理并推送源码及当前固件；本文件是来源说明，不代表已完成全部二进制依赖许可审计。
