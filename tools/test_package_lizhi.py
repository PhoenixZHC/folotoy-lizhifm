"""仅主机夹具验证候选打包边界，不触及设备或业务数据。"""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("package_lizhi", Path(__file__).with_name("package_lizhi.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PackageTests(unittest.TestCase):
    def setUp(self):
        root = Path(__file__).resolve().parents[1] / ".local-backup"
        root.mkdir(exist_ok=True)
        self.work = Path(tempfile.mkdtemp(prefix="package-lizhi-test-", dir=root))
        self.build, self.output = self.work / "build", self.work / "output"
        (self.build / "bootloader").mkdir(parents=True)
        (self.build / "partition_table").mkdir()
        self.description = dict(project_name="FoloToy-Lizhi", project_version="0.3.0-dev", target="esp32c3")
        self.write_description()
        (self.build / "sdkconfig").write_text('CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y\nCONFIG_LIZHI_ACCOUNT_USER_ID=""\n')
        self.app = b"isolated-unit-test-image"
        (self.build / "FoloToy-Lizhi.bin").write_bytes(self.app)
        (self.build / "bootloader/bootloader.bin").write_bytes(b"test-bootloader")
        table = b"".join(struct.pack("<HBBII16sI", 0x50aa, *row, 0) for row in
                         [(1, 2, 0x9000, 0x6000, b"nvs"), (1, 1, 0xf000, 0x1000, b"phy_init"),
                          (0, 0, 0x10000, 0x700000, b"factory")])
        (self.build / "partition_table/partition-table.bin").write_bytes(table + b"\xff" * (3072 - len(table)))

    def write_description(self):
        (self.build / "project_description.json").write_text(json.dumps(self.description))

    def test_blank_factory_and_preserved_app(self):
        manifest = module.package(self.build, self.output)
        full = next(self.output.glob("*factory-blank-8MiB.bin")).read_bytes()
        self.assertEqual(len(full), 8 * 1024 * 1024)
        self.assertEqual(full[0x9000:0x10000], b"\xff" * 0x7000)
        self.assertEqual(full[0x10000:0x10000 + len(self.app)], self.app)
        self.assertEqual(manifest["acceptance"], "incomplete")
        self.assertTrue(manifest["factory_image_clears_settings"])
        for name, expected in manifest["files"].items():
            self.assertEqual(hashlib.sha256((self.output / name).read_bytes()).hexdigest(), expected["sha256"])
        self.assertEqual(module.package(self.build, self.output), manifest)

    def test_reject_embedded_account(self):
        (self.build / "sdkconfig").write_text('CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y\nCONFIG_LIZHI_ACCOUNT_USER_ID="123"\n')
        with self.assertRaises(ValueError):
            module.package(self.build, self.output)

    def test_reject_wrong_project(self):
        self.description["project_name"] = "FoloToy-Wangyiyun"
        self.write_description()
        with self.assertRaises(ValueError):
            module.package(self.build, self.output)

    def test_reject_serial_test_controls(self):
        with (self.build / "sdkconfig").open('a') as config:
            config.write('CONFIG_LIZHI_SERIAL_TEST=y\n')
        with self.assertRaises(ValueError):
            module.package(self.build, self.output)

    def test_reject_changed_partition(self):
        path = self.build / "partition_table/partition-table.bin"
        data = bytearray(path.read_bytes())
        data[8] ^= 1
        path.write_bytes(data)
        with self.assertRaises(ValueError):
            module.package(self.build, self.output)

    def test_reject_overwrite(self):
        module.package(self.build, self.output)
        (self.build / "FoloToy-Lizhi.bin").write_bytes(self.app + b"changed")
        with self.assertRaises(FileExistsError):
            module.package(self.build, self.output)

    def test_stable_1_0_full_image(self):
        self.description["project_version"] = "1.0"
        self.write_description()
        manifest = module.package(self.build, self.output)
        full = (self.output / "FoloToy-Lizhi-1.0-full-8MiB.bin").read_bytes()
        self.assertEqual(manifest["version"], "1.0")
        self.assertEqual(manifest["release_channel"], "stable")
        self.assertEqual(manifest["acceptance"], "documented_limitations")
        self.assertTrue(manifest["known_limitations"])
        self.assertEqual(len(full), module.FLASH_BYTES)
        self.assertEqual(full[0:15], b"test-bootloader")
        self.assertEqual(full[0x9000:0x10000], b"\xff" * 0x7000)
        self.assertEqual(full[0x10000:0x10000+len(self.app)], self.app)
        self.assertEqual((self.output / "FoloToy-Lizhi-1.0-app.bin").read_bytes(), self.app)

    def test_reject_unsafe_version(self):
        for version in ("../1.0", "1.0/bad", "1.0-rc1", "release"):
            with self.subTest(version=version):
                self.description["project_version"] = version
                self.write_description()
                with self.assertRaises(ValueError):
                    module.package(self.build, self.output)


if __name__ == "__main__":
    unittest.main()
