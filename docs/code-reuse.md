# 代码复用与复制范围

> 2026-11-12 / Ubuntu 开发电脑：正式1.0使用独立荔枝入口；原始复制清单和下文旧产物/阶段记录保留不变，最新状态见CONTEXT.md。

来源：`/home/phoenix/code/folotoy-wangyiyun`；Git 提交 `f369b3b5f46fa325bca5c81952bd547bb68aec23`。复制于 2026-11-10，来源设备：Ubuntu 开发电脑。

## 已复制

共 143 份来源文件，13,173,717 字节；逐文件来源、目标与 SHA-256 见 [清单](source-manifest.json)。只从源 Git 跟踪文件中按白名单选取，复制内容保持字节一致，不使用软链接或依赖原工程绝对路径。

| 目标位置 | 用途 | 后续处理 |
| --- | --- | --- |
| `firmware/ai-passport/components/bsp/` | 屏幕、按键、电量、I2C、I2S、引脚事实 | 直接复用，不改板级引脚 |
| `main/wifi_provisioning_*` | 配网逻辑、页面与旧 portal 测试参考 | 设备选网复用，账号导入若需 portal 单独设计 |
| `main/display_*`、`device_music_ui*`、`ui_pixel*` | 主题、熄屏、时钟、文字与列表样式 | 保留通用逻辑，分离网易云状态和文案 |
| `main/fonts/` | 中文字库、生成清单、字体许可证 | 直接复用，不重复生成大字库 |
| `firmware/components/audio_stream/` | HTTP MP3、下载/解码任务、缓冲、续传、音量 | 基线复用，额外实现播客定位和持久续听 |
| `main/netease_app*`、`netease_direct_adapter*` | worker、应用状态、分页和异步 UI 参考 | 当前构建闭环所需；后续由荔枝应用替换 |
| `firmware/components/netease_client/`、`netease_direct_client/` | 旧兼容模型、直连 HTTP、受限解析与会话模式 | 当前 CMake 必需；协议不迁移到荔枝 |
| `main/music_artwork*`、`music_lyrics*`、`demo_radio*` 及原 demo 源码 | 既有编译依赖与实现参考 | 歌词不属于荔枝首版；未编译 demo 不新增入口 |
| CMake、Kconfig、配置默认值、分区表、组件清单与锁文件 | 恢复可构建基线 | 使用 5.5.4 与直连 defaults；暂保留网易云产物名 |
| `tests/`、`tools/` | 主机回归、字体、打包及构建工具 | 主机 stub 不进入生产；原仓库门禁仅作参考 |
| `docs/reference/` | 硬件规格、引脚指南与热点音频经验 | 原始参考文档，顶部语言链接可能指向未复制的英文稿，以本项目文档为入口 |

保留网易云相关文件是为了保住当前 CMake 与回归依赖闭环，不代表后续产品需要这些协议。准备阶段不做跨文件改名或删除，避免把可追溯复制变成未经验证的移植。

## 未复制

- `.git`、分支、远端和提交历史；本项目尚未初始化 Git。
- `build/`、`build-hotspot/`、`managed_components/`、本机 `sdkconfig` 和完整/应用固件。
- NVS、Wi-Fi 密码、登录 Cookie、设备 QR 信息、私有抓包、串口日志和 `.local-backup/`。
- `server/` 旧中转服务、`netease-official-firmware/` 历史官方平台实验。
- 源项目根目录的未提交文档，以及上游 workflows、Skills、展示图片和玩具玩法资料。

验证时在新目录恢复了锁定的托管依赖，并生成独立 `build-reference/` 与参考打包产物；它们是本次新生成、被忽略的文件，不是从旧项目复制的缓存。新工程源码没有指向旧工程代码的软链接。

## 原门禁的边界

`firmware/ai-passport/tools/validate.sh` 会检查旧仓库的 Git、双语文档、工作流，并可能安装 actionlint、清理临时目录；当前目录只复制了开发必需子集，尚无 Git，不应直接运行这个完整门禁。

本项目提供 `tools/run_baseline_tests.py` 运行同一批 14 个主机测试程序和 4 项打包回归；不调用仓库政策检查、安装或删除步骤。固件构建按 [独立命令](build-and-validation.md) 执行。

复制一致性检查仅适用于准备快照；日后有意修改原文件，原哈希自然会不一致，应记录迁移差异并以新行为测试验收，不通过更新原始清单掩盖变化。

## 后续新增（不属于原始复制清单）

- `firmware/components/lizhi_direct_client/lizhi_detail.c` 及公开头文件：根据真实单集和节目列表响应实现纯解析，不复用网易云认证协议，不含网络、账号或存储入口。
- `firmware/ai-passport/tests/test_lizhi_detail.c`、`test_lizhi_page.c` 与 `tools/run_lizhi_tests.py`：独立主机测试；测试内只保留真实响应的公开字段投影，不复制原始响应、token 或账号字段。
- 参考固件重新配置后能发现并编译新增组件；应用没有调用此组件，主机测试源码不列入组件 CMake，不进入正式固件。原 143 份来源文件未改，来源清单保持原样。
- `lizhi_http.c`、`include/lizhi_client.h`：新增公共 HTTPS 与媒体首段探测，无网易云会话/认证复用。`tests/test_lizhi_http.c` 和 `tests/lizhi_stubs/` 只由主机入口编译；生产构建使用实际 ESP-IDF HTTP/TLS。
- `tools/lizhi_device_probe/`：独立真实设备验证工程，只引用新的荔枝组件和 ESP-IDF；使用原分区 CSV，无旧播放器、BSP、UI、字体或音频依赖。仅以只读方式复用参考播放器的 Wi-Fi SSID/密码命名空间，不读取旧账号会话。

