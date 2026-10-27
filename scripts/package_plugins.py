#!/usr/bin/env python3
"""Package existing x64 plugin builds as deterministic, self-contained ZIPs."""

import argparse
from datetime import datetime, timezone
import hashlib
import os
from pathlib import Path
import re
import stat
import sys
import tempfile
import zipfile

from release_version import valid_version


PLUGIN = "SMS-AnvilleSampler"
LEGAL_FILES = ("LICENSE", "HOST-LINKING-EXCEPTION.txt", "THIRD-PARTY-NOTICES.txt", "SOURCE.txt")
DEFAULT_EPOCH = 315532800  # 1980-01-01, the earliest ZIP timestamp.


class PackageError(Exception):
    """Build output is incomplete or unsafe to distribute."""


def require_file(path):
    if path.is_symlink() or not path.is_file() or path.stat().st_size == 0:
        raise PackageError("Missing, empty, or unsafe required file: {}".format(path))


def require_directory(path):
    if path.is_symlink() or not path.is_dir():
        raise PackageError("Missing or unsafe required directory: {}".format(path))


def check_companions(root, prefix=""):
    skins = root / (prefix + "skins")
    require_directory(skins)
    if not any(skins.iterdir()):
        raise PackageError("Empty skins directory: {}".format(skins))
    require_file(root / (prefix + "skin-template") / "skin.ini")
    legal = root / (prefix + "licenses")
    require_directory(legal)
    for name in LEGAL_FILES:
        require_file(legal / name)


def collect_tree(source, archive_root):
    """Return sorted files/directories without following links or special files."""
    entries = []

    def visit(path, name):
        if any(character in name for character in "\\\r\n\x00:<>\"|?*"):
            raise PackageError("Non-portable archive path: {!r}".format(name))
        mode = path.lstat().st_mode
        if stat.S_ISLNK(mode):
            raise PackageError("Symlinks are not allowed in packages: {}".format(path))
        if stat.S_ISDIR(mode):
            entries.append((name + "/", path, mode))
            for child in sorted(path.iterdir()):
                visit(child, name + "/" + child.name)
        elif stat.S_ISREG(mode):
            entries.append((name, path, mode))
        else:
            raise PackageError("Special files are not allowed in packages: {}".format(path))

    visit(source, archive_root)
    return sorted(entries)


def zip_timestamp(epoch):
    try:
        epoch = int(epoch)
        if epoch < 0:
            raise ValueError("negative epoch")
        timestamp = datetime.fromtimestamp(max(epoch, DEFAULT_EPOCH), timezone.utc)
        if timestamp.year > 2107:
            raise ValueError("ZIP timestamps end in 2107")
    except (ValueError, OverflowError, OSError) as error:
        raise PackageError("Invalid source date epoch: {}".format(epoch)) from error
    return timestamp.timetuple()[:6]


