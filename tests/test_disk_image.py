"""Exercise the generated FAT12 floppy images through the production FAT reader."""

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