## 关注同步接入差异（2026-11-11）

新增 `lizhi_follow.[ch]`、关注协议/分页测试；扩展荔枝HTTP为有上限二进制POST，生产继续使用ESP-IDF证书校验。应用/适配层新增真实关注及动态博主节目请求，进度加载按记录自身博主校验。复制来源中的音频改动见播放定位文档；本批不修改来源清单或网易云协议/UI字库。本机用户绑定仅在忽略配置，未写入源码。

四行列表更新：只读对照 `/home/phoenix/code/folotoy-wangyiyun/firmware/ai-passport/main/netease_app.c` 的四条可见行和列表编号方式，沿用当前已复制UI的4行/42px布局；未改来源工程或复制QQ业务。新增荔枝加载层将两个小请求组成四行视图，没有修改原JSON/二进制接口单页上限、字库或来源清单。

## 完整功能候选接入差异（2026-11-11）

新增 `lizhi_auth.[ch]`/`lizhi_auth_http.c`、`lizhi_settings.[ch]`、`lizhi_library.[ch]`，扩展适配层、账号隔离续听及设置界面。原复制文件 `device_music_ui.[ch]` 新增与荔枝相关的独立QR布局和实际播放状态接口；原网易云调用默认行为保留。`audio_stream.[ch]` 新增只在自然结束时报告一次的事件，区分手动停止和错误。原始143文件来源清单不变。

本轮准备快照检查134一致/9差异，9项均为已记录的有意迁移：上述UI两份、音频stream两份、此前下载器及头文件/CMake三份、下载器主机测试及脚本两份。基线14个主机程序与4项打包回归通过；不把来源哈希差异当作功能失败，也不改清单掩盖差异。新增 `tools/package_lizhi.py` 只生成明确标记未验收的本地开发候选，拒绝含开发账号ID的配置，不执行旧仓库发布脚本。

## 播客布局差异（2026-11-12，Ubuntu 开发电脑）

`device_music_ui.[ch]` 新增独立播客绘制接口、固定裁剪区长标题动画和列表行可选滚动标志；保留原音乐绘制接口。共享加载提示改为中性文案。`tests/ui_preview/CMakeLists.txt` 支持显式指定本项目LVGL源码；`tests/ui_preview/main.c` 增加播客/长标题/四行列表的日夜布局、字形和动画断言，测试内容不进入固件。最新来源检查132一致/11项差异，新增两项正是预览CMake和main；原始清单未重写。

## 本地交付修复差异（2026-11-12）

来源中的BSP按键实现显式将上下长按门槛设为600ms，OK沿用原门槛；音频输入减为4KiB、独立网络队列12KiB、预缓冲仍8KiB。音频解码器延后到TLS握手与预缓冲结束才分配。下载器恢复最多10次并沿用30秒无进展限制，增加真实生产下载器18秒断网主机用例。共享源码有意差异共13项（130份一致），比前批增加BSP按键和audio_stream_policy.h两项；清单仍为复制原值。

荔枝独立defaults关闭mbedTLS动态缓冲以避免播放中16KiB连续块申请失败，应用栈6144、API worker7168按实测余量收紧；未修改上游依赖源码。main manifest显式锁定复用BSP/音频所需的四个托管组件，避免重配时组件管理器把缓存误判为无用。串口控制仅CONFIG_LIZHI_SERIAL_TEST测试构建存在，默认关闭且打包器拒绝开启该项的产物。

### 2026-11-12 配网收尾

新增主机 `test_lizhi_adapter.c` 与 `tests/adapter_stubs/adapter_platform.h`，直接编译生产adapter以验证换网失败恢复和账号保存异常；仅测试命令生成替身转发头并编译，不影响设备组件。adapter在失败/取消后恢复原驱动网络配置，UI分别说明恢复失败和已连但保存失败。本批来源清单仍130一致/13项已记录差异，原清单不改。

### 正式1.0收尾

项目版本1.0；设置/关于由ESP-IDF描述读取版本，新增esp_app_format显式依赖。打包工具支持正式完整镜像，保留测试/账号/分区校验。原来源130一致/13项有意差异，清单不改；历史dev产物不改写。

## 右上角电量显示（1.0补充）

2026-11-12T17:13:10+08:00 / Ubuntu 开发电脑。按用户要求参考/home/phoenix/code/folotoy-wangyiyun：复用当前CW2017驱动初始化和默认4.2V Li-Poly profile，修复荔枝入口未初始化电量计；共用状态栏将百分比固定在x191/宽35，与Wi-Fi/信号分开，离线也显示百分比，未知为--%。应用每5秒采样并在BSP锁内更新已有标签，不重建页面/打断标题滚动。默认profile不是电芯充放电标定。

原生UI日夜14场景通过，包括100%/0%/未知/无效范围的标签更新；IDF5.5.4构建通过，打包8项通过。设备仅0x10000应用区更新3888352字节，写后Hash校验通过；当前读数99%、4162mV，CW2017版本0x0F，启动/联网正常。本次不擦设置，未重跑播放验收。

最新补充包release/lizhi-1.0-battery-9eb78d5d/；app SHA256 `9eb78d5dd723ee45314e64426c7e192cddbe315ee89692da6f966538eb3215ee`，完整8MiB SHA256 `80f4d566c1a1b560a9968783aea1144097c6461b95738f843ac7f55879e66978`。旧release/lizhi-1.0保留为此前全片烧录产物，不覆盖旧包和历史证据。本次BSP电量驱动与参考源一致，共用UI为有意迁移；原143来源129一致/14有意差异，原清单不改。原始日志.local-backup/battery-{build,flash,startup,source-check,package-tests}.log。
