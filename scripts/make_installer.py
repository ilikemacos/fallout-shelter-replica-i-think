#!/usr/bin/env python3
"""Build the self-contained installers.

    dist/macOS/haven.sh     from scripts/installer_header.sh
    dist/Windows/haven.ps1  from scripts/installer_header.ps1

Each is its header with a base64 tarball of the game source appended after
the payload marker, so the whole game travels as one file.

    python3 scripts/make_installer.py            # both
    python3 scripts/make_installer.py macos      # just one

Runs on any platform; each produced installer runs on its own.
"""

from __future__ import annotations
import base64
import io
import sys
import tarfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# name -> (header template, output path, executable bit)
TARGETS = {
    "macos": (ROOT / "scripts" / "installer_header.sh",
              ROOT / "dist" / "macOS" / "haven.sh", True),
    "windows": (ROOT / "scripts" / "installer_header.ps1",
                ROOT / "dist" / "Windows" / "haven.ps1", False),
}

INCLUDE = ["haven", "scripts", "tests", "run.py", "requirements.txt",
           "README.md", "LICENSE"]
EXCLUDE_DIRS = {"__pycache__", ".git", "build", "dist", ".venv"}


def _filter(info: tarfile.TarInfo):
    parts = Path(info.name).parts
    if any(p in EXCLUDE_DIRS for p in parts):
        return None
    if info.name.endswith((".pyc", ".ico", ".icns")):
        return None
    # deterministic metadata so the installer is reproducible
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    info.mtime = 0
    return info


def build_payload() -> bytes:
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz", compresslevel=9) as tar:
        for name in INCLUDE:
            src = ROOT / name
            if not src.exists():
                raise SystemExit(f"missing {src}")
            tar.add(src, arcname=name, filter=_filter)
    return buf.getvalue()


def write_installer(header_path: Path, out: Path, executable: bool,
                    payload: bytes, b64_lines: str):
    header = header_path.read_text(encoding="utf-8")
    marker = "__HAVEN_PAYLOAD_BELOW__\n"
    # The marker may also appear where the script defines it, so anchor on
    # the last occurrence — that is the one the payload follows.
    if marker not in header:
        raise SystemExit(f"{header_path.name} is missing the payload marker")
    header = header[: header.rindex(marker) + len(marker)]

    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", encoding="utf-8", newline="\n") as f:
        f.write(header)
        f.write(b64_lines)
        f.write("\n")
    if executable:
        out.chmod(0o755)
    print(f"Wrote {out}  ({out.stat().st_size / 1024:.0f} KB, "
          f"payload {len(payload) / 1024:.0f} KB)")


def main():
    wanted = sys.argv[1:] or list(TARGETS)
    unknown = [w for w in wanted if w not in TARGETS]
    if unknown:
        raise SystemExit(f"unknown target(s): {', '.join(unknown)}. "
                         f"Choose from: {', '.join(TARGETS)}")

    payload = build_payload()
    b64 = base64.b64encode(payload).decode("ascii")
    lines = "\n".join(b64[i:i + 76] for i in range(0, len(b64), 76))

    for name in wanted:
        header_path, out, executable = TARGETS[name]
        write_installer(header_path, out, executable, payload, lines)


if __name__ == "__main__":
    main()
