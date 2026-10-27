#!/usr/bin/env python3
"""Export a clean, recursively pinned Git checkout as a reproducible source archive."""

import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import tarfile
import tempfile
from typing import Dict, List, NamedTuple, Optional


MANIFEST_NAME = "SOURCE-MANIFEST.json"
EXTRA_PATHS = (
    "third_party/DPF/khronos/GL/glext.h",
    "third_party/DPF/khronos/KHR/khrplatform.h",
)


class PackageError(Exception):
    """A checkout cannot be exported faithfully."""


class Entry(NamedTuple):
    path: str
    mode: str
    repository: Path
    object_id: str


def git(repository: Path, *arguments: str) -> bytes:
    result = subprocess.run(
        ["git", "-C", str(repository), *arguments],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False,
    )
    if result.returncode:
        detail = result.stderr.decode("utf-8", errors="replace").strip()
        raise PackageError("Git failed in {}: {}".format(repository, detail))
    return result.stdout


def validate_repository(repository: Path, expected: Optional[str] = None) -> str:
    if not repository.is_dir():
        raise PackageError("Uninitialized submodule: {}".format(repository))
    try:
        top_level = Path(os.fsdecode(git(repository, "rev-parse", "--show-toplevel")).strip()).resolve()
    except PackageError as error:
        raise PackageError("Uninitialized or invalid repository: {}".format(repository)) from error
    if top_level != repository.resolve():
        raise PackageError("Uninitialized submodule: {}".format(repository))
    commit = git(repository, "rev-parse", "HEAD").decode("ascii").strip()
    if expected is not None and commit != expected:
        raise PackageError("Submodule {} is at {}, expected {}".format(repository, commit, expected))
    # Check the index separately so that changed, staged gitlinks are also rejected.
    for arguments in (("diff", "--cached", "--quiet", "--ignore-submodules=none", "HEAD", "--"),
                      ("diff", "--quiet", "--ignore-submodules=all", "--")):
        result = subprocess.run(["git", "-C", str(repository), *arguments],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        if result.returncode == 1:
            raise PackageError("Dirty tracked checkout: {}".format(repository))
        if result.returncode:
            raise PackageError("Cannot check tracked changes in {}: {}".format(
                repository, result.stderr.decode("utf-8", errors="replace").strip()))
    return commit


def collect(repository: Path, commit: str, prefix: str,
            entries: List[Entry], submodules: Dict[str, str], repositories: Dict[Path, str]) -> None:
    validate_repository(repository, commit)
    repositories[repository] = commit
    for record in git(repository, "ls-tree", "-rz", "--full-tree", commit).split(b"\0"):
        if not record:
            continue
        metadata, raw_path = record.split(b"\t", 1)
        mode, kind, object_id = metadata.decode("ascii").split()
        relative_path = os.fsdecode(raw_path)
        path = prefix + relative_path
        parts = PurePosixPath(path).parts
        if (not parts or any(part in (".", "..", ".git") for part in parts)
                or PurePosixPath(path).is_absolute()):
            raise PackageError("Unsafe tracked path: {!r}".format(path))
        if mode == "160000" and kind == "commit":
            submodules[path] = object_id
            collect(repository / relative_path, object_id, path + "/", entries, submodules, repositories)
        elif kind == "blob" and mode in ("100644", "100755", "120000"):
            entries.append(Entry(path, mode, repository, object_id))
        else:
            raise PackageError("Unsupported Git entry: {} {}".format(mode, path))


def tar_entry(archive: tarfile.TarFile, name: str, contents: bytes,
              timestamp: int, mode: int = 0o644, symlink: bool = False) -> None:
    info = tarfile.TarInfo(name)
    info.mtime = timestamp
    info.mode = mode
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    if symlink:
        info.type = tarfile.SYMTYPE
        info.linkname = os.fsdecode(contents)
        archive.addfile(info)
    else:
        info.size = len(contents)
        archive.addfile(info, io.BytesIO(contents))


def package_source(root: Path, output: Path, prefix: str) -> None:
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]*", prefix):
        raise PackageError("Prefix must be a single folder using letters, digits, '.', '_' or '-'")
    root = root.resolve()
    output = output.resolve()
    commit = validate_repository(root)
    timestamp = int(git(root, "show", "-s", "--format=%ct", commit).strip())
    if not 0 <= timestamp <= 0xFFFFFFFF:
        raise PackageError("Commit timestamp is outside the gzip timestamp range")
    entries: List[Entry] = []
    submodules: Dict[str, str] = {}
    repositories: Dict[Path, str] = {}
    collect(root, commit, "", entries, submodules, repositories)
    tracked_paths = {entry.path for entry in entries}
    if MANIFEST_NAME in tracked_paths:
        raise PackageError("Tracked file conflicts with generated {}".format(MANIFEST_NAME))
    extras = {}
    for path in EXTRA_PATHS:
        candidate = root / path
        if path not in tracked_paths and candidate.exists():
            if not candidate.is_file() or candidate.is_symlink():
                raise PackageError("Extra header must be a regular file: {}".format(path))
            extras[path] = candidate.read_bytes()
    if output in (root / entry.path for entry in entries) or output in (root / path for path in extras):
        raise PackageError("Output would overwrite source input: {}".format(output))
    manifest = {
        "format_version": 1,
        "root_commit": commit,
        "submodules": dict(sorted(submodules.items())),
        "extras": [{"path": path, "sha256": hashlib.sha256(contents).hexdigest()}
                   for path, contents in sorted(extras.items())],
    }
    manifest_bytes = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, prefix=".source-", suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            with gzip.GzipFile(filename="", mode="wb", fileobj=stream, mtime=timestamp) as compressed:
                with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
                    directory = tarfile.TarInfo(prefix + "/")
                    directory.type = tarfile.DIRTYPE
                    directory.mode = 0o755
                    directory.mtime = timestamp
                    archive.addfile(directory)
                    # Ordering and metadata depend only on the captured Git tree and allowed extras.
                    by_path = {entry.path: entry for entry in entries}
                    for path in sorted([*by_path, *extras, MANIFEST_NAME]):
                        name = prefix + "/" + path
                        if path == MANIFEST_NAME:
                            tar_entry(archive, name, manifest_bytes, timestamp)
                        elif path in extras:
                            tar_entry(archive, name, extras[path], timestamp)
                        else:
                            entry = by_path[path]
                            contents = git(entry.repository, "show", entry.object_id)
                            tar_entry(archive, name, contents, timestamp,
                                      0o755 if entry.mode == "100755" else 0o644,
                                      entry.mode == "120000")
        for repository, expected in repositories.items():
            validate_repository(repository, expected)
        for path, contents in extras.items():
            if (root / path).read_bytes() != contents:
                raise PackageError("Extra header changed during export: {}".format(path))
        os.replace(temporary, output)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent,
                        help="Git checkout to export (default: repository containing this script)")
    parser.add_argument("--output", required=True, type=Path, help="Destination .tar.gz file")
    parser.add_argument("--prefix", required=True, help="Single top-level archive folder")
    arguments = parser.parse_args()
    try:
        package_source(arguments.root, arguments.output, arguments.prefix)
    except (PackageError, OSError, ValueError) as error:
        print("Source packaging failed: {}".format(error), file=sys.stderr)
        return 1
    print("Created {}".format(arguments.output))
    return 0


if __name__ == "__main__":
    sys.exit(main())
