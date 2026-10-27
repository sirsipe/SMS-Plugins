#!/usr/bin/env python3
"""Check packaged notices against the actual vendored dependencies."""

import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("package_licenses", ROOT / "scripts/package_licenses.py")
PACKAGER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGER)


class LicensePackagingTests(unittest.TestCase):
    def copy_notice_inputs(self, root):
        dpf = root / "third_party/DPF"
        dpf.mkdir(parents=True)
        for relative in ("licenses", "third_party/DPF/distrho/src/lv2",
                         "third_party/DPF/distrho/src/travesty",
                         "third_party/DPF/dgl/src/pugl-upstream/src",
                         "third_party/DPF/dgl/src/pugl-upstream/include",
                         "third_party/DPF/dgl/src/nanovg"):
            shutil.copytree(ROOT / relative, root / relative)
        for relative in ("LICENSE", "LICENSING.md", "dgl/src/pugl-upstream/COPYING",
                         "distrho/extra/sofd/libsofd.h",
                         "distrho/extra/ScopedPointer.hpp", "distrho/extra/LeakDetector.hpp",
                         "distrho/extra/Base64.hpp", "distrho/extra/String.hpp",
                         "dgl/src/resources/LICENSE-DejaVuSans.ttf.txt"):
            destination = dpf / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / "third_party/DPF" / relative, destination)
        for name in ("LICENSE", "HOST-LINKING-EXCEPTION.txt"):
            shutil.copyfile(ROOT / name, root / name)

    def test_real_repository_cli_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            subprocess.run([sys.executable, str(ROOT / "scripts/package_licenses.py"),
                            "--root", str(ROOT), "--output-directory", str(output)], check=True)
            self.assertEqual({path.name for path in output.iterdir()}, {
                "LICENSE", "HOST-LINKING-EXCEPTION.txt", "THIRD-PARTY-NOTICES.txt", "SOURCE.txt"})
            for name in ("LICENSE", "HOST-LINKING-EXCEPTION.txt"):
                self.assertEqual((output / name).read_bytes(), (ROOT / name).read_bytes())
            notices = (output / "THIRD-PARTY-NOTICES.txt").read_bytes()
            for name in ("licenses/CLAP-MIT.txt", "licenses/UTF8-decoder.txt",
                         "third_party/DPF/LICENSE",
                         "third_party/DPF/dgl/src/resources/LICENSE-DejaVuSans.ttf.txt"):
                self.assertIn((ROOT / name).read_bytes(), notices)
            source = (output / "SOURCE.txt").read_text()
            self.assertIn("third_party/DPF", source)
            self.assertIn("*-source.tar.gz", source)
            self.assertIn("Docs/RELEASING.md", source)

    def test_overlooked_authors_and_full_permissions(self):
        notices = PACKAGER.third_party_notices(ROOT)
        for text in ("Lars Luthman", "Leonard Ritter", "Gabriel M. Beddingfield",
                     "Paul Barton-Davis, Stefan Westerfeld", "Ben Loftis, Harrison Consoles",
                     "Robin Gareus", "Hanspeter Portner", "Bjoern Hoehrmann",
                     "Raw Material Software Ltd.", "René Nyffenegger",
                     "Copyright (c) 2021 Alexandre BIQUE", "2014-2022 Alexandre Bique",
                     "Copyright (c) 2017 Sean Barrett", "Tavmjong Bah",
                     "This notice may not be removed or altered from any source distribution.",
                     "Where that dedication is not", "perpetual, irrevocable license",
                     "The above copyright and trademark notices and this permission notice"):
            self.assertIn(text, notices)
        # Ensure the short MIT label was not substituted for either added license.
        self.assertIn((ROOT / "licenses/CLAP-MIT.txt").read_bytes().decode(), notices)
        self.assertIn((ROOT / "licenses/UTF8-decoder.txt").read_bytes().decode(), notices)
        self.assertLess(len(notices), 120000, "Do not package complete stb implementation headers")

    def test_source_archive_manifest_without_git(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = {"format_version": 1, "root_commit": "a" * 40,
                        "submodules": {"third_party/DPF": "b" * 40}, "extras": []}
            (root / "SOURCE-MANIFEST.json").write_text(json.dumps(manifest))
            source = PACKAGER.source_information(root)
            self.assertIn("a" * 40, source)
            self.assertIn("third_party/DPF " + "b" * 40, source)
            self.assertIn("SOURCE-MANIFEST.json", source)

    def test_checkout_line_endings_do_not_change_notice_contents(self):
        expected = PACKAGER.third_party_notices(ROOT).replace("\r\n", "\n").encode("utf-8")
        for newline in (b"\n", b"\r\n"):
            with self.subTest(newline=newline), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                self.copy_notice_inputs(root)
                # Reproduce Git checkouts using either native line-ending style.
                for path in root.rglob("*"):
                    if path.is_file():
                        path.write_bytes(path.read_bytes().replace(b"\r\n", b"\n").replace(b"\n", newline))
                output = root / "output"
                subprocess.run([sys.executable, str(ROOT / "scripts/package_licenses.py"),
                                "--root", str(root), "--output-directory", str(output)], check=True)
                notices = (output / "THIRD-PARTY-NOTICES.txt").read_bytes()
                self.assertEqual(notices.replace(b"\r\n", b"\n"), expected)
                # Input notices remain verbatim, even in the CRLF checkout.
                self.assertIn((root / "third_party/DPF/LICENSE").read_bytes(), notices)
                self.assertIn((root / "licenses/UTF8-decoder.txt").read_bytes(), notices)
                self.assertEqual((output / "LICENSE").read_bytes(), (root / "LICENSE").read_bytes())

    def test_optional_khronos_notices_after_preprocessor_lines(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.copy_notice_inputs(root)
            dpf = root / "third_party/DPF"
            # glext.h has an SPDX-only comment; khrplatform.h has full terms.
            headers = {
                "GL/glext.h": ("#ifndef GL_EXTENSION\n/*\r\n"
                               "Copyright 2013-2026 The Khronos Group Inc.\r\n"
                               "SPDX-License-Identifier: MIT\r\n*/\r\n"),
                "KHR/khrplatform.h": ("#ifndef KHR_PLATFORM\n/*\n"
                                      "Copyright (c) 2008-2018 The Khronos Group Inc.\n"
                                      "Permission is hereby granted to use the Materials.\n*/\n")}
            for relative, content in headers.items():
                path = dpf / "khronos" / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content.encode())
            notices = PACKAGER.third_party_notices(root)
            for content in headers.values():
                self.assertIn(content[content.index("/*"):content.index("*/") + 2], notices)
            self.assertIn("Full MIT terms for the preceding Khronos SPDX notice", notices)
            self.assertIn("Permission is hereby granted, free of charge", notices)


if __name__ == "__main__":
    unittest.main()
