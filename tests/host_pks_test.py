"""Run the production shell source against host POSIX calls and real executables."""
import os
from pathlib import Path
import pty
import subprocess
import sys
import tempfile
import tty

shell = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(dir=shell.parent) as directory:
    root = Path(directory)
    def run(line, status=0):
        p = subprocess.run([shell, "-c", line], cwd=root, capture_output=True, timeout=5)
        assert p.returncode == status, (line,p.returncode,p.stderr)
        return p
    helper = root / "unregistered"
    helper.write_text("#!/bin/sh\nexec /bin/echo \"$@\"\n")
    helper.chmod(0o755)
    # A fresh name, installed after the shell was built, is found by PATH.
    script = 'export PATH=.:/bin\nunregistered "A B" "" | cat | cat > "out file"\necho C >> "out file"\n'
    p=subprocess.run([shell],input=script.encode(),cwd=root,capture_output=True,timeout=5)
    output = root / "out file"
    actual = output.read_bytes() if output.exists() else None
    assert (p.returncode, p.stdout, actual) == (0, b"", b"A B \nC\n"), (
        p.returncode, p.stdout, p.stderr, actual
    )
    assert run('cat < "out file"').stdout==b"A B \nC\n"
    assert run("./unregistered ok").stdout==b"ok\n"
    words = ["a"*80] + [str(i) for i in range(12)]
    assert run("echo " + " ".join(words) + " | cat").stdout == (" ".join(words)+"\n").encode()
    invalid=subprocess.run([shell],input=b"echo |\nexit\n",capture_output=True,timeout=5)
    assert invalid.returncode==2
    run("not-a-pks-command",127)
    run("/bin/false | /bin/true")
    run("/bin/true | /bin/false",1)
    run("echo no > bad |",2)
    assert not (root/"bad").exists()
    run("echo "+("x"*300),2)
    # Exercise the actual editing loop through a raw pseudo-terminal.
    master,slave=pty.openpty()
    try:
        tty.setraw(slave)
        p=subprocess.Popen([shell],stdin=slave,stdout=subprocess.PIPE,stderr=subprocess.PIPE,cwd=root)
        os.write(master,b"pwd\necho ac\x1b[D\x1b[A\x1b[Bb\x1b[F > draft\nexit\n")
        stdout,stderr=p.communicate(timeout=5)
        assert p.returncode==0 and b"pks$ " in stdout and (root/"draft").read_bytes()==b"abc\n",(stdout,stderr)
    finally:
        os.close(master);os.close(slave)
print("PKS PATH discovery, argv, stdin scripts, pipeline status, redirect and terminal editing PASS")
