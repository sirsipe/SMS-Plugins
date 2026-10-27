#!/usr/bin/env python3
"""Read and validate the public version declared in the plugin's CMake project."""

import argparse
from pathlib import Path
import re
import sys


NUMBER = r"(?:0|[1-9][0-9]*)"
IDENTIFIER = r"(?:0|[1-9][0-9]*|[0-9A-Za-z-]*[A-Za-z-][0-9A-Za-z-]*)"
VERSION = re.compile(NUMBER + r"\." + NUMBER + r"\." + NUMBER +
                     r"(?:-" + IDENTIFIER + r"(?:\." + IDENTIFIER + r")*)?")
TAG_PREFIX = "SMS-AnvilleSampler-v"


def valid_version(version):
    return VERSION.fullmatch(version) is not None


def project_version(root):
    cmake = (Path(root) / "SMS-AnvilleSampler/CMakeLists.txt").read_text(encoding="utf-8")
    numeric = re.search(r"^project\(SMSAnvilleSampler VERSION ([0-9]+\.[0-9]+\.[0-9]+)\s", cmake, re.MULTILINE)
    prerelease = re.search(r'^set\(ANVILLESAMPLER_PRERELEASE "([^"\n]*)"\)', cmake, re.MULTILINE)
    if numeric is None or prerelease is None:
        raise ValueError("CMake must declare numeric VERSION and ANVILLESAMPLER_PRERELEASE")
    version = numeric.group(1)
    if prerelease.group(1):
        version += "-" + prerelease.group(1)
    if not valid_version(version):
        raise ValueError("Invalid release version: " + version)
    return version


def verify_tag(root, tag):
    version = project_version(root)
    expected = TAG_PREFIX + version
    if tag != expected:
        raise ValueError("Release tag must be " + expected + "; got " + tag)
    return version


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--tag", help="Require this exact version tag")
    args = parser.parse_args()
    try:
        print(verify_tag(args.root, args.tag) if args.tag is not None else project_version(args.root))
    except (OSError, ValueError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
