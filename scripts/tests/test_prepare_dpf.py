"""Test isolated DPF patch preparation and cache invalidation without Git metadata."""

import difflib
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "prepare_dpf.py"


class PrepareDpfTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="DPF preparation ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root / "source archive"
        self.source.mkdir()
        (self.source / "CMakeLists.txt").write_text("project(Fixture)\n", encoding="utf-8")
        (self.source / "plugin.cpp").write_text("int offline = 0;\n", encoding="utf-8")
        self.destination = self.root / "build" / "patched"
        self.patch = self.root / "offline.patch"
        self.write_patch()
        self.inputs = self.root / "inputs.json"
        self.inputs.write_text(json.dumps({"upstream_revision": "fixture-pin", "files": {
            "plugin.cpp": hashlib.sha256((self.source / "plugin.cpp").read_bytes().replace(
                b"\r\n", b"\n")).hexdigest()
        }}), encoding="utf-8")

    def write_patch(self, value=1):
        self.patch.write_text("".join(difflib.unified_diff(
            ["int offline = 0;\n"], ["int offline = {};\n".format(value)],
            fromfile="a/plugin.cpp", tofile="b/plugin.cpp")), encoding="utf-8")

    def prepare(self, succeeds=True, environment=None):
        result = subprocess.run([sys.executable, str(SCRIPT), "--source", str(self.source),
                                 "--destination", str(self.destination), "--patch", str(self.patch),
                                 "--inputs", str(self.inputs)], env=environment,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(result.returncode == 0, succeeds, result.stderr)
        return result

    def stamp(self):
        return self.destination / ".sms-dpf-prepared.json"

    def test_archive_without_git_and_recursive_git_exclusion(self):
        # A containing Git repository and explicit environment must not influence
        # applying patches to an archive that has no repository of its own.
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        nested = self.source / "nested"
        nested.mkdir()
        (nested / ".git").write_text("gitdir: nonexistent\n", encoding="utf-8")
        (nested / "header.h").write_text("// keep\n", encoding="utf-8")
        environment = os.environ.copy()
        environment["GIT_DIR"] = str(self.root / "nonexistent-repo")
        environment["GIT_WORK_TREE"] = str(self.root / "nonexistent-tree")
        environment["GIT_INDEX_FILE"] = str(self.root / "nonexistent-index")
        self.prepare(environment=environment)
        self.assertEqual((self.destination / "plugin.cpp").read_text(), "int offline = 1;\n")
        self.assertEqual((self.source / "plugin.cpp").read_text(), "int offline = 0;\n")
        self.assertTrue((nested / ".git").is_file())
        self.assertTrue((self.destination / "nested/header.h").is_file())
        self.assertEqual(list(self.destination.rglob(".git")), [])

    def test_unchanged_inputs_reuse_tree_and_ignore_git_metadata(self):
        (self.source / ".git").mkdir()
        (self.source / ".git/HEAD").write_text("before\n", encoding="utf-8")
        self.prepare()
        original_time = self.stamp().stat().st_mtime_ns
        (self.source / ".git/HEAD").write_text("after\n", encoding="utf-8")
        result = self.prepare()
        self.assertIn("cached", result.stdout)
        self.assertEqual(self.stamp().stat().st_mtime_ns, original_time)

    def test_crlf_checkout_and_patch_are_normalized_only_in_generated_copy(self):
        original = b"int offline = 0;\r\n"
        (self.source / "plugin.cpp").write_bytes(original)
        self.patch.write_bytes(self.patch.read_bytes().replace(b"\r\n", b"\n").replace(
            b"\n", b"\r\n"))
        self.prepare()
        self.assertEqual((self.source / "plugin.cpp").read_bytes(), original)
        self.assertEqual((self.destination / "plugin.cpp").read_bytes(), b"int offline = 1;\n")

    def test_input_drift_fails_without_modifying_old_tree(self):
        self.prepare()
        (self.source / "plugin.cpp").write_text("unreviewed upstream change\n", encoding="utf-8")
        result = self.prepare(succeeds=False)
        self.assertIn("input drift in plugin.cpp", result.stderr)
        self.assertIn("fixture-pin", result.stderr)
        self.assertEqual((self.destination / "plugin.cpp").read_text(), "int offline = 1;\n")

    def test_other_source_change_refreshes_copy(self):
        self.prepare()
        (self.source / "new.h").write_text("// newly supplied header\n", encoding="utf-8")
        result = self.prepare()
        self.assertIn("prepared", result.stdout)
        self.assertTrue((self.destination / "new.h").is_file())

    def test_patch_change_rebuilds_from_original(self):
        self.prepare()
        self.write_patch(2)
        self.prepare()
        self.assertEqual((self.destination / "plugin.cpp").read_text(), "int offline = 2;\n")

    def test_generated_target_corruption_is_repaired(self):
        self.prepare()
        (self.destination / "plugin.cpp").write_text("corrupted\n", encoding="utf-8")
        self.prepare()
        self.assertEqual((self.destination / "plugin.cpp").read_text(), "int offline = 1;\n")

    def test_failed_patch_preserves_previous_copy(self):
        self.prepare()
        self.patch.write_text("not a patch\n", encoding="utf-8")
        result = self.prepare(succeeds=False)
        self.assertIn("patch validation failed", result.stderr)
        self.assertEqual((self.destination / "plugin.cpp").read_text(), "int offline = 1;\n")
        self.assertEqual(list(self.destination.parent.glob(".sms-dpf-*")), [])

    def test_refuses_unmanaged_destination(self):
        self.destination.mkdir(parents=True)
        (self.destination / "keep.txt").write_text("user file\n", encoding="utf-8")
        result = self.prepare(succeeds=False)
        self.assertIn("unmanaged DPF destination", result.stderr)
        self.assertTrue((self.destination / "keep.txt").exists())

    def test_refuses_destination_inside_source(self):
        self.destination = self.source / "build"
        result = self.prepare(succeeds=False)
        self.assertIn("must be separate trees", result.stderr)
        self.assertFalse(self.destination.exists())


if __name__ == "__main__":
    unittest.main()
