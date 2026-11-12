# 网页版关注接口调查

> 2026-11-12 / Ubuntu 开发电脑：本文是官网关注接口的历史调查记录；后续已通过普通关注二进制接口实现真实同步，正式1.0使用设备官方扫码绑定，见follow-sync.md和account-and-settings.md。

更新时间：2026-11-11T16:05:57+08:00（Asia/Shanghai）；来源设备：Ubuntu 开发电脑。

## 当前结论

官网扫码会话有效，但本轮没有找到读取账号“我关注的博主”的网页接口。不能把粉丝数、主播粉丝入口、公开节目列表或 Mock 数据当作关注同步；也不能由此断言服务端不存在该接口。

本轮没有修改固件、烧录或导出账号凭证。关注同步仍未实现。

## 已验证的范围

| 对象 | 实际证据 | 结论与边界 |
| --- | --- | --- |
| `https://www.lizhi.fm/` | 隔离浏览器请求 HTTP 200；首页登录链接为主播管理平台 | 未找到听众“我的关注”入口 |
| `https://www.lizhi.fm/user/5036275272706175532` | 页面及其加载的脚本可读取；“关注”文案实际显示 `fansCount` | 是粉丝数量，不是当前账号的关注列表 |
| 官网首页与博主页脚本 | 检查实际 script 标签及浏览器资源记录中的相关脚本，并单独检查首页业务脚本 | 可见公开节目、分享和统计功能，没有取得关注列表接口 |
| 已登录主播平台 | 浏览器原会话读取 `https://njnew.lizhi.fm/user/getCurrentUserInfo`，HTTP 200、`rcode=0`、存在 userId 字段；不输出字段值 | 仅确认会话有效，不能推出移动端关注权限可用 |
| 主播平台完整资源清单 | `https://nj.lizhi.fm/static/newsite/asset-manifest.json` 可读取；主脚本另行检查；43 份其他 JS 分块/运行时脚本全部读取成功 | 按关注/订阅及相关英文命名检索，并解码 Unicode 转义复查，没有发现关注列表定义；框架依赖未作为业务接口检查对象 |
| 旧平台入口 | 浏览器原会话 fetch `/theylike`，最终路径 `/pagenotfound`；标题为“荔枝官方主播登陆入口” | 旧入口已经失效，不是空关注列表；此前页面导航回新版平台也不能证明该入口可用 |
| 旧平台公共脚本 | 读取 grayVersion、公共 m.js、im.js；grayVersion 解释了旧首页跳新版平台的前端行为 | 未取得关注列表接口，不通过改灰度标志伪装服务可用 |

检查中未执行关注/取消关注、账号修改、上传或付费操作。未猜测并批量探测鉴权接口。

## 开源参考排查

- [gcmwhite/lizhiFM](https://github.com/gcmwhite/lizhiFM)：检查 API 目录源码，提供公开网页/音频解析，没有取得账号关注实现。
- [yrq1429/vue-lizhiFM](https://github.com/yrq1429/vue-lizhiFM)：存在 `Follow.vue`，但只是占位页面；`src/api/getData.js` 请求 easy-mock，不能用于生产数据接入。
- [musicdl 荔枝适配](https://github.com/CharlesPikachu/musicdl/blob/master/musicdl/modules/audiobooks/lizhi.py)：可作为公开搜索、详情、博主节目和播单的参考，没有取得账号关注接口。未复制其媒体地址猜测或回退逻辑。
- 搜索引擎结果不充分，不能作为“网上没有接口”的证据；公共代码搜索服务 grep.app 返回 429，本轮未覆盖其索引。

公开资源和检查摘要保留在忽略目录 `.local-backup/lizhi-auth-research/creator-assets/`。这里只记录脱敏结论，不记录浏览器 Cookie、登录令牌或账号资料。

## 下一步依赖

继续实现关注同步，需要观察官方客户端“我的 → 关注”的实际请求，确认方法、路径、分页、鉴权和真实列表是否一致。用户目前只有 iPhone 蜂窝网络/热点，不能使用需要同一路由器 Wi-Fi 的电脑代理流程。

可以尝试 iPhone 本机抓包，方法与边界见 [iPhone 关注接口验证](iphone-follow-verification.md)。尚未在用户手机上验证，不能保证临时证书被荔枝接受，或本地抓包与热点同时工作。没有协议证据前不编写假接口，也不以手动博主列表替代目标。
