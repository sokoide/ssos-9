#!/usr/bin/env python3
"""Host regression tests for XEiJ's control FIFO and lock."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest


TOOL = Path(__file__).resolve().parents[1] / "tools" / "xeij-transport.py"


class TransportTest(unittest.TestCase):
    @unittest.skipUnless(shutil.which("zsh"), "runner shell is unavailable")
    def test_checked_command_rejects_corrupt_guest_echo_before_enter(self):
        script = TOOL.with_name("emulator-itest.sh")
        shell = """
source "$1"
type_text() { :; }
guest_echo=$2
snap() {
    s=${guest_echo%%$'\\r'*}
    guest_echo=${guest_echo#*$'\\r'}
}
key() { print -r -- "$1"; }
type_checked_cmd 'wait 2'
"""
        for echo, code, output in (
            ("> wait 2     ", 0, "return\n"),
            ("> wait\r> wait 2", 0, "return\n"),
            ("> wait 2\n> fwafitr e2", 3, ""),
        ):
            with self.subTest(echo=echo):
                result = subprocess.run(
                    ["zsh", "-c", shell, "fixture", script, echo],
                    capture_output=True, text=True, timeout=5,
                )
                self.assertEqual(result.returncode, code, result.stderr)
                self.assertEqual(result.stdout, output)

    def invoke(self, *args):
        return subprocess.run(
            [sys.executable, TOOL, *args], capture_output=True, timeout=5
        )

    def test_fifo_delivery_and_missing_reader(self):
        with tempfile.TemporaryDirectory() as directory:
            fifo = Path(directory) / "control"
            os.mkfifo(fifo)
            result = self.invoke("send", fifo, "typekey a", "0.1")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"control FIFO unavailable", result.stderr)
            reader = os.open(fifo, os.O_RDONLY | os.O_NONBLOCK)
            try:
                result = self.invoke("send", fifo, "typekey a", "0.1")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(os.read(reader, 128), b"typekey a\n")
            finally:
                os.close(reader)

    def test_key_batch_preserves_order_and_pacing(self):
        with tempfile.TemporaryDirectory() as directory:
            fifo = Path(directory) / "control"
            os.mkfifo(fifo)
            reader = os.open(fifo, os.O_RDONLY | os.O_NONBLOCK)
            try:
                started = time.monotonic()
                result = self.invoke("keys", fifo, "a", "colon", "return")
                elapsed = time.monotonic() - started
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(os.read(reader, 128), b"typekey a\ntypekey colon\ntypekey return\n")
                self.assertGreaterEqual(elapsed, 0.3)
            finally:
                os.close(reader)

    def test_regular_file_is_untouched(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "control"
            path.write_bytes(b"original")
            self.assertNotEqual(self.invoke("send", path, "reset", "0.1").returncode, 0)
            self.assertEqual(path.read_bytes(), b"original")

    def test_lock_survives_exec_and_releases(self):
        with tempfile.TemporaryDirectory() as directory:
            lock = Path(directory) / "lock"
            command = [sys.executable, TOOL, "lock", lock]
            with subprocess.Popen(
                [*command, sys.executable, "-u", "-c", "print('ready'); input()"],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
            ) as owner:
                self.assertEqual(owner.stdout.readline(), b"ready\n")
                self.assertNotEqual(self.invoke("lock", lock, "/usr/bin/true").returncode, 0)
                owner.communicate(b"\n", timeout=5)
            self.assertEqual(self.invoke("lock", lock, "/usr/bin/true").returncode, 0)

    def test_child_timeout(self):
        result = self.invoke("run", "0.1", sys.executable, "-c", "import time; time.sleep(20)")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"timed out", result.stderr)


    @unittest.skipUnless(shutil.which("zsh"), "runner shell is unavailable")
    def test_arena_sample_must_follow_current_reap(self):
        script = TOOL.with_name("emulator-itest.sh")
        shell = 'source "$1"; arena_after "$2" "PID 3 exited with status "'
        old = "arena free_bytes=100 largest_free=50\nPID 3 exited with status 0"
        fresh = "arena free_bytes=100 largest_free=50"
        for screen, expected in ((old, ""), (old + "\n" + fresh, fresh + "\n")):
            result = subprocess.run(
                ["zsh", "-c", shell, "fixture", script, screen], check=True,
                capture_output=True, text=True, timeout=5,
            )
            self.assertEqual(result.stdout, expected)

    @unittest.skipUnless(shutil.which("zsh"), "runner shell is unavailable")
    def test_shell_releases_pressed_key_on_term(self):
        with tempfile.TemporaryDirectory() as directory:
            fifo = Path(directory) / "control"
            os.mkfifo(fifo)
            reader = os.open(fifo, os.O_RDONLY | os.O_NONBLOCK)
            commands = """
source "$1"
PIPE=$2
ini_backup=fixture
restore_xeij_ini() {
    python3 -c 'import signal; print(signal.getsignal(signal.SIGTERM))'
}
trap 'finish_case $?' EXIT
trap 'exit 143' TERM
send_control 'presskey ctrl'
print ready
while true; do sleep 0.05; done
"""
            try:
                with subprocess.Popen(
                    ["zsh", "-c", commands, "fixture", TOOL.with_name("emulator-itest.sh"), fifo],
                    stdout=subprocess.PIPE,
                ) as owner:
                    self.assertEqual(owner.stdout.readline(), b"ready\n")
                    owner.terminate()
                    self.assertEqual(owner.wait(timeout=5), 143)
                    self.assertEqual(owner.stdout.read(), b"0\n")
                self.assertEqual(os.read(reader, 128), b"presskey ctrl\nreleasekey ctrl\n")
            finally:
                os.close(reader)


class ProfileTest(unittest.TestCase):
    def test_media_and_host_files_are_isolated(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            source_ini = root / "source.ini"
            image = root / "disk with spaces.HDS"
            export = root / "export"
            profile = root / "profile"
            export.mkdir()
            image.write_bytes(b"original disk")
            for name in ("ssos9.x", "ssos9.xdf"):
                (export / name).write_text(name)
            original = f"si0={image}:R\nhf0=/original\nhf1=/second\nsaveonexit=on\n"
            source_ini.write_text(original)
            result = subprocess.run(
                [sys.executable, TOOL.with_name("xeij-test-profile.py"), source_ini, export, profile],
                capture_output=True, timeout=5,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            config = (profile / "XEiJ.ini").read_text()
            clone = profile / "media" / "si0.HDS"
            self.assertIn(f"si0={clone}:R\n", config)
            self.assertIn(f"hf0={profile / 'hfs'}\n", config)
            self.assertNotIn("hf1=", config)
            self.assertIn("saveonexit=off\n", config)
            self.assertEqual((profile / "hfs" / "ssos9.xdf").read_text(), "ssos9.xdf")
            clone.write_bytes(b"changed")
            self.assertEqual(image.read_bytes(), b"original disk")
            self.assertEqual(source_ini.read_text(), original)
            ini = profile / "XEiJ.ini"
            ini.write_text(config.replace(str(clone), str(image)))
            result = subprocess.run(
                [sys.executable, TOOL.with_name("xeij-suite.py"), "--require-profile", "d1"],
                capture_output=True, text=True, timeout=5,
                env={**os.environ, "SSOS9_XEIJ_INI": str(ini),
                     "SSOS9_XEIJ_STAGE": str(profile / "hfs")},
            )
            self.assertEqual(result.returncode, 2)
            self.assertIn("ini media or HFS shares differ", result.stderr)


class SuiteTest(unittest.TestCase):
    def test_environment_failure_records_remaining_case_as_not_run(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            suite = root / "xeij-suite.py"
            shutil.copy2(TOOL.with_name(suite.name), suite)
            shutil.copy2(TOOL, root / TOOL.name)
            runner = root / "emulator-itest.sh"
            runner.write_text("#!/bin/sh\necho case=\"$1\"\nexit 3\n")
            runner.chmod(0o700)
            results = root / "results"
            result = subprocess.run(
                [sys.executable, suite, "--results", results, "c1", "c2"],
                capture_output=True, timeout=5, env={**os.environ, "HOME": str(root)},
            )
            self.assertEqual(result.returncode, 3, result.stderr)
            (run_dir,) = results.iterdir()
            report = json.loads((run_dir / "result.json").read_text())
            self.assertEqual(
                [(case["case"], case["status"]) for case in report["cases"]],
                [("c1", "environment_error"), ("c2", "not_run")],
            )
            self.assertEqual((run_dir / "01-c1.log").read_text(), "case=c1\n")


if __name__ == "__main__":
    unittest.main()
