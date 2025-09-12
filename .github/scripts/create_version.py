#!/usr/bin/env python3

import json
import argparse
import re

from pathlib import Path
from urllib.request import urlopen

RELEASES_URL = "https://api.github.com/repos/mjonuschat/PrusaSlicer/releases"
VERSION_PATTERN = re.compile(
    r"version_(?P<version>\d+\.\d+\.\d+)(-(?P<suffix>(alpha|beta|rc)\d+))?\+boss"
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", help="Write version information to this file")
    args = parser.parse_args()

    with urlopen(RELEASES_URL) as response:
        body = response.read()
        releases = json.loads(body)


    versions = {}
    assets = {}

    for release in releases:
        if release.get("draft", True):
            continue
        if not (tag := release.get("tag_name", "")):
            continue
        if not (match := VERSION_PATTERN.fullmatch(tag)):
            continue

        version = match.group("version")
        suffix = match.group("suffix") or ""

        if suffix.startswith("rc"):
            kind = "rc"
            full_version = f"{version}-{suffix}"
        elif suffix.startswith("beta"):
            kind = "beta"
            full_version = f"{version}-{suffix}"
        elif suffix.startswith("alpha"):
            kind = "alpha"
            full_version = f"{version}-{suffix}"
        else:
            kind = "release"
            full_version = version

        if versions.get(kind, "") < full_version:
            versions[kind] = full_version

            if kind == "release":
                for asset in release.get("assets", {}):
                    name = asset.get("name", "")
                    if not name:
                        continue

                    if "Linux" in name:
                        assets["linux"] = asset
                    if "MacOS-universal" in name:
                        assets["osx"] = asset
                    if "win64" in name:
                        assets["win64"] = asset

    contents = []
    if version := versions.get("release"):
        contents.append(version)
    if version := versions.get("alpha"):
        contents.append(f"alpha={version}")
    if version := versions.get("beta"):
        contents.append(f"beta={version}")
    contents.append("")

    contents.append("[common]")
    for kind in ["release", "alpha", "beta", "rc"]:
        if version := versions.get(kind, ""):
            contents.append(f"{kind} = {version}")

    contents.append("")
    for os in ["win64", "linux", "osx"]:
        if not (asset := assets.get(os, {})):
            continue

        if not (url := asset.get("browser_download_url", "")):
            continue

        if not (size := asset.get("size", 0)):
            continue

        contents.append(f"[release:{os}]")
        contents.append(f"url = {url}")
        match os:
            case "osx":
                contents.append(f"size = {size}")
            case _:
                contents.append("action = browser")
        contents.append("")

    with open(Path(args.output), 'w', encoding="utf-8") as f:
        print("\n".join(contents), file=f)


if __name__ == "__main__":
    main()
