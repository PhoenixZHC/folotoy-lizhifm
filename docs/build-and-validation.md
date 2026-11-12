# Ubuntu 构建与验证

## 正式1.0构建与完整打包

在Ubuntu开发电脑，确认ESP-IDF5.5.4安装及IDF_TOOLS_PATH后加载环境，进入`firmware/lizhi-player`：

```bash
export IDF_TOOLS_PATH=/home/phoenix/.espressif
source /home/phoenix/.espressif/python_env/idf5.5_py3.12_env/bin/activate
source /home/phoenix/.espressif/esp-idf-v5.5.4/export.sh
export PATH=/home/phoenix/code/folotoy-wangyiyun/.local-backup/hotspot-tools/cmake/data/bin:/home/phoenix/code/folotoy-wangyiyun/.local-backup/hotspot-tools/bin:$PATH
idf.py --version
idf.py -B build -D SDKCONFIG=build/sdkconfig build
```

回到项目根目录打包，不连接或写入设备：

```bash
python3 tools/package_lizhi.py firmware/lizhi-player/build release/lizhi-1.0
python3 -m unittest tools/test_package_lizhi.py
```

正式包生成`FoloToy-Lizhi-1.0-full-8MiB.bin`（0x0，清除配置）、`FoloToy-Lizhi-1.0-app.bin`（0x10000，保留配置），以及manifest/哈希。打包器支持正式数字版本和旧-dev版本，拒绝测试开关/嵌入账号/错误分区及覆盖不同二进制。版本号1.0、release_channel=stable；acceptance=documented_limitations保留未实测边界。正式1.0已在用户追加授权后全片擦写清空设置，独立8MiB校验与首次启动通过；构建命令本身不会刷机。

> 2026-11-12 / Ubuntu 开发电脑：正式1.0使用firmware/lizhi-player和下文最新正式打包命令。历史网易云基线、公开样本及未提供账号/配网描述仅适用于早期阶段，不代表当前功能。

当前荔枝交付入口为`firmware/lizhi-player`，按下文“荔枝公开节目试用入口”命令构建；前面的网易云步骤仅保留历史基线含义。2026-11-12的最新交付验证见[交付记录](delivery-validation.md)。正常交付必须关闭`CONFIG_LIZHI_SERIAL_TEST`，`tools/package_lizhi.py`会拒绝开启测试控制的固件。荔枝main manifest显式锁定BSP与Helix依赖；不依赖旧缓存碰巧存在。

主机入口`tools/run_lizhi_tests.py`另包含72项真实adapter分支检查：网络失败/取消/配置读取失败/恢复失败/保存失败及账号保存/解绑。Wi-Fi、NVS与RTOS替身仅由该命令编译；转发头在忽略的临时产物目录生成，不在设备构建使用。

## 历史：准备阶段产物含义

源码保持 Folo Music 2.1.1 入口。以下构建只证明复制后的参考基线能编译，产物仍叫 `FoloToy-Wangyiyun`，运行的是网易云逻辑；不是荔枝客户端。

当前结果见 [验证记录](validation-results.md)。不要照搬源项目历史通过数量为本次结果。

## 环境

- ESP-IDF 5.5.4，ESP32-C3，8 MiB，无 PSRAM。
- 依赖版本由 `firmware/ai-passport/dependencies.lock` 锁定，不随意升级 LVGL/codec。
- 系统 Python 仅用于不依赖 IDF 的检查；IDF 编译使用该安装自带的 Python 环境。
- 主机 C 测试还需要 C11 编译器；网易云参考协议测试需要 mbedTLS 开发头和 `libmbedcrypto`。
- 新目录不复制托管依赖；首次编译需能恢复已锁定组件。网络不可用时明确报告，不用指向源工程目录的软链接伪装独立构建成功。

本机确认存在的安装（仅适用于 Ubuntu 开发电脑）：

