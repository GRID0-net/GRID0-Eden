#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

"""Build a key-free NRO for testing NCE, TLS, atomics and mixed guest permissions."""

import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    source = Path(__file__).with_name("nce-smoke.s")
    with tempfile.TemporaryDirectory() as directory:
        obj = Path(directory) / "smoke.o"
        binary = Path(directory) / "smoke.bin"
        subprocess.run([os.environ.get("AARCH64_AS", "as"), str(source), "-o", str(obj)], check=True)
        subprocess.run([os.environ.get("AARCH64_OBJCOPY", "objcopy"), "-O", "binary",
                        str(obj), str(binary)], check=True)
        data = bytearray(binary.read_bytes())
    if len(data) != 0x7000:
        raise RuntimeError("Unexpected smoke image layout")
    struct.pack_into("<4sIII", data, 0x10, b"NRO0", 0, len(data), 0)
    struct.pack_into("<IIIIIII", data, 0x20, 0, 0x5000, 0x5000, 0x1000, 0x6000, 0x1000, 0)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(args.output)


if __name__ == "__main__":
    main()
