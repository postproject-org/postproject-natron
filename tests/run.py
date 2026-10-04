"""Run normal and negative native paths in pinned Natron, then inspect public facts."""

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

from postproject import MetadataProperty, MetadataString, Production

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
parser.add_argument("--renderer", required=True, type=Path)
parser.add_argument("--library", required=True, type=Path)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
root = args.root.resolve()
root.mkdir(parents=True, exist_ok=False)
for scenario, script in (("normal", "host.py"), ("negative", "negative.py")):
    directory = root / scenario
    subprocess.run(
        [
            sys.executable,
            str(repo / "tests/prepare.py"),
            str(directory),
            "--library",
            str(args.library.resolve()),
        ],
        check=True,
    )
    environment = os.environ | {
        "POSTPROJECT_NATRON_ROOT": str(directory),
        "POSTPROJECT_NATRON_REPO": str(repo),
        "POSTPROJECT_NATRON_PYTHON": sys.executable,
        "POSTPROJECT_LIBRARY": str(args.library.resolve()),
        "POSTPROJECT_ABI_TRACE": str(directory / f"natron-{scenario}.txt"),
        "NATRON_DISK_CACHE_PATH": str(root / "cache"),
        "XDG_CACHE_HOME": str(root / "cache"),
    }
    started = time.perf_counter()
    with (directory / "natron.log").open("w") as log:
        subprocess.run(
            [
                str(args.renderer.resolve()),
                "--no-settings",
                "--opengl",
                "disabled",
                "-w",
                "AcceptanceWrite",
                "1001-1001",
                str(repo / "tests" / script),
            ],
            env=environment,
            stdout=log,
            stderr=subprocess.STDOUT,
            check=True,
            timeout=90,
        )
    assert (directory / f"{scenario}-passed").is_file()
    assert (
        (directory / "readback.1001.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
    )
    with Production.open(
        directory / "shared.pproj", library_path=args.library
    ) as production:
        assert len(production.assets) == 2
        bindings = json.loads((directory / "bindings.json").read_text())
        for asset in production.assets:
            representation = production.representations[asset.id][0]
            assert (
                production.host_bindings[representation.id]
                == bindings[asset.display_name]
            )
            assertions = production.metadata[asset.id]
            assert any(
                item.property == MetadataProperty("example.org/unrecognized", "note")
                and item.value == MetadataString("keep exactly: α/unknown")
                for item in assertions
            )
    print(
        f"Natron {scenario}: passed including process exit and decode ({time.perf_counter() - started:.3f}s)"
    )

directory = root / "normal"
environment["POSTPROJECT_NATRON_ROOT"] = str(directory)
environment.pop("POSTPROJECT_ABI_TRACE", None)
with (directory / "fallback.log").open("w") as log:
    subprocess.run(
        [
            str(args.renderer.resolve()),
            "--no-settings",
            "--opengl",
            "disabled",
            "-w",
            "FallbackWrite",
            "1001-1001",
            str(repo / "tests/fallback.py"),
        ],
        env=environment,
        stdout=log,
        stderr=subprocess.STDOUT,
        check=True,
        timeout=90,
    )
assert (directory / "fallback.1001.png").is_file()
print("Natron fallback: passed without loading the native adapter")
