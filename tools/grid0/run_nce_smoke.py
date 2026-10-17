#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

"""Run the key-free NCE smoke test with isolated settings and the Null renderer."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve()
    with tempfile.TemporaryDirectory(prefix="eden-nce-smoke-") as directory:
        root = Path(directory)
        nro = root / "smoke.nro"
        subprocess.run([sys.executable, str(Path(__file__).with_name("build_nce_smoke.py")),
                        str(nro)], check=True, stdout=subprocess.DEVNULL)
        config = root / "smoke.ini"
        config.write_text(
            "[Cpu]\ncpu_backend=1\ncpu_backend\\default=false\n"
            "[Renderer]\nbackend=2\nbackend\\default=false\n"
            "[Audio]\noutput_engine=3\noutput_engine\\default=false\n"
            "[Miscellaneous]\nlog_filter=*:Info Core.ARM:Debug\n"
            "log_filter\\default=false\nflush_line=true\nflush_line\\default=false\n"
        )
        env = os.environ.copy()
        env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
        for key, folder in [("XDG_CONFIG_HOME", "config"), ("XDG_DATA_HOME", "data"),
                            ("XDG_CACHE_HOME", "cache")]:
            (root / folder).mkdir()
            env[key] = str(root / folder)
        for folder in ["load", "tas", "sdmc", "dump", "play_time"]:
            (root / "data/eden" / folder).mkdir(parents=True, exist_ok=True)
        output = root / "console.log"
        try:
            with output.open("wb") as console:
                result = subprocess.run([str(executable), "-c", str(config), "-g", str(nro)],
                                        env=env, stdout=console, stderr=subprocess.STDOUT,
                                        timeout=30)
        except subprocess.TimeoutExpired:
            print(output.read_text(errors="replace"))
            raise SystemExit("NCE smoke test timed out")
        log_path = root / "data/eden/log/eden_log.txt"
        log = log_path.read_text(errors="replace") if log_path.exists() else ""
        steps = [int(step) for step in re.findall(r"NCE software step (\d+)", log)]
        if result.returncode != 0 or "GRID0 NCE SMOKE PASS" not in log or not steps:
            print(log or output.read_text(errors="replace"))
            raise SystemExit(f"NCE smoke test failed (exit {result.returncode})")
        if max(steps) >= 4096:
            raise SystemExit("Guest passed, but the arithmetic loop used software stepping")
        print("GRID0 NCE SMOKE PASS: native loop, TLS, counters, atomics and mixed permissions")


if __name__ == "__main__":
    main()
