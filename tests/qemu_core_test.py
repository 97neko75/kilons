"""Boot an isolated guest, type test19 through the PS/2 keyboard, check serial output."""
import argparse
import pathlib
import socket
import subprocess
import tempfile
import time


def send_keys(monitor, keys):
    deadline = time.monotonic() + 5
    console = None
    while True:
        try:
            console = socket.socket(socket.AF_UNIX)
            console.connect(str(monitor))
            break
        except OSError:
            if console is not None:
                console.close()
                console = None
            if time.monotonic() >= deadline:
                raise RuntimeError("QEMU monitor did not become available")
            time.sleep(0.05)
    with console:
        console.settimeout(1.0)

        def wait_prompt():
            response = bytearray()
            finish = time.monotonic() + 2
            while time.monotonic() < finish:
                if b"(qemu) " in response:
                    return bytes(response)
                try:
                    chunk = console.recv(4096)
                except socket.timeout:
                    continue
                if not chunk:
                    break
                response.extend(chunk)
            raise RuntimeError("QEMU monitor did not return a prompt: " +
                               bytes(response).decode(errors="replace"))

        wait_prompt()
        for key in keys:
            # Keep one HMP session alive and wait for its prompt.  Closing a
            # socket immediately after sendkey can discard the command on
            # some QEMU builds before the guest input event is queued.
            # Keep the key pressed briefly.  This is the timing used by the
            # previously passing guest test and avoids losing the make/break
            # pair between QEMU's HMP monitor and the emulated PS/2 device.
            console.sendall(f"sendkey {key} 30\n".encode())
            response = wait_prompt()
            if b"Error" in response or b"error" in response:
                raise RuntimeError("QEMU sendkey failed: " +
                                   response.decode(errors="replace"))
            time.sleep(0.12)


def exercise(iso, initramfs=False):
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
                if initramfs:
                    # The top-level default is the full rootfs entry. Move to
                    # the advanced submenu and select its initramfs entry
                    # before Limine's five-second timeout expires.
                    time.sleep(1.0)
                    # Limine keeps the directory selected after expanding it;
                    # move once more to its child before pressing Enter.
                    send_keys(monitor, ("down", "ret", "down", "ret"))
                    offset = 0
                    for marker in (b"started /sbin/init", b"P0 BOOT init process selected", b"pks$ "):
                        offset = wait_for(marker, offset)
                    # The minimal image intentionally does not contain
                    # /bin/test19. This negative probe proves that the
                    # advanced entry was selected instead of merely booting
                    # the default full rootfs and reaching the same prompt.
                    send_keys(monitor, ("t", "e", "s", "t", "1", "9", "ret"))
                    wait_for(b"exec: test19", offset)
                    data = log.read_bytes()
                    if b"kregress: PASS" in data:
                        raise RuntimeError(log.read_text(errors="replace"))
                else:
                    wait_for(b"pks$ ")
                    send_keys(monitor, ("t", "e", "s", "t", "1", "9", "ret"))
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
    parser.add_argument("--initramfs", action="store_true")
    options = parser.parse_args()
    exercise(options.iso, options.initramfs)
