#!/usr/bin/env python3
"""Assemble the legal documents accompanying Anville Sampler binaries."""

import argparse
import json
from pathlib import Path
import re
import shutil
import subprocess


def read(path):
    # read_text() would silently normalize vendored CRLF notices.
    return path.read_bytes().decode("utf-8")


def first_block(path):
    text = read(path)
    start = text.index("/*")
    return text[start:text.index("*/", start) + 2]


def leading_comments(path):
    lines = []
    for line in read(path).splitlines(keepends=True):
        if line.startswith("//") or not line.strip():
            lines.append(line)
        else:
            break
    return "".join(lines)


def third_party_notices(root):
    sections = []

    def add(label, content):
        sections.append("\n=== " + label + " ===\n\n" + content + "\n")

    def whole(relative):
        add(relative, read(root / relative))

    dpf = root / "third_party/DPF"
    whole("third_party/DPF/LICENSE")
    # DPF utility headers also contain third-party notices below their prologues.
    for name in ("ScopedPointer.hpp", "LeakDetector.hpp", "Base64.hpp"):
        path = dpf / "distrho/extra" / name
        for block in re.findall(r"/\*.*?\*/", read(path), flags=re.DOTALL):
            if "Copyright" in block:
                add(path.relative_to(root).as_posix(), block)
    string = read(dpf / "distrho/extra/String.hpp")
    start = string.index("    // base64 stuff, based on")
    add("String.hpp embedded base64 attribution", string[start:string.index("\n\n", start)])
    for directory in ("distrho/src/travesty", "distrho/src/lv2"):
        for path in sorted((dpf / directory).glob("*.h")):
            add(path.relative_to(root).as_posix(), first_block(path))

    pugl = dpf / "dgl/src/pugl-upstream"
    whole("third_party/DPF/dgl/src/pugl-upstream/COPYING")
    # COPYING alone omits platform contributors and newer copyright years.
    for directory in (pugl / "src", pugl / "include/pugl"):
        for path in sorted(directory.iterdir()):
            if path.is_file() and path.suffix in (".h", ".c", ".m", ".mm"):
                content = leading_comments(path)
                if content:
                    add(path.relative_to(root).as_posix(), content)

    whole("third_party/DPF/dgl/src/nanovg/LICENSE.txt")
    for name in ("fontstash.h", "nanovg_gl.h", "nanovg_gl_utils.h"):
        path = dpf / "dgl/src/nanovg" / name
        add(path.relative_to(root).as_posix(), leading_comments(path))
    fontstash = read(dpf / "dgl/src/nanovg/fontstash.h")
    utf8_start = fontstash.index("// Copyright (c) 2008-2010 Bjoern Hoehrmann")
    utf8_end = fontstash.index("\n\n", utf8_start)
    add("fontstash.h embedded UTF-8 decoder attribution", fontstash[utf8_start:utf8_end])
    add("UTF-8 decoder MIT permission; https://bjoern.hoehrmann.de/utf-8/decoder/dfa/#license",
        read(root / "licenses/UTF8-decoder.txt"))

    image = read(dpf / "dgl/src/nanovg/stb_image.h")
    start = image.index("\nLICENSE\n")
    add("stb_image.h local public-domain dedication and fallback license",
        image[start:image.index("*/", start) + 2])
    truetype = read(dpf / "dgl/src/nanovg/stb_truetype.h")
    start = truetype.rindex("/*")
    add("stb_truetype.h complete dual-license footer", truetype[start:truetype.index("*/", start) + 2])
    whole("third_party/DPF/dgl/src/resources/LICENSE-DejaVuSans.ttf.txt")
    add("libSOFD", first_block(dpf / "distrho/extra/sofd/libsofd.h"))

    licensing = read(dpf / "LICENSING.md")
    clap_row = next(line for line in licensing.splitlines() if line.startswith("| CLAP"))
    add("DPF LICENSING.md CLAP attribution", clap_row)
    add("CLAP 1.1.1 MIT license; https://github.com/free-audio/clap/blob/1.1.1/LICENSE",
        read(root / "licenses/CLAP-MIT.txt"))

    for relative in ("khronos/GL/glext.h", "khronos/KHR/khrplatform.h"):
        path = dpf / relative
        if path.exists():
            notice = first_block(path)
            add(path.relative_to(root).as_posix(), notice)
            if "SPDX-License-Identifier: MIT" in notice and "Permission is hereby granted" not in notice:
                mit = read(root / "licenses/CLAP-MIT.txt")
                add("Full MIT terms for the preceding Khronos SPDX notice",
                    mit[mit.index("Permission is hereby granted"):])

    return "Third-party notices for SMS-Anville Sampler\n" + "".join(sections)


def source_information(root):
    try:
        # Avoid accidentally identifying a parent checkout when using an archive.
        if not (root / ".git").exists():
            raise ValueError("source archive")
        commit = subprocess.check_output(
            ["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
        submodules = subprocess.check_output(
            ["git", "-C", str(root), "submodule", "status", "--recursive"], text=True).strip()
        dirty = subprocess.check_output(
            ["git", "-C", str(root), "status", "--porcelain", "--untracked-files=normal"],
            text=True)
        identity = "Base repository commit: " + commit + "\nSubmodules:\n" + submodules + "\n"
        identity += ("Working tree contains uncommitted changes; the commit alone does not "
                     "identify the complete source used for this build.\n" if dirty else
                     "Working tree is clean.\n")
    except (OSError, subprocess.CalledProcessError, ValueError):
        manifest = root / "SOURCE-MANIFEST.json"
        if manifest.exists():
            data = json.loads(read(manifest))
            identity = "Repository commit: " + data["root_commit"] + "\nSubmodules:\n"
            identity += "".join(path + " " + commit + "\n"
                                for path, commit in sorted(data["submodules"].items()))
            identity += "See SOURCE-MANIFEST.json in the source archive for dependency checksums.\n"
        else:
            identity = "Exact revision unavailable in this source tree.\n"
    return ("SMS-Anville Sampler source information\n\n" + identity +
            "\nUse the accompanying *-source.tar.gz archive for the corresponding source, "
            "including vendored dependencies. The archive's SOURCE-MANIFEST.json records "
            "its revisions and additional dependency checksums. Build and source packaging "
            "instructions are in Docs/RELEASING.md inside that archive.\n")


def package(root, output):
    output.mkdir(parents=True, exist_ok=True)
    for name in ("LICENSE", "HOST-LINKING-EXCEPTION.txt"):
        shutil.copyfile(root / name, output / name)
    (output / "THIRD-PARTY-NOTICES.txt").write_bytes(third_party_notices(root).encode("utf-8"))
    (output / "SOURCE.txt").write_text(source_information(root), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output-directory", required=True, type=Path)
    args = parser.parse_args()
    package(args.root.resolve(), args.output_directory.resolve())


if __name__ == "__main__":
    main()
