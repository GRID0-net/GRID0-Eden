#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

"""Upload a checked package to the release for an existing Git tag."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from urllib.error import HTTPError
from urllib.parse import quote
from urllib.request import Request, urlopen


def api(path, data=None, method=None):
    base = os.environ.get("GITHUB_API_URL", "https://api.github.com").rstrip("/")
    request = Request(base + path, data=json.dumps(data).encode() if data is not None else None,
                      method=method,
                      headers={"Authorization": "Bearer " + os.environ["GH_TOKEN"],
                               "Accept": "application/vnd.github+json",
                               "Content-Type": "application/json",
                               "X-GitHub-Api-Version": "2026-03-10"})
    with urlopen(request, timeout=60) as response:
        return json.load(response)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--directory", type=Path, required=True)
    args = parser.parse_args()
    subprocess.run(["git", "check-ref-format", "refs/tags/" + args.tag], check=True)
    files = sorted(path for path in args.directory.iterdir() if path.is_file())
    packages = [path for path in files if path.name.endswith((".zip", ".AppImage"))]
    if not packages:
        parser.error("No release packages found")
    for package in packages:
        checksum = package.with_name(package.name + ".sha256")
        expected, filename = checksum.read_text().strip().split(maxsplit=1)
        if filename != package.name or hashlib.sha256(package.read_bytes()).hexdigest() != expected:
            raise RuntimeError(f"Checksum mismatch: {package}")
    uploads = [path for package in packages
               for path in [package, package.with_name(package.name + ".sha256")]]
    repository = os.environ["GITHUB_REPOSITORY"]
    prefix = "/repos/" + repository
    tag = quote(args.tag, safe="")
    reference = api(prefix + "/git/ref/tags/" + tag)["object"]
    while reference["type"] == "tag":
        reference = api(prefix + "/git/tags/" + reference["sha"])["object"]
    if reference["type"] != "commit" or reference["sha"] != args.commit:
        raise RuntimeError("Release tag no longer points to the commit that was built")
    release_path = prefix + "/releases/tags/" + tag
    try:
        release = api(release_path)
    except HTTPError as error:
        if error.code != 404:
            raise
        try:
            release = api(prefix + "/releases", {"tag_name": args.tag, "name": args.tag,
                                                 "draft": True})
        except HTTPError as create_error:
            if create_error.code != 422:
                raise
            # Another platform's workflow may have created the release first.
            release = api(release_path)
    if release.get("immutable"):
        raise RuntimeError("This release is immutable; use a new tag or upload to a draft release")
    subprocess.run(["gh", "release", "upload", "--repo", repository, "--clobber", "--",
                    args.tag, *map(str, uploads)], check=True)
    if release.get("draft"):
        api(prefix + "/releases/" + str(release["id"]), {"draft": False}, method="PATCH")
    print(release["html_url"])


if __name__ == "__main__":
    main()
