#!/usr/bin/env python3
"""生成本地荔枝固件包；只读取构建产物，不读取设备、不烧录、不发布。"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

FLASH_BYTES = 8 * 1024 * 1024
APP_OFFSET = 0x10000
APP_BYTES = 0x700000
SEGMENTS = ((0, "bootloader/bootloader.bin"),
            (0x8000, "partition_table/partition-table.bin"),
            (APP_OFFSET, "FoloToy-Lizhi.bin"))


def package(build, output):
    description = json.loads((build / "project_description.json").read_text())
    version = description.get("project_version", "")
    if description.get("project_name") != "FoloToy-Lizhi" or description.get("target") != "esp32c3":
        raise ValueError("构建不是荔枝 ESP32-C3 工程")
    if not re.fullmatch(r"\d+\.\d+(?:\.\d+)?(?:-dev)?", version):
        raise ValueError("版本号必须为数字主次版本，可含修订号和-dev后缀")
    stable = not version.endswith("-dev")
    config = (build / "sdkconfig").read_text().splitlines()
    if 'CONFIG_LIZHI_SERIAL_TEST=y' in config:
        raise ValueError("拒绝打包启用了串口测试控制的固件")
    for expected in ('CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y', 'CONFIG_LIZHI_ACCOUNT_USER_ID=""'):
        if expected not in config:
            raise ValueError("构建配置不满足候选包要求：" + expected)
    table = (build / SEGMENTS[1][1]).read_bytes()
    partitions = {}
    for pos in range(0, len(table) - 31, 32):
        if table[pos:pos + 2] != b"\xaa\x50":
            break
        _, typ, subtype, offset, size, label, flags = struct.unpack("<HBBII16sI", table[pos:pos + 32])
        name = label.split(b"\0", 1)[0].decode()
        if name in partitions:
            raise ValueError("分区名称重复")
        partitions[name] = (typ, subtype, offset, size, flags)
    expected = {"nvs": (1, 2, 0x9000, 0x6000, 0), "phy_init": (1, 1, 0xf000, 0x1000, 0),
                "factory": (0, 0, APP_OFFSET, APP_BYTES, 0)}
    if partitions != expected:
        raise ValueError("分区布局与已核对设备不一致")
    image = bytearray(b"\xff" * FLASH_BYTES)
    segments = []
    app = None
    for offset, name in SEGMENTS:
        data = (build / name).read_bytes()
        end = {0: 0x8000, 0x8000: 0x9000, APP_OFFSET: APP_OFFSET + APP_BYTES}[offset]
        if not data or offset + len(data) > end:
            raise ValueError("构建产物为空或超过分区边界：" + name)
        image[offset:offset + len(data)] = data
        segments.append(dict(offset=hex(offset), bytes=len(data), sha256=hashlib.sha256(data).hexdigest()))
        if offset == APP_OFFSET:
            app = data
    if len(app) > APP_BYTES - 512 * 1024:
        raise ValueError("应用分区剩余不足 512 KiB")
    if image[0x9000:APP_OFFSET] != b"\xff" * 0x7000:
        raise ValueError("候选全片包含有 NVS 或 PHY 数据")
    full_name = f"FoloToy-Lizhi-{version}-{'full' if stable else 'factory-blank'}-8MiB.bin"
    files = {f"FoloToy-Lizhi-{version}-app.bin": app, full_name: bytes(image)}
    manifest = dict(product="FoloToy-Lizhi", version=version,
                    release_channel="stable" if stable else "development",
                    acceptance="documented_limitations" if stable else "incomplete",
                    known_limitations=["另一真实热点和另一账号切换未完成实机覆盖",
                                       "部分手机热点恢复后需要手动OK重试"],
                    target="esp32c3", flash_bytes=FLASH_BYTES, application_bytes=len(app),
                    application_flash_offset=hex(APP_OFFSET), factory_flash_offset="0x0",
                    factory_image_clears_settings=True, nvs_included=False,
                    embedded_account_id=False, segments=segments,
                    files={name: dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
                           for name, data in files.items()})
    files["manifest.json"] = (json.dumps(manifest, ensure_ascii=False, indent=2) + "\n").encode()
    heading = f"荔枝播放器正式版 {version}；已验证范围和已知限制见交付文档。\n" if stable else "开发候选，完整验收尚未完成，不能当作正式发布版。\n"
    files["使用说明.txt"] = (heading +
        "日常更新只写 app.bin 到 0x10000，保留设备配置。\n"
        f"{full_name} 严格为8MiB，含bootloader、分区表、应用及空白NVS/PHY；写入0x0会清除账号、网络和续听。\n"
        "本工具没有读取设备数据，也没有执行烧录或发布。许可与验收状态以项目文档为准。\n").encode()
    files["SHA256SUMS.txt"] = "".join(hashlib.sha256(data).hexdigest() + "  " + name + "\n"
                                    for name, data in files.items()).encode()
    for name, data in files.items():
        destination = output / name
        if destination.exists() and destination.read_bytes() != data:
            raise FileExistsError("拒绝覆盖不同候选产物：" + str(destination))
    output.mkdir(parents=True, exist_ok=True)
    for name, data in files.items():
        destination = output / name
        destination.write_bytes(data)
        if destination.read_bytes() != data:
            raise ValueError("产物写后校验失败：" + name)
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    print(json.dumps(package(args.build.resolve(), args.output.resolve()), ensure_ascii=False, indent=2))
