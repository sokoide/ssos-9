"""Tests against the real cross-linked sample and mutated ELF metadata."""

import importlib.util
import struct
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("mkmodule", ROOT / "tools/mkmodule.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class PackerTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.elf = (ROOT / "examples/hello/build/hello.elf").read_bytes()
        cls.shoff = struct.unpack_from(">I", cls.elf, 32)[0]
        cls.shnum = struct.unpack_from(">H", cls.elf, 48)[0]
        cls.sections = [struct.unpack_from(">10I", cls.elf, cls.shoff + 40 * i)
                        for i in range(cls.shnum)]
        cls.rela = next(section for section in cls.sections if section[1] == 4)

    def test_image_crc_and_code(self):
        image = module.pack(self.elf, "hello")
        self.assertEqual(image[:4], b"S9M1")
        crc = struct.unpack_from(">I", image, 0x34)[0]
        image[0x34:0x38] = b"\0" * 4
        self.assertEqual(zlib.crc32(image), crc)
        self.assertEqual(struct.unpack_from(">I", image, 0xc)[0], len(image))

    def test_truncated_and_bad_name(self):
        for bad in (self.elf[:100], self.elf[:-1]):
            with self.assertRaises(ValueError):
                module.pack(bad, "hello")
        with self.assertRaises(ValueError):
            module.pack(self.elf, "../bad")

    def test_stack_size_bounds(self):
        for size in (4096, 32768, 65532):
            image = module.pack(self.elf, "hello", size)
            self.assertEqual(struct.unpack_from(">I", image, 0x28)[0], size)
        for size in (0, 4095, 4097, 65536):
            with self.assertRaises(ValueError):
                module.pack(self.elf, "hello", size)

    def test_unknown_relocation_and_displacement(self):
        bad = bytearray(self.elf)
        bad[self.rela[4] + 7] = 1
        with self.assertRaises(ValueError):
            module.pack(bad, "hello")
        bad = bytearray(self.elf)
        text = next(section for section in self.sections if section[1] == 1 and section[2] == 6)
        address = struct.unpack_from(">I", bad, self.rela[4])[0]
        bad[text[4] + address] ^= 1
        with self.assertRaises(ValueError):
            module.pack(bad, "hello")

    def test_allocated_data_rejected(self):
        bad = bytearray(self.elf)
        data_index = next(i for i, section in enumerate(self.sections)
                          if section[1] == 1 and section[2] == 3)
        struct.pack_into(">I", bad, self.shoff + data_index * 40 + 20, 4)
        with self.assertRaises(ValueError):
            module.pack(bad, "hello")

    def test_missing_or_empty_relocations_rejected(self):
        rela_index = self.sections.index(self.rela)
        for field, value in ((4, 0), (20, 0)):
            bad = bytearray(self.elf)
            struct.pack_into(">I", bad, self.shoff + rela_index * 40 + field, value)
            with self.assertRaises(ValueError):
                module.pack(bad, "hello")


if __name__ == "__main__":
    unittest.main()
