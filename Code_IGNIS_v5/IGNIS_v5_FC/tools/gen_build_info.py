"""Write src/config/build_info.h with the current git hash (+ '-dirty').

Run before compiling a build you intend to fly:
    python -I tools/gen_build_info.py
The file is gitignored. Without it, version.h falls back to "nogit".
"""
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parent.parent


def git(*args):
    return subprocess.run(["git", *args], cwd=root, capture_output=True,
                          text=True, check=False).stdout.strip()


h = git("rev-parse", "--short=10", "HEAD") or "nogit"
if git("status", "--porcelain", "--", "."):
    h += "-dirty"
out = root / "src" / "config" / "build_info.h"
out.write_text(f'#pragma once\n#define IGNIS_GIT_HASH "{h}"\n', encoding="ascii")
print(f"build_info.h: IGNIS_GIT_HASH={h}")
