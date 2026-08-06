#!/usr/bin/env python3
"""Run PlatformIO only after the repository dependency policy is enforced."""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import venv
from pathlib import Path


C3_IDF_ENVIRONMENTS = {
    "esp32-c3-super-mini",
    "esp32-c3-oled-128x64",
    "esp32-c3-oled-128x32",
}
IDF_VERSION = "4.4.7"
IDF_ENV_VERSION = "1.0.0"


def selected_environments(arguments: list[str]) -> set[str]:
    selected: set[str] = set()
    for index, argument in enumerate(arguments):
        if argument in ("-e", "--environment") and index + 1 < len(arguments):
            selected.add(arguments[index + 1])
        elif argument.startswith("--environment="):
            selected.add(argument.split("=", 1)[1])
    return selected


def prepare_idf_python(root: Path) -> None:
    core_dir = Path(os.environ.get("PLATFORMIO_CORE_DIR", root / ".platformio"))
    environment = core_dir / "penv" / f".espidf-{IDF_VERSION}"
    environment.parent.mkdir(parents=True, exist_ok=True)
    venv.EnvBuilder(with_pip=True, clear=True).create(environment)

    scripts = "Scripts" if os.name == "nt" else "bin"
    python = environment / scripts / ("python.exe" if os.name == "nt" else "python")
    requirements = root / "tools" / "espidf-python-requirements.txt"
    subprocess.run(
        [
            "uv",
            "pip",
            "install",
            "--python",
            str(python),
            "--require-hashes",
            "--no-deps",
            "--requirement",
            str(requirements),
        ],
        check=True,
        cwd=root,
    )

    version = sys.version_info
    metadata = {
        "version": IDF_ENV_VERSION,
        "python_version": (
            f"{version.major}.{version.minor}.{version.micro}-"
            f"{version.releaselevel}.{version.serial}"
        ),
    }
    (environment / "pio-idf-venv.json").write_text(
        json.dumps(metadata, indent=2), encoding="utf-8"
    )


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    project = root if (root / "platformio.ini").exists() else root / "firmware"
    subprocess.run(
        [sys.executable, str(root / "tools" / "check_dependency_age.py")],
        check=True,
        cwd=root,
    )
    if selected_environments(sys.argv[1:]) & C3_IDF_ENVIRONMENTS:
        prepare_idf_python(root)

    pio = shutil.which("pio")
    if not pio:
        print("PlatformIO is not installed in the locked uv environment.", file=sys.stderr)
        return 1
    return subprocess.run([pio, *sys.argv[1:]], cwd=project).returncode


if __name__ == "__main__":
    raise SystemExit(main())
