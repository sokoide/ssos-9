"""Exercise the H2 cleanup with a fake MonitorSocket."""
import socket
import subprocess
import tempfile
import threading
import unittest
from pathlib import Path

RUNNER = Path(__file__).resolve().parents[1] / "tools/mpx68k-h2.sh"


class MonitorCleanupTest(unittest.TestCase):
    def run_case(self, replies, body, expected_status):
        with tempfile.TemporaryDirectory(dir="/tmp") as directory:
            path = str(Path(directory) / "monitor.sock")
            commands = []
            errors = []
            with socket.socket(socket.AF_UNIX) as server:
                server.bind(path)
                server.listen()
                server.settimeout(5)

                def serve():
                    try:
                        for reply in replies:
                            conn, _ = server.accept()
                            with conn, conn.makefile("rb") as stream:
                                conn.settimeout(5)
                                commands.append(stream.readline().decode().strip())
                                if reply is not None:
                                    conn.sendall(reply)
                    except OSError as error:
                        errors.append(error)

                thread = threading.Thread(target=serve)
                thread.start()
                try:
                    script = """
source "$1"
MONITOR_SOCK=$2
IMAGE=/test.xdf
trap 'finish $?' EXIT
""" + body
                    run = subprocess.run(
                        ["zsh", "-c", script, "h2-test", str(RUNNER), path],
                        text=True, capture_output=True, timeout=12,
                    )
                finally:
                    thread.join()
            self.assertFalse(errors)
            self.assertEqual(run.returncode, expected_status, run.stdout + run.stderr)
            return commands, run

    def test_mount_rejected_and_ejected_on_exit(self):
        commands, _ = self.run_case(
            [b"OK\n", b"ERR mock mount\n", b"OK\n", b"OK\n", b"OK\n", b"OK\n"],
            'mount_floppy || environment_fail "mount rejected"',
            3,
        )
        self.assertEqual(commands, [
            "PAUSE", "MOUNTFDD 1 /test.xdf", "RESUME", "PAUSE", "EJECTFDD 1", "RESUME",
        ])

    def test_lost_eject_reply_retries_cleanup(self):
        commands, _ = self.run_case(
            [b"OK\n", None, b"OK\n", b"OK\n", b"OK\n", b"OK\n"],
            'mounted=1\neject_floppy || environment_fail "eject reply lost"',
            3,
        )
        self.assertEqual(commands, ["PAUSE", "EJECTFDD 1", "RESUME"] * 2)

    def test_resume_resent_even_after_successful_eject(self):
        commands, run = self.run_case(
            [b"OK\n", b"OK\n", b"ERR mock resume\n", b"OK\n"],
            'mounted=1\neject_floppy || environment_fail "resume rejected"',
            3,
        )
        self.assertEqual(commands, ["PAUSE", "EJECTFDD 1", "RESUME", "RESUME"])
        self.assertNotIn("resume MPX68K manually", run.stderr)

    def test_unrecoverable_resume_fails_run(self):
        commands, run = self.run_case(
            [b"OK\n", b"OK\n", None, None],
            'mounted=1\neject_floppy || environment_fail "resume reply lost"',
            1,
        )
        self.assertEqual(commands, ["PAUSE", "EJECTFDD 1", "RESUME", "RESUME"])
        self.assertIn("RESUME failed", run.stderr)


if __name__ == "__main__":
    unittest.main()
