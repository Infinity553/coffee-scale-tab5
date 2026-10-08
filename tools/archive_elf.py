# PlatformIO post-build script: copies firmware.bin to firmware-archive/coffeescale-tab5.bin
# for the web update, and keeps a copy of every firmware ELF, named by the
# ELF SHA-256 that the firmware reports in its [DIAG] crash lines. With it, crash
# addresses from any earlier build can still be decoded:
#   riscv32-esp-elf-addr2line -pfiaC -e firmware-archive/<sha>.elf <addresses>
import hashlib
import os
import shutil

Import("env")  # noqa: F821  (provided by PlatformIO)


def archive(target, source, env):
    elf = str(target[0])
    with open(elf, "rb") as f:
        sha = hashlib.sha256(f.read()).hexdigest()[:16]
    out_dir = os.path.join(env.subst("$PROJECT_DIR"), "firmware-archive")
    os.makedirs(out_dir, exist_ok=True)
    shutil.copy2(elf, os.path.join(out_dir, sha + ".elf"))
    # keep the 40 most recent builds
    files = sorted((os.path.join(out_dir, f) for f in os.listdir(out_dir) if f.endswith(".elf")),
                   key=os.path.getmtime)
    for old in files[:-40]:
        os.remove(old)
    print("archive_elf: firmware-archive/%s.elf" % sha)


def copy_bin(target, source, env):
    # a fixed place for the web update: the editor's "clean" empties .pio/build
    out_dir = os.path.join(env.subst("$PROJECT_DIR"), "firmware-archive")
    os.makedirs(out_dir, exist_ok=True)
    shutil.copy2(str(target[0]), os.path.join(out_dir, "coffeescale-tab5.bin"))
    print("archive_elf: firmware-archive/coffeescale-tab5.bin (for the web update)")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", archive)  # noqa: F821
env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_bin)  # noqa: F821
