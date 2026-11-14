"""Boot an isolated guest, type test19 through the PS/2 keyboard, check serial output."""
import argparse
import pathlib
import socket
import subprocess
import tempfile
import time


def exercise(iso):
    with tempfile.TemporaryDirectory(prefix="kilons-guest-") as temp:
        log = pathlib.Path(temp) / "serial"
        monitor = pathlib.Path(temp) / "monitor"
        args = ["qemu-system-x86_64", "-cdrom", iso, "-m", "256M", "-no-reboot",
                "-display", "none", "-serial", f"file:{log}",
                "-monitor", f"unix:{monitor},server=on,wait=off"]
        with subprocess.Popen(args) as guest:
            def wait_for(marker, start=0):
                deadline = time.monotonic() + 60
                while time.monotonic() < deadline:
                    data = log.read_bytes() if log.exists() else b""
                    index = data.find(marker, start)
                    if index >= 0:
                        return index + len(marker)
                    if guest.poll() is not None or b"kregress: FAIL" in data:
                        break
                    time.sleep(0.1)
                raise RuntimeError(log.read_text(errors="replace") if log.exists() else "No serial log")
            try:
                wait_for(b"pks$ ")
                with socket.socket(socket.AF_UNIX) as console:
                    console.connect(str(monitor))
                    for key in ("t", "e", "s", "t", "1", "9", "ret"):
                        console.sendall(f"sendkey {key} 30\n".encode())
                        time.sleep(0.12)
                    offset = 0
                    for marker in (b"intrcheck: PASS", b"vfork/fexecve", b"posixcheck: PASS", b"kregress: PASS", b"pks$ "):
                        offset = wait_for(marker, offset)
                    print(log.read_text(errors="replace"))
            finally:
                if guest.poll() is None:
                    guest.terminate()
                    try:
                        guest.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        guest.kill()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("iso")
    exercise(parser.parse_args().iso)
