#!/usr/bin/env python3
"""Compile and run the block-pixel torus. Ctrl+C to bail."""

from __future__ import annotations

import hashlib
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
C_SRC = os.path.join(HERE, "donut.c")


def bin_path(src: bytes) -> str:
    digest = hashlib.sha256(src).hexdigest()[:16]
    return os.path.join(tempfile.gettempdir(), f"torus_{digest}")


def compile_torus(src: bytes, dest: str) -> None:
    cmd = [
        "gcc",
        "-O3",
        "-ffast-math",
        "-fno-math-errno",
        "-std=c11",
        "-o",
        dest,
        "-x",
        "c",
        "-",
        "-lm",
    ]
    r = subprocess.run(cmd, input=src, capture_output=True)
    if r.returncode != 0:
        sys.stderr.write(r.stderr.decode("utf-8", "replace"))
        sys.stderr.write("gcc failed — install gcc and try again\n")
        sys.exit(1)
    os.chmod(dest, 0o755)


def main() -> None:
    if not os.path.isfile(C_SRC):
        sys.stderr.write(f"missing {C_SRC}\n")
        sys.exit(1)
    with open(C_SRC, "rb") as f:
        src = f.read()
    dest = bin_path(src)
    if not os.path.isfile(dest):
        compile_torus(src, dest)
    os.execv(dest, [dest, *sys.argv[1:]])


if __name__ == "__main__":
    main()
