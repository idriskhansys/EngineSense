#!/usr/bin/env python3
"""REQ-ARCH-001: the portable core must not depend on any UI / platform framework."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FORBIDDEN_INCLUDES = re.compile(
    r'#\s*include\s*[<"](Q[A-Z]\w*|Qt\w*|jni\.h|android/|UIKit|Foundation|Cocoa|windows\.h|Windows\.h|objc/)')
FORBIDDEN_CMAKE = re.compile(r"\b(Qt[56]?::|find_package\s*\(\s*Qt|android|UIKit|Foundation)", re.I)

bad = []
for path in list((ROOT / "core").rglob("*.[ch]pp")) + list((ROOT / "core").rglob("*.h")):
    for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if FORBIDDEN_INCLUDES.search(line):
            bad.append(f"{path.relative_to(ROOT)}:{n}: {line.strip()}")
for n, line in enumerate((ROOT / "core" / "CMakeLists.txt").read_text(encoding="utf-8").splitlines(), 1):
    if FORBIDDEN_CMAKE.search(line):
        bad.append(f"core/CMakeLists.txt:{n}: {line.strip()}")
if bad:
    print("core must stay platform independent:\n  " + "\n  ".join(bad), file=sys.stderr)
    sys.exit(1)
print("core is free of UI/platform dependencies")
