#!/usr/bin/env python3
"""Prepare a cached, patched DPF build tree without modifying vendored sources.

Git is required only to apply the maintained patch; source archives need no Git
metadata. The input manifest verifies the exact upstream files the patch targets.
CRLF checkouts are validated as LF text and normalized only in the generated copy.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
STAMP = ".sms-dpf-prepared.json"


class PreparationError(Exception):
    pass


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def text_digest(path):
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def tree_signature(source):
    """Detect source updates cheaply, excluding recursive submodule Git metadata."""
    records = []
    for directory, directories, filenames in os.walk(source, followlinks=False):
        directories[:] = sorted(name for name in directories if name != ".git")
        for name in sorted(filenames + [name for name in directories
                                        if (Path(directory) / name).is_symlink()]):
            if name == ".git":
                continue
            path = Path(directory) / name
            stat = path.lstat()
            records.append((path.relative_to(source).as_posix(), stat.st_mode,
                            stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns,
                            os.readlink(path) if path.is_symlink() else None))
    return hashlib.sha256(json.dumps(records).encode("utf-8")).hexdigest()


def apply_patch(git, destination, patch, check=False):
    environment = os.environ.copy()
    # Do not discover the enclosing project repository or inherit a caller's
    # explicit repository/index. This also works in exported source archives.
    for name in ("GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_COMMON_DIR"):
        environment.pop(name, None)
    environment["GIT_CEILING_DIRECTORIES"] = str(destination.parent)
    # Git's checkout conversion can reintroduce CRLF after normalization,
    # including when applying outside a repository on Windows.
    command = [git, "-c", "core.autocrlf=false", "-c", "core.eol=lf",
               "-C", str(destination), "apply", "--whitespace=error"]
    if check:
        command.append("--check")
    result = subprocess.run(command + [str(patch)], env=environment,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if result.returncode:
        raise PreparationError("DPF patch {} failed: {}".format(
            "validation" if check else "application", result.stderr.strip()))


def prepare(source, destination, patch, inputs, git="git"):
    source, destination = source.resolve(), destination.resolve()
    patch, inputs = patch.resolve(), inputs.resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        raise PreparationError("DPF source and generated destination must be separate trees")
    if not (source / "CMakeLists.txt").is_file():
        raise PreparationError("DPF source is missing; initialize the pinned submodule first")
    manifest = json.loads(inputs.read_text(encoding="utf-8"))
    for relative, expected in manifest["files"].items():
        path = source / relative
        if (Path(relative).is_absolute() or ".." in Path(relative).parts or path.is_symlink()
                or not path.is_file() or text_digest(path) != expected):
            raise PreparationError("DPF input drift in {} (expected upstream {}). "
                                   "Update and review the maintained patch and input hashes.".format(
                                       relative, manifest["upstream_revision"]))
    identity = {"version": 1, "source": str(source), "tree": tree_signature(source),
                "patch": digest(patch), "inputs": digest(inputs),
                "helper": digest(Path(__file__))}
    stamp_path = destination / STAMP
    if destination.exists():
        if not stamp_path.is_file():
            raise PreparationError("Refusing to replace an unmanaged DPF destination: " + str(destination))
        previous = json.loads(stamp_path.read_text(encoding="utf-8"))
        outputs = previous.get("outputs", {})
        if previous.get("identity") == identity and outputs and all(
                (destination / relative).is_file() and digest(destination / relative) == expected
                for relative, expected in outputs.items()):
            return False
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".sms-dpf-", dir=destination.parent))
    try:
        shutil.copytree(source, temporary, dirs_exist_ok=True, symlinks=True,
                        ignore=shutil.ignore_patterns(".git"))
        for relative in manifest["files"]:
            path = temporary / relative
            contents = path.read_bytes()
            if b"\r\n" in contents:
                path.write_bytes(contents.replace(b"\r\n", b"\n"))
        normalized_patch = temporary / ".sms-offline-host-mode.patch"
        normalized_patch.write_bytes(patch.read_bytes().replace(b"\r\n", b"\n"))
        apply_patch(git, temporary, normalized_patch, check=True)
        apply_patch(git, temporary, normalized_patch)
        normalized_patch.unlink()
        outputs = {relative: digest(temporary / relative) for relative in manifest["files"]}
        (temporary / STAMP).write_text(json.dumps({"identity": identity, "outputs": outputs},
                                                indent=2) + "\n", encoding="utf-8")
        if destination.exists():
            shutil.rmtree(destination)
        temporary.replace(destination)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--destination", required=True, type=Path)
    parser.add_argument("--patch", type=Path, default=ROOT / "cmake/DPF/offline-host-mode.patch")
    parser.add_argument("--inputs", type=Path, default=ROOT / "cmake/DPF/offline-host-mode.inputs.json")
    parser.add_argument("--git", default="git")
    arguments = parser.parse_args()
    try:
        changed = prepare(arguments.source, arguments.destination, arguments.patch,
                          arguments.inputs, arguments.git)
    except (PreparationError, OSError, ValueError, KeyError) as error:
        parser.exit(1, "DPF preparation failed: {}\n".format(error))
    print("DPF build tree {}: {}".format("prepared" if changed else "cached", arguments.destination))


if __name__ == "__main__":
    main()
