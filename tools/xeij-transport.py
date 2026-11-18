#!/usr/bin/env python3
"""Bounded host-side transport for the XEiJ integration runner."""

import errno
import fcntl
import os
import stat
import subprocess
import sys
import time


def send(path, command, timeout):
    deadline = time.monotonic() + timeout
    data = (command + "\n").encode("ascii") if command is not None else b""
    while True:
        try:
            fd = os.open(path, os.O_WRONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
            try:
                if not stat.S_ISFIFO(os.fstat(fd).st_mode):
                    raise ValueError("control path is not a FIFO")
                if not data:
                    return
                if len(data) > os.fpathconf(fd, "PC_PIPE_BUF"):
                    raise ValueError("control command exceeds atomic write size")
                if os.write(fd, data) != len(data):
                    raise OSError("incomplete FIFO write")
                return
            finally:
                os.close(fd)
        except OSError as error:
            if error.errno not in (errno.ENXIO, errno.EAGAIN, errno.ENOENT):
                raise
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"control FIFO unavailable: {path}") from error
            time.sleep(min(0.05, remaining))



def send_logged(path, command, timeout):
    started = time.monotonic()
    try:
        send(path, command, timeout)
    finally:
        if log := os.environ.get("SSOS9_XEIJ_COMMAND_LOG"):
            with open(log, "a", encoding="ascii") as output:
                print(f"{started:.6f} {command}", file=output)

def main(args):
    operation, *parameters = args
    if operation == "send":
        path, command, timeout = parameters
        send_logged(path, command, float(timeout))
    elif operation == "probe":
        path, timeout = parameters
        send(path, None, float(timeout))
    elif operation == "keys":
        path, *keys = parameters
        for key in keys:
            send_logged(path, f"typekey {key}", 2)
            time.sleep(0.1)
    elif operation == "lock":
        path, *command = parameters
        fd = os.open(path, os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RuntimeError("another XEiJ integration case is running") from error
        os.set_inheritable(fd, True)
        os.execvp(command[0], command)
    elif operation == "monotonic":
        print(f"{time.monotonic():.6f}")
    elif operation == "launch":
        log, *command = parameters
        with open(log, "ab") as output:
            subprocess.Popen(
                command, stdin=subprocess.DEVNULL, stdout=output,
                stderr=subprocess.STDOUT, start_new_session=True,
            )
    elif operation == "run":
        timeout, *command = parameters
        return subprocess.run(command, timeout=float(timeout), check=False).returncode
    else:
        raise ValueError(f"unknown operation: {operation}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"XEiJ transport: {error}", file=sys.stderr)
        sys.exit(3)
