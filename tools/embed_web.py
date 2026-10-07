# PlatformIO pre-build script: gzips web/index.html into src/generated/web_index.h
import gzip
import os

Import("env")  # noqa: F821  (provided by PlatformIO)

root = env.subst("$PROJECT_DIR")  # noqa: F821
src = os.path.join(root, "web", "index.html")
out_dir = os.path.join(root, "src", "generated")
out = os.path.join(out_dir, "web_index.h")

if not os.path.exists(out) or os.path.getmtime(src) > os.path.getmtime(out):
    os.makedirs(out_dir, exist_ok=True)
    with open(src, "rb") as f:
        data = gzip.compress(f.read(), 9, mtime=0)
    lines = []
    for i in range(0, len(data), 20):
        lines.append("  " + ", ".join("0x%02x" % b for b in data[i:i + 20]) + ",")
    with open(out, "w") as f:
        f.write("// Generated from web/index.html by tools/embed_web.py - do not edit\n")
        f.write("#pragma once\n#include <stddef.h>\n#include <stdint.h>\n\n")
        f.write("static const uint8_t WEB_INDEX_GZ[] = {\n%s\n};\n" % "\n".join(lines))
        f.write("static const size_t WEB_INDEX_GZ_LEN = %d;\n" % len(data))
    print("embed_web: %d bytes gzipped" % len(data))
