"""Exercise generated multi-cluster data through the production FAT reader."""

import importlib.util
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("mkfat12xdf", ROOT / "tools/mkfat12xdf.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class DiskImageTest(unittest.TestCase):
    def test_external_fat16_write(self):
        mkfs = shutil.which("mkfs.fat")
        fsck = shutil.which("fsck.fat")
        mcopy = shutil.which("mcopy")
        if not mkfs or not fsck or not mcopy:
            self.skipTest("mkfs.fat, fsck.fat and mtools are optional")
        for image_bytes in (32 * 1024 * 1024, 2 * 1024 * 1024 * 1024):
            with self.subTest(image_bytes=image_bytes), tempfile.TemporaryDirectory() as temp:
                image = Path(temp) / "fat16.img"
                original = Path(temp) / "original.bin"
                fresh = Path(temp) / "fresh.bin"
                replaced = Path(temp) / "replaced.bin"
                streamed = Path(temp) / "streamed.bin"
                with image.open("wb") as stream:
                    stream.truncate(image_bytes)
                original.write_bytes(b"old data" * 1200)
                subprocess.run([mkfs, "-F", "16", str(image)], check=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                subprocess.run([mcopy, "-i", str(image), str(original), "::EXIST.BIN"],
                               check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                subprocess.run([str(ROOT / "tests/build/fat16-image-tool"), str(image)],
                               check=True)
                subprocess.run([fsck, "-n", str(image)], check=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                for name, output in (("FRESH.BIN", fresh), ("EXIST.BIN", replaced),
                                     ("STREAM.BIN", streamed)):
                    subprocess.run([mcopy, "-i", str(image), f"::{name}", str(output)],
                                   check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                self.assertEqual(fresh.read_bytes(), bytes(i % 251 for i in range(7001)))
                self.assertEqual(replaced.read_bytes(),
                                 bytes((i * 7 + 13) % 251 for i in range(12003)))
                self.assertEqual(streamed.read_bytes(), bytes(i % 251 for i in range(70001)))

    def test_multiple_clusters(self):
        # Cluster 682's FAT12 entry straddles a 1024-byte FAT sector boundary.
        content = bytes(i % 251 for i in range(683 * 1024))
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "hello.s9m"
            image = Path(temp) / "data.xdf"
            source.write_bytes(content)
            image.write_bytes(module.create(content, "HELLO.S9M"))
            subprocess.run([str(ROOT / "tests/build/test-fat12"), str(image),
                            str(source)], check=True)

    def test_reject_invalid_input(self):
        with self.assertRaises(ValueError):
            module.create(b"content", "../hello")
        with self.assertRaises(ValueError):
            module.create(b"", "HELLO.S9M")
        with self.assertRaises(ValueError):
            module.create(b"x" * ((module.SECTORS - module.DATA_START) * module.SECTOR + 1),
                          "HELLO.S9M")

    def test_external_fat_tools(self):
        fsck = shutil.which("fsck.fat")
        mcopy = shutil.which("mcopy")
        if not fsck or not mcopy:
            self.skipTest("fsck.fat and mtools are optional")
        content = bytes(i % 251 for i in range(4097))
        with tempfile.TemporaryDirectory() as temp:
            image = Path(temp) / "data.xdf"
            extracted = Path(temp) / "extracted.s9m"
            image.write_bytes(module.create(content, "HELLO.S9M"))
            subprocess.run([fsck, "-n", str(image)], check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            subprocess.run([mcopy, "-i", str(image), "::HELLO.S9M", str(extracted)],
                           check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertEqual(extracted.read_bytes(), content)


if __name__ == "__main__":
    unittest.main()
