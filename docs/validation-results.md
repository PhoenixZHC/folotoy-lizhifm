# 开发准备验证记录

> 2026-11-12 / Ubuntu 开发电脑：本文记录复制准备阶段的参考基线验证，表中未实现/未运行不表示正式1.0现状；最新构建、打包与验证见delivery-validation.md。

时间：2026-11-10T11:09:31+08:00（Asia/Shanghai）。来源设备：Ubuntu 开发电脑。范围：代码复制和网易云参考基线，不含荔枝业务或设备验收。

## 结果

| 检查 | 结果 | 证据 |
| --- | --- | --- |
| 复制一致性 | 143 通过，0 失败 | `python3 tools/check_preparation.py`；构建恢复依赖后复核，锁文件亦未改变 |
| 新增 Python 工具语法 | 2 通过，0 失败 | `python3 -m py_compile tools/check_preparation.py tools/run_baseline_tests.py` |
| 主机参考程序 | 14 通过，0 失败 | `tools/run_baseline_tests.py`，含下载并发故障场景及直连客户端参考测试 |
| 打包回归 | 4 通过，0 失败 | 原 `tests/test_package_release.py` 由新测试入口调用 |
| 参考固件编译 | 通过 | 新目录 `build-reference`，ESP-IDF 5.5.4，应用 3,905,680 字节，7 MiB app 分区余 3,434,352 字节 |
| 合并镜像/分段 | 通过 | `verify_firmware.py` 核对 bootloader 0x0、分区表 0x8000、app 0x10000；普通合并镜像 3,971,216 字节 |
| 严格 8 MiB 参考包 | 通过 | `package_release.py` 检查目标/直连配置/分区/空白 NVS 与 PHY 并生成清单；完整包 8,388,608 字节 |
| 源码独立性 | 通过 | 新目录没有指向源工程代码的软链接；没有 `.git`；构建独立恢复托管组件 |
| 荔枝 API / 登录 / 关注 | 未运行 | 当前只有参考基线，没有荔枝实现与真实账号测试条件 |
| 设备测试 | 未运行 | 没有刷机或新设备播放结果 |
| 原仓库完整门禁/字体/UI渲染 | 未运行 | 不把参考仓库政策、字体与页面历史结果算作本项目验收 |

## 本机复现环境

没有更换项目要求的 IDF 版本，也没有安装新的工具。系统没有 `cc`、CMake、Ninja，使用已存在的本机工具。它们位于原工程的忽略目录，仅作为验证机的外部工具链使用；复制后的固件源码不依赖该目录的代码。换电脑须准备等价工具，不能把本机路径当作产品依赖。

### 主机程序实际使用的参数

从项目根目录执行：

```bash
export IDF_PATH=/home/phoenix/.espressif/esp-idf-v5.5.4
export CC=/home/phoenix/code/folotoy-wangyiyun/.local-backup/hotspot-tools/host-cc
export DIRECT_TEST_CFLAGS='-I/home/phoenix/.espressif/esp-idf-v5.5.4/components/mbedtls/mbedtls/include'
export DIRECT_TEST_LDFLAGS='-L/home/phoenix/code/folotoy-wangyiyun/.local-backup/hotspot-tools/host-libs -Wl,-rpath,/home/phoenix/code/folotoy-wangyiyun/.local-backup/hotspot-tools/host-libs'
python3 tools/run_baseline_tests.py
```

`host-cc` 是已有 Zig C 编译器包装。主机产物保留在 `.local-backup/baseline-2htm0ob1/`，不进入生产编译。

### 参考构建实际使用的激活步骤

```bash
export IDF_TOOLS_PATH=/home/phoenix/.espressif
source /home/phoenix/.espressif/python_env/idf5.5_py3.12_env/bin/activate
source /home/phoenix/.espressif/esp-idf-v5.5.4/export.sh
export PATH=/home/phoenix/code/folotoy-wangyiyun/.local-backup/hotspot-tools/cmake/data/bin:/home/phoenix/code/folotoy-wangyiyun/.local-backup/hotspot-tools/bin:$PATH
idf.py --version
cmake --version
ninja --version
```

输出版本：ESP-IDF 5.5.4；Python 3.12.15；CMake 4.4.4；Ninja `1.13.2.git.kitware.jobserver-pipe-1`。固件使用 IDF 安装中的 RISC-V GCC 14.2.0；构建命令见 [构建说明](build-and-validation.md)。

## 遇到并已处理的问题

- 直接从系统 Python 3.14 执行 IDF export，会查找不存在的 `idf5.5_py3.14_env`。改为先激活已确认存在的 3.12 环境，再 export，验证版本正确。
- 最初 `merge-bin -o build-reference/文件名` 错把输出相对路径加了两次构建目录，合并失败。改为 `-o FoloToy-Wangyiyun-full.bin` 后合并与分段验证通过，文档已修正。
- IDF 安装与新项目都没有 Git 元数据，构建输出版本查询警告；未初始化 Git，依赖 `PROJECT_VER=2.1.1` 和锁文件正常编译。组件管理器提示存在新版本，但未升级。
- 构建过程中出现上游配置/组件提示，最终编译退出码为 0；未据此宣称无警告或获得设备验证。

## 参考产物

目录：`release/reference-netease-2.1.1/`，不是荔枝正式交付。

- `FoloMusic-v2.1.1-full-8MB.bin`：8,388,608 字节；SHA-256 `1d22cf50e29736e59daf33be3996438c863ff47c423add94e2f849b971df6c9a`。
- `FoloMusic-v2.1.1-app.bin`：3,905,680 字节；SHA-256 `1be20f040672c562f96af337ff0cc83b1c52de3bdde3c4d0636a2caafd0e198a`。
- 清单和校验文件：同目录 `manifest.json`、`SHA256SUMS.txt`。

本次未访问设备、烧录、读取账号或触发公开发布。源工程已有的未提交文件保留；调查期间出现的其他新增文档也未触碰。
