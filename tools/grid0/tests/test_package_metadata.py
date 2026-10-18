# SPDX-License-Identifier: GPL-3.0-or-later

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from package_metadata import read_git_metadata


class PackageMetadataTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="grid0-package-git-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "checkout with spaces"
        self.root.mkdir()
        self.environment = patch.dict(os.environ, {
            "GIT_CONFIG_NOSYSTEM": "1",
            "GIT_CONFIG_GLOBAL": str(Path(self.temporary.name) / "global.config"),
            "GIT_CONFIG_COUNT": "0",
            "GIT_TEST_ASSUME_DIFFERENT_OWNER": "0",
        })
        self.environment.start()
        self.addCleanup(self.environment.stop)
        self.git("init", "--quiet")
        (self.root / "tracked.txt").write_text("original\n")
        self.git("add", "tracked.txt")
        self.git("-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                 "-c", "commit.gpgsign=false", "commit", "--quiet", "-m", "fixture")
        self.commit = self.git("rev-parse", "HEAD").strip()

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.root), *args], check=True,
                              capture_output=True, text=True).stdout

    def test_container_ownership_check_is_reproduced_and_handled(self):
        os.environ["GIT_TEST_ASSUME_DIFFERENT_OWNER"] = "1"
        baseline = subprocess.run(["git", "-C", str(self.root), "rev-parse", "HEAD"],
                                  capture_output=True, text=True)
        self.assertEqual(baseline.returncode, 128)
        self.assertIn("dubious ownership", baseline.stderr)
        config = (self.root / ".git/config").read_bytes()
        self.assertEqual(read_git_metadata(self.root), (self.commit, False))
        self.assertEqual((self.root / ".git/config").read_bytes(), config)
        self.assertFalse(Path(os.environ["GIT_CONFIG_GLOBAL"]).exists())

    def test_dirty_checkout_keeps_its_actual_revision(self):
        os.environ["GIT_TEST_ASSUME_DIFFERENT_OWNER"] = "1"
        os.environ["GITHUB_SHA"] = "f" * 40
        (self.root / "tracked.txt").write_text("modified\n")
        self.assertEqual(read_git_metadata(self.root), (self.commit, True))
        (self.root / "tracked.txt").write_text("original\n")
        (self.root / "untracked.txt").write_text("new\n")
        self.assertEqual(read_git_metadata(self.root), (self.commit, True))

    def test_missing_checkout_reports_git_error(self):
        with self.assertRaisesRegex(RuntimeError, "not a git repository"):
            read_git_metadata(self.root.parent)

    def test_workflow_trust_step_allows_later_plain_git_commands(self):
        os.environ["GIT_TEST_ASSUME_DIFFERENT_OWNER"] = "1"
        subprocess.run(["git", "config", "--global", "--add", "safe.directory",
                        str(self.root)], check=True)
        self.assertEqual(self.git("rev-parse", "HEAD").strip(), self.commit)
        self.assertEqual(self.git("status", "--porcelain"), "")
        self.assertEqual(Path(os.environ["GIT_CONFIG_GLOBAL"]).read_text().count("directory ="), 1)


if __name__ == "__main__":
    unittest.main()