```text
ESP-IDF: /home/phoenix/.espressif/esp-idf-v5.5.4
IDF_TOOLS_PATH: /home/phoenix/.espressif
IDF Python: /home/phoenix/.espressif/python_env/idf5.5_py3.12_env
```

## 复制完整性

```bash
cd /home/phoenix/code/folotoy-lizhifm
python3 tools/check_preparation.py
```

## 主机基线测试

先确认依赖存在，不自动安装：

```bash
export IDF_TOOLS_PATH=/home/phoenix/.espressif
source /home/phoenix/.espressif/python_env/idf5.5_py3.12_env/bin/activate
source /home/phoenix/.espressif/esp-idf-v5.5.4/export.sh
idf.py --version
command -v cc
cd /home/phoenix/code/folotoy-lizhifm
python tools/run_baseline_tests.py
```

无系统 `cc` 时，可用 `CC=/absolute/path/to/compiler-wrapper` 指定已确认可用的替代编译器。额外主机参数由 `BASELINE_CFLAGS` 传入；原直连测试脚本接受 `DIRECT_TEST_CFLAGS`、`DIRECT_TEST_LDFLAGS`。具体本机替代环境见验证记录，不让新工程运行期依赖旧工程工具目录。

测试在 `.local-backup/` 保留产物，不删除任何文件，不启动硬件或业务账号请求。复制来的测试夹具只用于主机测试，不能编入生产入口。

## 参考固件构建

必须使用新目录自己的构建目录与 sdkconfig，不能复用原工程 build 缓存。确认工具链版本后执行：

```bash
export IDF_TOOLS_PATH=/home/phoenix/.espressif
source /home/phoenix/.espressif/python_env/idf5.5_py3.12_env/bin/activate
source /home/phoenix/.espressif/esp-idf-v5.5.4/export.sh
idf.py --version
cd /home/phoenix/code/folotoy-lizhifm/firmware/ai-passport
idf.py -B build-reference -D SDKCONFIG=build-reference/sdkconfig \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.direct.defaults' build
idf.py -B build-reference merge-bin -o FoloToy-Wangyiyun-full.bin
python tools/verify_firmware.py build-reference
```

新工程暂不提供一键刷机或发布。构建产物只留在忽略目录；设备刷写需另行授权。

注意：`merge-bin -o` 的相对输出路径相对于构建目录，不能再次添加 `build-reference/` 前缀；也可传绝对路径。

本机系统未提供 `cc`、CMake 和 Ninja；本次使用已有工具完成验证，精确设置见验证记录。上述通用命令要求先补齐或在 PATH 中提供 CMake/Ninja。系统 Python 是 3.14，必须先激活已有 3.12 环境；否则 IDF 会查找不存在的 `idf5.5_py3.14_env`，这不是源码错误。

生成严格 8 MiB 参考包时可单独运行：

```bash
python tools/package_release.py build-reference ../../release/reference-netease-2.1.1
```

此包名称和 UI 仍是网易云，禁止当荔枝正式包分发。完整 0x0 烧录会重置设备设置；即使是 app-only，也必须先确认分区匹配与设备授权。

## 荔枝实现后的增量验证

新增解析、分页/过期结果、会话隔离、媒体 URL、进度保存和 seek 行为测试，再运行受影响的现有回归。协议解析使用真实脱敏响应，真实账号请求与离线测试分开执行。动态验证不能由 Mock 成功代替。

构建通过、主机测试通过、荔枝接口通过、设备验收通过必须分别记录，任一未执行即写“未运行”。

## 荔枝单集详情解析验证

在项目根目录执行，先提供已确认的主机编译器及 ESP-IDF 5.5.4 路径：

```bash
CC=/absolute/path/to/compiler IDF_PATH=/absolute/path/to/esp-idf-v5.5.4 \
  python3 tools/run_lizhi_tests.py
```

