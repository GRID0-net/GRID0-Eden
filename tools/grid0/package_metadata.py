# SPDX-License-Identifier: GPL-3.0-or-later

"""Read package provenance from the checkout, including container-mounted checkouts."""

from pathlib import Path
import subprocess


def read_git_metadata(root):
    root = Path(root).resolve()

    def git(*arguments):
        result = subprocess.run(
            ["git", "-c", f"safe.directory={root.as_posix()}", "-C", str(root), *arguments],
            capture_output=True, text=True,
        )
        if result.returncode:
            raise RuntimeError(
                f"Cannot read package Git metadata from {root}: {result.stderr.strip()}"
            )
        return result.stdout

    return git("rev-parse", "--verify", "HEAD^{commit}").strip(), bool(
        git("status", "--porcelain")
    )
