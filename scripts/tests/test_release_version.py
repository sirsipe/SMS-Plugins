"""Exercise public version declarations and exact release-tag validation."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
from release_version import project_version, valid_version, verify_tag


class ReleaseVersionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / "SMS-AnvilleSampler").mkdir()

    def declare(self, numeric="0.1.0", prerelease="beta.1"):
        (self.root / "SMS-AnvilleSampler/CMakeLists.txt").write_text(
            'project(SMSAnvilleSampler VERSION ' + numeric + ' LANGUAGES C CXX)\n' +
            'set(ANVILLESAMPLER_PRERELEASE "' + prerelease + '")\n', encoding="utf-8")

    def test_beta_tag_matches_full_version_and_stable_tag_is_rejected(self):
        self.declare()
        self.assertEqual(project_version(self.root), "0.1.0-beta.1")
        self.assertEqual(verify_tag(self.root, "SMS-AnvilleSampler-v0.1.0-beta.1"), "0.1.0-beta.1")
        for tag in ("SMS-AnvilleSampler-v0.1.0", "SMS-AnvilleSampler-v0.1.beta",
                    "SMS-AnvilleSampler-v0.1.0-beta.2", "Other-v0.1.0-beta.1"):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                verify_tag(self.root, tag)

    def test_stable_version_uses_empty_prerelease(self):
        self.declare(prerelease="")
        self.assertEqual(verify_tag(self.root, "SMS-AnvilleSampler-v0.1.0"), "0.1.0")

    def test_invalid_versions_and_missing_declaration_are_rejected(self):
        for version in ("0.1.beta", "00.1.0", "0.1.0-beta.01", "0.1.0-beta..1", "0.1.0-beta/1"):
            self.assertFalse(valid_version(version), version)
        self.declare(prerelease="beta.01")
        with self.assertRaises(ValueError):
            project_version(self.root)
        (self.root / "SMS-AnvilleSampler/CMakeLists.txt").write_text("", encoding="utf-8")
        with self.assertRaises(ValueError):
            project_version(self.root)

    def test_cli_verifies_tag_and_returns_public_version(self):
        self.declare()
        command = [sys.executable, str(ROOT / "scripts/release_version.py"), "--root", str(self.root), "--tag"]
        result = subprocess.run(command + ["SMS-AnvilleSampler-v0.1.0-beta.1"], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "0.1.0-beta.1")
        wrong = subprocess.run(command + ["SMS-AnvilleSampler-v0.1.0"], text=True, capture_output=True)
        self.assertNotEqual(wrong.returncode, 0)
        self.assertIn("SMS-AnvilleSampler-v0.1.0-beta.1", wrong.stderr)


if __name__ == "__main__":
    unittest.main()