当前 Ubuntu 开发电脑可使用验证记录中已有的 `host-cc` 包装器；不自动安装依赖。详情 39 项、列表 10 项离线检查使用真实响应的公开字段投影及隔离的异常输入，不联网、不读设备、不写 NVS。可另传一个本机详情响应文件的绝对路径作额外验证；或用 `--page-files` 依次传入本样本每页 2 条的第 1、2、227、228 页本机文件，增加 4 项真实分页验证。工具只显示通过数量，不打印响应或解析字段；不要将带 token 的响应提交进仓库。

同一入口现还编译 43 项 HTTP/媒体边界测试，覆盖生产 `lizhi_http.c`；仅在主机编译时使用 `tests/lizhi_stubs/`，不将 stub 编入设备工程。43 项测试不联网，不能代替真实设备结果。

新增组件后首次构建应先运行 `idf.py -B build-reference reconfigure`，让 CMake 发现新目录，再按前述配置构建。当前验证了解析和公开 HTTP 组件可编译，不表示荔枝应用或设备播放可用。详情见 `implementation-progress.md`。

## 独立设备网络探测构建

激活同一 ESP-IDF 5.5.4/Python3.12/CMake/Ninja 环境后，进入 `tools/lizhi_device_probe` 执行 `idf.py -B build -D SDKCONFIG=build/sdkconfig build`。当前输出为诊断应用，构建不刷机；功能、NVS 只读配置和设备操作边界见该目录 README。不要照抄 IDF 构建末尾的全量烧录命令。

诊断工程和参考工程的分区表字节已核对一致；实际设备仍须单独核对。没有设备、烧录授权或已验证的设备配网时，记录硬件验收未运行，不宣称联网/内存/音频通过。

## 历史：荔枝公开节目试用入口

与网易云参考入口分开构建；复用本项目BSP、14像素中文字库和音频组件，不链接网易云API/账号。完成前述本机IDF激活后：

```bash
cd /home/phoenix/code/folotoy-lizhifm/firmware/lizhi-player
idf.py --version
idf.py -B build -D SDKCONFIG=build/sdkconfig build
```

产物 `build/FoloToy-Lizhi.bin`，分区与诊断相同。需已有Wi-Fi NVS配置，当前只读 `netease_wifi` 的配网键，不读网易云账号。配置中不得写入密码；没有配网时明确报错，尚未提供新配网界面。

首页确定进入博主节目列表；上下选择节目，在列表边缘自动翻页，确定播放；长按确定取消加载或返回。播放中确定暂停/继续，上下切集；长按确定打开停止返回、音量、快退15秒、快进30秒和从头播放菜单。返回列表会停止音频，API和音频TLS暂不并发。新增定位和续听待实机验收，账号关注尚未提供。

主机检查 `tools/run_lizhi_tests.py` 已加入纯分页状态机检查；此命令仍不联网或写数据库。实机试用需用户提供屏幕及三键反馈，不把编译或初始化日志当完整界面验收。

界面复用网易云项目已复制的 `device_music_ui`、各字号字库和 `display_policy`，纸色日夜主题、长按导航、60秒熄屏和首键只唤醒一致。压缩中文字库必须启用 `CONFIG_LV_USE_FONT_COMPRESSED=y`；荔枝入口有编译期检查，防止只显示色块不显示文字。

## 帧锚点定位与最近收听

解码器按真实Layer III帧边界记录最近64个约1秒间隔的锚点，采样时钟保留小数余数，避免逐帧毫秒取整导致长时漂移。快退15秒和快进30秒从已知帧重新建立下载/解码，至少从目标前约1秒预热，不使用文件长度比例猜测VBR位置。锚点至目标最多65秒；无锚点或没有强ETag/Last-Modified及总长度时明确拒绝定位。

Range恢复严格核对206、Content-Range、长度及资源验证器；服务器忽略Range、缺失或改变验证器时失败，不静默重播。被验证器确认不变时可以在同次播放中复用现有媒体URL；重启续听按节目ID重新解析URL再验证资源，不保存URL。

