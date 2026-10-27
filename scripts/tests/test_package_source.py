"""End-to-end source archive tests using local, recursively nested Git repos."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "package_source.py"


class SourceArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="source archive ")
        self.addCleanup(self.temporary.cleanup)
        self.workspace = Path(self.temporary.name)
        self.root = self.workspace / "checkout"
        pugl = self.workspace / "pugl"
        dpf = self.workspace / "dpf"
        for repository in (self.root, pugl, dpf):
            repository.mkdir()
            self.git(repository, "init", "-q")
            self.git(repository, "config", "user.name", "Archive Test")
            self.git(repository, "config", "user.email", "archive@example.invalid")
            self.git(repository, "config", "core.symlinks", "false")
        self.write(pugl, "pugl.h", "nested pugl source\n")
        self.commit(pugl)
        self.write(dpf, "dpf.h", "framework source\n")
        self.write(dpf, ".gitignore", "/khronos/\n")
        self.git(dpf, "-c", "protocol.file.allow=always", "submodule", "add", "-q", str(pugl), "dgl/src/pugl-upstream")
        self.commit(dpf)
        self.write(self.root, "main.txt", "source\n")
        self.write(self.root, "excluded.txt", "must survive export-ignore\n")
        self.write(self.root, ".gitattributes", "excluded.txt export-ignore\n")
        self.git(self.root, "-c", "protocol.file.allow=always", "submodule", "add", "-q", str(dpf), "third_party/DPF")
        self.commit(self.root)
        self.git(self.root, "-c", "protocol.file.allow=always", "submodule", "update", "--init", "--recursive", "-q")
        # A file-backed Git symlink also exercises Windows with core.symlinks=false.
        self.write(self.root, "source-link", "main.txt")
        blob = self.git(self.root, "hash-object", "-w", "source-link").strip()
        self.git(self.root, "update-index", "--add", "--cacheinfo", "120000," + blob + ",source-link")
        self.git(self.root, "commit", "-qm", "Add source link")
        self.dpf = self.root / "third_party/DPF"
        self.pugl = self.dpf / "dgl/src/pugl-upstream"
        self.output = self.workspace / "archive.tar.gz"

    @staticmethod
    def git(repository, *arguments):
        result = subprocess.run(["git", "-C", str(repository), *arguments],
                                check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return result.stdout.decode("utf-8")

    @staticmethod
    def write(repository, path, contents):
        target = repository / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(contents, encoding="utf-8")

    def commit(self, repository):
        self.git(repository, "add", "-A")
        self.git(repository, "commit", "-qm", "Fixture source")

    def package(self, prefix="source-1.0", succeeds=True):
        result = subprocess.run([sys.executable, str(SCRIPT), "--root", str(self.root),
                                 "--output", str(self.output), "--prefix", prefix],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if succeeds:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertFalse(self.output.exists())
        return result

    def test_recursive_tree_manifest_and_reproducible_archive(self):
        self.write(self.root, "untracked.txt", "do not export")
        self.write(self.root, "build/cache.txt", "do not export")
        self.write(self.pugl, "untracked.txt", "do not export")
        extras = {
            "third_party/DPF/khronos/GL/glext.h": b"downloaded GL declarations\n",
            "third_party/DPF/khronos/KHR/khrplatform.h": b"downloaded platform declarations\n",
        }
        for path, contents in extras.items():
            target = self.root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(contents)
        self.write(self.dpf, "khronos/GL/unapproved.h", "do not export")
        self.package()
        first = self.output.read_bytes()
        self.package()
        self.assertEqual(self.output.read_bytes(), first)
        with tarfile.open(self.output, "r:gz") as archive:
            names = set(archive.getnames())
            self.assertIn("source-1.0/third_party/DPF/dpf.h", names)
            self.assertIn("source-1.0/third_party/DPF/dgl/src/pugl-upstream/pugl.h", names)
            self.assertIn("source-1.0/excluded.txt", names)
            self.assertTrue(all(name == "source-1.0" or name.startswith("source-1.0/") for name in names))
            self.assertFalse(any(part in (".git", "build", "untracked.txt", "unapproved.h")
                                 for name in names for part in name.split("/")))
            link = archive.getmember("source-1.0/source-link")
            self.assertTrue(link.issym())
            self.assertEqual(link.linkname, "main.txt")
            manifest = json.load(archive.extractfile("source-1.0/SOURCE-MANIFEST.json"))
            self.assertEqual(manifest["root_commit"], self.git(self.root, "rev-parse", "HEAD").strip())
            self.assertEqual(manifest["submodules"], {
                "third_party/DPF": self.git(self.dpf, "rev-parse", "HEAD").strip(),
                "third_party/DPF/dgl/src/pugl-upstream": self.git(self.pugl, "rev-parse", "HEAD").strip(),
            })
            self.assertEqual(manifest["extras"], [
                {"path": path, "sha256": hashlib.sha256(contents).hexdigest()}
                for path, contents in sorted(extras.items())])
            timestamp = int(self.git(self.root, "show", "-s", "--format=%ct", "HEAD"))
            for member in archive.getmembers():
                self.assertEqual(member.mtime, timestamp)
                self.assertEqual((member.uid, member.gid, member.uname, member.gname), (0, 0, "", ""))
            for path, contents in extras.items():
                self.assertEqual(archive.extractfile("source-1.0/" + path).read(), contents)

    def test_dirty_tracked_root_and_nested_submodule_rejected(self):
        for repository, path in ((self.root, "main.txt"), (self.dpf, "dpf.h"), (self.pugl, "pugl.h")):
            with self.subTest(repository=repository):
                self.write(repository, path, "dirty source\n")
                result = self.package(succeeds=False)
                self.assertIn("Dirty tracked checkout", result.stderr)
                self.git(repository, "checkout", "--", path)

    def test_staged_changes_rejected(self):
        self.write(self.root, "main.txt", "staged source\n")
        self.git(self.root, "add", "main.txt")
        self.assertIn("Dirty tracked checkout", self.package(succeeds=False).stderr)

    def test_wrong_submodule_commit_rejected(self):
        self.git(self.dpf, "config", "user.name", "Archive Test")
        self.git(self.dpf, "config", "user.email", "archive@example.invalid")
        self.write(self.dpf, "dpf.h", "different commit\n")
        self.commit(self.dpf)
        self.assertIn("expected", self.package(succeeds=False).stderr)

    def test_uninitialized_nested_submodule_rejected(self):
        self.git(self.dpf, "submodule", "deinit", "-f", "--", "dgl/src/pugl-upstream")
        self.assertIn("Uninitialized", self.package(succeeds=False).stderr)

    def test_unsafe_prefix_rejected(self):
        for prefix in ("../escape", "/absolute", "a/b", "a\\b", ".", "..", "C:drive"):
            with self.subTest(prefix=prefix):
                self.assertIn("Prefix", self.package(prefix=prefix, succeeds=False).stderr)


if __name__ == "__main__":
    unittest.main()
