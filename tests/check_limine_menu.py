"""Keep Limine's default and advanced boot paths aligned with the ISO."""
from pathlib import Path


def main():
    config = Path("boot/limine/limine.conf").read_text(encoding="utf-8")
    makefile = Path("Makefile").read_text(encoding="utf-8")
    version = Path("VERSION").read_text(encoding="utf-8").strip()
    required = (
        "timeout: 5",
        "default_entry: 1",
        f"interface_branding: kilons {version}",
        f"/kilons {version}",
        "protocol: limine",
        "path: boot():/boot/kilons/kernel.elf",
        "module_path: boot():/boot/kilons/rootfs.kilofs",
        "module_path: boot():/boot/kilons/initramfs.img",
        "module_string: rootfs",
        f"/Advanced options for kilons {version}",
        f"//kilons {version} (initramfs)",
    )
    for item in required:
        assert item in config, item
    assert "ISO_LIMINE_DIR := $(ISO_DIR)/boot/limine" in makefile
    assert "cp boot/limine/limine.conf $(ISO_LIMINE_DIR)/limine.conf" in makefile
    assert "limine bios-install $(BUILD)/kilons.iso" in makefile
    assert "xorriso -as mkisofs" in makefile
    qemu_test = Path("tests/qemu_core_test.py").read_text(encoding="utf-8")
    assert 'send_keys(monitor, ("down", "ret", "down", "ret"))' in qemu_test
    assert 'sendkey {key} 30\\n' in qemu_test
    assert "grub" not in makefile.lower()
    assert not Path("boot/grub/grub.cfg").exists()
    print("Limine default/advanced menu and ISO paths PASS")


if __name__ == "__main__":
    main()
