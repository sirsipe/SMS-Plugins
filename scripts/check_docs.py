#!/usr/bin/env python3
"""Check owned Markdown links, index coverage, size, and instruction aliases.

Uses Git's file list to skip ignored output and submodule contents. Supports
inline Markdown links; does not check remote URLs, fragments, or factual claims.
"""

from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import unquote, urlsplit


ROOT = Path(__file__).resolve().parents[1]
INDEX = ROOT / "Docs/AI/INDEX.md"
AI_REFERENCES = {
    "Common-Src/README.md",
    "Common-UI/README.md",
    "SMS-Midichopper/Docs/ARCHITECTURE.md",
}
LIMITS = {
    "AGENTS.md": 600,
    "Docs/AI/INDEX.md": 350,
    ".github/copilot-instructions.md": 100,
}
LINK = re.compile(r'!?\[[^\]\n]*\]\((?:<([^>]+)>|([^\s)]+))(?:\s+"[^"]*")?\)')


def local_targets(path: Path, content: str):
    # Ignore examples inside fenced code blocks.
    content = re.sub(r"^(`{3,}|~{3,}).*?^\1\s*$", "", content,
                     flags=re.MULTILINE | re.DOTALL)
    for match in LINK.finditer(content):
        href = match.group(1) or match.group(2)
        url = urlsplit(href)
        if url.scheme or url.netloc or not url.path:
            continue
        target = (path.parent / unquote(url.path)).resolve()
        yield href, target


def main() -> int:
    listing = subprocess.check_output(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=ROOT,
    ).decode().split("\0")
    paths = sorted({ROOT / name for name in listing
                    if name.endswith(".md") and not name.startswith("third_party/")})
    errors = []
    contents = {}
    for path in paths:
        # Removed tracked files are handled by index/link checks, not read here.
        if not path.exists() and not path.is_symlink():
            continue
        relative = path.relative_to(ROOT).as_posix()
        if path.is_symlink():
            if not path.exists():
                errors.append(f"{relative}: broken symlink")
            continue
        content = path.read_text(encoding="utf-8")
        contents[path] = content
        limit = LIMITS.get(relative, 900 if relative.startswith("Docs/AI/")
                           or relative in AI_REFERENCES else 600)
        words = len(content.split())
        if words > limit:
            errors.append(f"{relative}: {words} words exceeds {limit}; trim or split by task")
        for href, target in local_targets(path, content):
            if not target.is_relative_to(ROOT):
                errors.append(f"{relative}: local link leaves repository: {href}")
            elif not target.exists():
                errors.append(f"{relative}: missing local link target: {href}")

    required = ["AGENTS.md", ".github/copilot-instructions.md", "Docs/AI/INDEX.md"]
    for name in required:
        if ROOT / name not in contents:
            errors.append(f"{name}: required regular Markdown file is missing")

    alias = ROOT / "CLAUDE.md"
    if not alias.is_symlink() or alias.readlink() != Path("AGENTS.md"):
        errors.append("CLAUDE.md: must be a relative symlink to AGENTS.md")

    if INDEX in contents:
        indexed = {target for _, target in local_targets(INDEX, contents[INDEX])}
        for path in contents:
            if path != INDEX and path not in indexed:
                errors.append(f"{path.relative_to(ROOT)}: add a link from Docs/AI/INDEX.md")

    if errors:
        print("Documentation checks failed:\n" + "\n".join(f"- {error}" for error in errors))
        return 1
    entry_words = len(contents[ROOT / "AGENTS.md"].split())
    print(f"Documentation OK: {len(contents)} pages; AGENTS.md {entry_words}/600 words.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