`lizhi_progress` 独立NVS命名空间保存最近一条本地收听记录（版本、local作用域、博主/节目ID、标题、已输出时间、预热帧、资源验证器、CRC）。每60秒及暂停/停止时合并保存，失败显示错误。还没有设备账号，local记录不能冒充账号缓存；未来账号作用域需要独立接入。首页出现第二条“继续”入口后由用户主动恢复，不自动发声；重启恢复和异常存储实机验收尚未完成。

主机 `tools/run_lizhi_tests.py` 增加24项帧/计时/锚点检查和13项记录校验。下载器额外的范围恢复检查：

```bash
CC=/home/phoenix/code/folotoy-wangyiyun/.local-backup/hotspot-tools/host-cc \
AUDIO_DOWNLOAD_TEST_OUT=/home/phoenix/code/folotoy-lizhifm/.local-backup/test-audio-download-position \
bash firmware/ai-passport/tools/run_audio_download_tests.sh
```

真实MP3片段验证（需已有Helix依赖及用户提供的片段，不自动下载）：

```bash
CC=/home/phoenix/code/folotoy-wangyiyun/.local-backup/hotspot-tools/host-cc \
python3 tools/run_lizhi_seek_sample.py .local-backup/lizhi-probes/seek-head.mp3
```

主机专用等价定点运算头只替代不可在x86运行的汇编，不修改第三方依赖，不编入正式固件。验证中间帧起播的reservoir预热、消费字节边界与连续解码目标PCM一致；短片段结果不替代实机跳转听感、定位误差和长时验收。

## 当前关注入口

当前实现与绑定限制以 [关注同步](follow-sync.md) 为准。此前“关注接口尚未找到”与“定位未实测”段落是历史阶段：后续已取得当前用户真实关注及独立定位诊断证据，仍不能推断设备内完整登录流程或长时验收完成。

`CONFIG_LIZHI_ACCOUNT_USER_ID` 默认空，开发绑定只写本机忽略的build/sdkconfig，不把用户ID写进源码/defaults。配置后首页进入“我的关注”，上下在页边缘翻页，确定进入所选博主节目，长按返回上级。读取失败保留当前页并提示。主机测试仍用 `tools/run_lizhi_tests.py`，最新237通过、0失败。

只读关注诊断：在忽略目录准备仅含用户ID的defaults，给 `tools/lizhi_device_probe` 使用独立SDKCONFIG（不要复用定位/试听配置），设置 `CONFIG_LIZHI_PROBE_FOLLOW_OWNER`。此模式优先于音频诊断，测试完整关注及所有博主第一页，不写NVS、不播放；实际刷写依旧遵守设备授权。

### 四行列表更新

荔枝关注和节目列表现在一屏4条，后台仍每次请求2条并串行合并，不扩大响应上限。`lizhi_list_loader` 是正式入口与只读诊断共用的加载层；主机测试新增多请求失败/取消/末页场景，当前308通过/0失败。关注诊断的“page/count”现在指四条显示视图，14位账号对应4屏（4/4/4/2）；历史8页记录指两条网络分页。

## 0.3.0-dev 账号与设置候选

`tools/run_lizhi_tests.py` 已新增账号解析/HTTPS、设置、关注快照、账号范围续听及隔离NVS故障测试；stub仅由主机测试编译。最新3748通过/0失败（缓存截断按每一长度计项）；基线14个程序及4项打包回归通过。新配网、自然切集、60分钟和弱网不能由这些结果替代。

本机候选打包（不烧录、不发布）：

```bash
python3 tools/test_package_lizhi.py
python3 tools/package_lizhi.py firmware/lizhi-player/build .local-backup/lizhi-0.3.0-dev-99dd9a33
```

新打包5项通过，拒绝错误工程/分区、写死账号ID及覆盖不同候选；生成严格8MiB空白工厂镜像、独立app与manifest/哈希，acceptance明确为incomplete。当前设备只写0x10000的app，保留配置，未写全片。完整记录见 `docs/account-and-settings.md` 和 `docs/implementation-progress.md`。
