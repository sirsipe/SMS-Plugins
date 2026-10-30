#!/usr/bin/env python3
"""Exercise complete portable plugin archives and their failure boundaries."""

import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zipfile


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/package_plugins.py"
sys.path.insert(0, str(SCRIPT.parent))
SPEC = importlib.util.spec_from_file_location("package_plugins", SCRIPT)
PACKAGER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGER)
PLUGIN = PACKAGER.PLUGIN


class PluginPackagingTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.build = self.root / "build"
        self.bin = self.build / "bin"
        self.bin.mkdir(parents=True)
        self.output = self.root / "dist"

    def file(self, path, contents=b"fixture contents\n", executable=False):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(contents)
        path.chmod(0o755 if executable else 0o644)

    def companions(self, path, prefix=""):
        self.file(path / (prefix + "skins") / "Skin with spaces" / "skin.ini")
        self.file(path / (prefix + "skins") / "Skin with spaces" / "nested" / "logo.png")
        self.file(path / (prefix + "skin-template") / "skin.ini")
        for name in PACKAGER.LEGAL_FILES:
            self.file(path / (prefix + "licenses") / name, ("notice " + name).encode())

    def fixture(self, platform="Linux", formats=("vst3", "lv2")):
        for fmt in formats:
            bundle = self.bin / (PLUGIN + "." + fmt)
            if fmt == "clap":
                self.file(bundle, executable=True)
                self.companions(self.bin, PLUGIN + ".")
            else:
                self.companions(bundle)
                if fmt == "vst3":
                    arch, extension = (("x86_64-linux", ".so") if platform == "Linux" else
                                       ("x86_64-win", ".vst3"))
                    self.file(bundle / "Contents" / arch / (PLUGIN + extension), executable=True)
                else:
                    for suffix in ("_dsp.so", "_ui.so", "_dsp.ttl", "_ui.ttl"):
                        self.file(bundle / (PLUGIN + suffix), executable=suffix.endswith(".so"))
                    self.file(bundle / "manifest.ttl")

    def package(self, platform="Linux", version="v1.2.3", **kwargs):
        return PACKAGER.package_plugins(self.build, self.output, platform, version, **kwargs)

    def assert_tree(self, archive, expected_files, root):
        with zipfile.ZipFile(archive) as zipped:
            self.assertEqual({name for name in zipped.namelist() if not name.endswith("/")},
                             set(expected_files))
            self.assertEqual({name.split("/")[0] for name in zipped.namelist()}, {root})
            self.assertEqual(zipped.namelist(), sorted(zipped.namelist()))
            self.assertTrue(all("\\" not in name for name in zipped.namelist()))
            for name, source in expected_files.items():
                self.assertEqual(zipped.read(name), source.read_bytes())
                mode = zipped.getinfo(name).external_attr >> 16
                self.assertTrue(stat.S_ISREG(mode))
                self.assertEqual(stat.S_IMODE(mode) & 0o111, source.stat().st_mode & 0o111)
                self.assertEqual(zipped.getinfo(name).date_time, (1980, 1, 1, 0, 0, 0))

    def test_linux_default_exact_trees(self):
        self.fixture()
        with mock.patch.dict(os.environ, {"SOURCE_DATE_EPOCH": str(PACKAGER.DEFAULT_EPOCH)}):
            archives = self.package()
        self.assertEqual([path.name for path in archives], [
            PLUGIN + "-v1.2.3-Linux-x64-vst3.zip",
            PLUGIN + "-v1.2.3-Linux-x64-lv2.zip"])
        for archive, fmt in zip(archives, ("vst3", "lv2")):
            bundle = self.bin / (PLUGIN + "." + fmt)
            expected = {path.relative_to(self.bin).as_posix(): path
                        for path in bundle.rglob("*") if path.is_file()}
            self.assert_tree(archive, expected, bundle.name)

    def test_linux_ignores_local_clap_build_and_rejects_explicit_packaging(self):
        self.fixture(formats=("vst3", "lv2", "clap"))
        archives = self.package()
        self.assertEqual({path.name for path in self.output.iterdir()}, {
            PLUGIN + "-v1.2.3-Linux-x64-vst3.zip",
            PLUGIN + "-v1.2.3-Linux-x64-lv2.zip"})
        for archive in archives:
            with zipfile.ZipFile(archive) as zipped:
                self.assertFalse(any(".clap" in name for name in zipped.namelist()))
        with self.assertRaises(PACKAGER.PackageError):
            self.package(formats=["clap"])
        result = subprocess.run([
            sys.executable, str(SCRIPT), "--build-dir", str(self.build),
            "--output-directory", str(self.output), "--platform", "Linux",
            "--version", "v1.2.3", "--formats", "clap"], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid choice", result.stderr)

    def test_windows_cli_default_layout(self):
        self.fixture("Windows", ("vst3",))
        bundle = self.bin / (PLUGIN + ".vst3")
        expected = {path.relative_to(self.bin).as_posix(): path
                    for path in bundle.rglob("*") if path.is_file()}
        for version in ("abcdef012345", "v1.2.3", "v0.9.1-beta.1"):
            with self.subTest(version=version):
                subprocess.run([sys.executable, str(SCRIPT), "--build-dir", str(self.build),
                                "--output-directory", str(self.output), "--platform", "Windows",
                                "--version", version,
                                "--source-date-epoch", str(PACKAGER.DEFAULT_EPOCH)], check=True)
                archive = self.output / (PLUGIN + "-" + version + "-Windows-x64-vst3.zip")
                self.assert_tree(archive, expected, bundle.name)
        self.assertEqual({path.name for path in self.output.iterdir()}, {
            PLUGIN + "-" + version + "-Windows-x64-vst3.zip"
            for version in ("abcdef012345", "v1.2.3", "v0.9.1-beta.1")})

    def test_beta_version_is_preserved_in_all_asset_names(self):
        self.fixture()
        archives = self.package(version="v0.1.0-beta.1")
        self.assertEqual(len(archives), 2)
        self.assertTrue(all("-v0.1.0-beta.1-Linux-x64-" in path.name for path in archives))
        for archive in archives:
            with zipfile.ZipFile(archive) as zipped:
                self.assertIsNone(zipped.testzip())

    def test_reproducible_despite_source_mtime_and_replacement(self):
        self.fixture()
        first = {path.name: path.read_bytes() for path in self.package(source_date_epoch=1700000001)}
        for source in self.bin.rglob("*"):
            os.utime(source, (1709999999, 1709999999))
        second = {path.name: path.read_bytes() for path in self.package(source_date_epoch=1700000001)}
        self.assertEqual(first, second)
        self.assertFalse(any(path.name.startswith(".") for path in self.output.iterdir()))
        with zipfile.ZipFile(next(iter(self.output.glob("*.zip")))) as archive:
            self.assertTrue(all(info.date_time == (2023, 11, 14, 22, 13, 20)
                                for info in archive.infolist()))

    def test_checksums_cli_includes_source_and_excludes_other_files(self):
        self.fixture("Windows", ("vst3",))
        archives = self.package("Windows")
        source = self.output / (PLUGIN + "-v1.2.3-source.tar.gz")
        source.write_bytes(b"source archive")
        (self.output / "irrelevant.txt").write_bytes(b"ignore")
        (self.output / "SHA256SUMS.txt").write_bytes(b"old checksums")
        subprocess.run([sys.executable, str(SCRIPT), "--output-directory", str(self.output),
                        "--checksums-only"], check=True)
        expected = "".join("{}  {}\n".format(hashlib.sha256(path.read_bytes()).hexdigest(), path.name)
                           for path in sorted(archives + [source]))
        self.assertEqual((self.output / "SHA256SUMS.txt").read_bytes(), expected.encode())

    def test_missing_required_inputs_leave_prior_output_untouched(self):
        self.fixture()
        archives = self.package()
        original = {path: path.read_bytes() for path in archives}
        missing = [self.bin / (PLUGIN + ".vst3") / "Contents/x86_64-linux" / (PLUGIN + ".so"),
                   self.bin / (PLUGIN + ".lv2") / "manifest.ttl",
                   self.bin / (PLUGIN + ".vst3") / "skin-template" / "skin.ini"]
        missing += [self.bin / (PLUGIN + ".lv2") / "licenses" / name
                    for name in PACKAGER.LEGAL_FILES]
        for path in missing:
            with self.subTest(path=path):
                contents = path.read_bytes()
                path.unlink()
                with self.assertRaises(PACKAGER.PackageError):
                    self.package()
                self.file(path, contents)
                self.assertEqual({archive: archive.read_bytes() for archive in archives}, original)

    def test_missing_companion_directory_and_wrong_platform_binary(self):
        self.fixture()
        skins = self.bin / (PLUGIN + ".vst3") / "skins"
        shutil.rmtree(skins)
        with self.assertRaises(PACKAGER.PackageError):
            self.package()
        self.companions(self.bin / (PLUGIN + ".vst3"))
        with self.assertRaises(PACKAGER.PackageError):
            self.package("Windows")
        self.assertFalse(self.output.exists())

    def test_unsafe_versions_formats_timestamps_and_missing_build(self):
        for version in ("../v1.2.3", "v1.2.3/evil", "1.2.3", "abc123", "v1.2.3\n", "a" * 40,
                        "v0.1.beta", "v0.1.0-beta.01", "v0.1.0-beta..1", "v0.1.0-beta/1"):
            with self.subTest(version=version), self.assertRaises(PACKAGER.PackageError):
                self.package(version=version)
        for formats in (["clap"], ["lv2"], [], ["vst3", "vst3"]):
            with self.subTest(formats=formats), self.assertRaises(PACKAGER.PackageError):
                self.package("Windows", formats=formats)
        for epoch in (-1, "invalid", 999999999999):
            with self.subTest(epoch=epoch), self.assertRaises(PACKAGER.PackageError):
                self.package(source_date_epoch=epoch)
        with self.assertRaises(PACKAGER.PackageError):
            PACKAGER.package_plugins(self.root / "missing", self.output, "Linux", "v1.2.3")

    @unittest.skipIf(os.name == "nt", "Windows symlink creation requires an optional privilege")
    def test_reject_symlinks_and_nonportable_paths(self):
        self.fixture()
        bundle = self.bin / (PLUGIN + ".vst3")
        link = bundle / "linked-license"
        link.symlink_to(bundle / "licenses/LICENSE")
        with self.assertRaises(PACKAGER.PackageError):
            self.package()
        link.unlink()
        self.file(bundle / "bad\\name")
        with self.assertRaises(PACKAGER.PackageError):
            self.package()
        self.assertFalse(self.output.exists())

    def test_write_failure_preserves_previous_archive_and_cleans_staging(self):
        self.fixture()
        archives = self.package()
        previous = {path: path.read_bytes() for path in archives}
        with mock.patch.object(zipfile.ZipFile, "open", side_effect=OSError("disk full")):
            with self.assertRaises(OSError):
                self.package()
        self.assertEqual({path: path.read_bytes() for path in archives}, previous)
        self.assertEqual(set(self.output.iterdir()), set(archives))


if __name__ == "__main__":
    unittest.main()