def package_plugins(build_dir, output_directory, platform, version, formats=None, source_date_epoch=None):
    if not ((version.startswith("v") and valid_version(version[1:])) or
            re.fullmatch(r"[0-9a-f]{12}", version)):
        raise PackageError("Version must be vX.Y.Z[-prerelease] or a lowercase 12-character Git commit")
    if platform not in ("Linux", "Windows"):
        raise PackageError("Unsupported platform: {}".format(platform))
    allowed = ("vst3", "lv2", "clap") if platform == "Linux" else ("vst3",)
    formats = list(allowed if formats is None else formats)
    if not formats or len(set(formats)) != len(formats) or any(fmt not in allowed for fmt in formats):
        raise PackageError("Unsupported or duplicate formats for {}: {}".format(platform, formats))
    timestamp = zip_timestamp(source_date_epoch if source_date_epoch is not None else
                              os.environ.get("SOURCE_DATE_EPOCH", DEFAULT_EPOCH))
    build_dir, output_directory = Path(build_dir), Path(output_directory)
    require_directory(build_dir)
    binaries = build_dir / "bin"
    require_directory(binaries)
    packages = []
    for fmt in formats:
        if fmt == "clap":
            require_file(binaries / (PLUGIN + ".clap"))
            check_companions(binaries, PLUGIN + ".")
            # CLAP's sidecars share one parent after extraction.
            entries = [(PLUGIN + "/", binaries, stat.S_IFDIR | 0o755)]
            for suffix in ("clap", "skins", "skin-template", "licenses"):
                name = PLUGIN + "." + suffix
                entries.extend(collect_tree(binaries / name, PLUGIN + "/" + name))
        else:
            bundle = binaries / (PLUGIN + "." + fmt)
            require_directory(bundle)
            check_companions(bundle)
            if fmt == "vst3":
                architecture, extension = (("x86_64-linux", ".so") if platform == "Linux"
                                           else ("x86_64-win", ".vst3"))
                require_file(bundle / "Contents" / architecture / (PLUGIN + extension))
            else:
                for name in ("manifest.ttl", PLUGIN + "_dsp.ttl", PLUGIN + "_ui.ttl",
                             PLUGIN + "_dsp.so", PLUGIN + "_ui.so"):
                    require_file(bundle / name)
            entries = collect_tree(bundle, bundle.name)
        qualifier = "experimental-" if fmt == "clap" or platform == "Windows" else ""
        filename = "{}-{}-{}-x64-{}{}.zip".format(PLUGIN, version, platform, qualifier, fmt)
        packages.append((filename, sorted(entries)))

    output_directory.mkdir(parents=True, exist_ok=True)
    # Validate every requested format before replacing any previous archives.
    with tempfile.TemporaryDirectory(prefix=".plugin-packages-", dir=output_directory) as staging:
        for filename, entries in packages:
            with zipfile.ZipFile(Path(staging) / filename, "w", zipfile.ZIP_DEFLATED,
                                 compresslevel=9) as archive:
                for name, path, mode in entries:
                    info = zipfile.ZipInfo(name, timestamp)
                    info.create_system = 3
                    info.compress_type = zipfile.ZIP_DEFLATED
                    info.external_attr = (stat.S_IFMT(mode) | stat.S_IMODE(mode)) << 16
                    if name.endswith("/"):
                        info.external_attr |= 0x10
                        archive.writestr(info, b"")
                    else:
                        with path.open("rb") as source, archive.open(info, "w") as destination:
                            while True:
                                block = source.read(1024 * 1024)
                                if not block:
                                    break
                                destination.write(block)
        for filename, _ in packages:
            os.replace(Path(staging) / filename, output_directory / filename)
    return [output_directory / filename for filename, _ in packages]


def write_checksums(output_directory):
    """Atomically hash ZIPs and matching source archives in basename order."""
    output_directory = Path(output_directory)
    require_directory(output_directory)
    lines = []
    for path in sorted(output_directory.iterdir()):
        if not (path.name.endswith(".zip") or path.name.endswith("-source.tar.gz")):
            continue
        require_file(path)
        if any(character in path.name for character in "\\\r\n"):
            raise PackageError("Unsafe checksum filename: {!r}".format(path.name))
        digest = hashlib.sha256()
        with path.open("rb") as source:
            for block in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(block)
        lines.append("{}  {}\n".format(digest.hexdigest(), path.name))
    if not lines:
        raise PackageError("No ZIP or source archives to checksum: {}".format(output_directory))
    with tempfile.TemporaryDirectory(prefix=".plugin-checksums-", dir=output_directory) as staging:
        temporary = Path(staging) / "SHA256SUMS.txt"
        temporary.write_bytes("".join(lines).encode("utf-8"))
        os.replace(temporary, output_directory / temporary.name)
    return output_directory / "SHA256SUMS.txt"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--platform", choices=("Linux", "Windows"))
    parser.add_argument("--version")
    parser.add_argument("--formats", nargs="+", choices=("vst3", "lv2", "clap"))
    parser.add_argument("--source-date-epoch", type=int,
                        help="UTC timestamp; defaults to SOURCE_DATE_EPOCH or 1980-01-01")
    parser.add_argument("--checksums-only", action="store_true")
    args = parser.parse_args()
    try:
        if args.checksums_only:
            write_checksums(args.output_directory)
        else:
            if args.build_dir is None or args.platform is None or args.version is None:
                parser.error("--build-dir, --platform and --version are required for packaging")
            package_plugins(args.build_dir, args.output_directory, args.platform, args.version,
                            args.formats, args.source_date_epoch)
    except (PackageError, OSError) as error:
        print("Packaging failed: {}".format(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
