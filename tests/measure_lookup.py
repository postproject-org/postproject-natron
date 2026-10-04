"""Informational scaling of the same native naming-aware lookup used by Readers."""

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path

from postproject import ImageSequenceSource, Production, SequenceNaming

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
parser.add_argument("--library", type=Path, required=True)
args = parser.parse_args()
root = args.root.resolve()
assert not root.exists()
repo = Path(__file__).resolve().parents[1]
subprocess.run(
    [
        sys.executable,
        str(repo / "tests/prepare.py"),
        str(root),
        "--library",
        str(args.library.resolve()),
    ],
    check=True,
)
sys.path.insert(0, str(repo / "build"))
import _postproject_natron as native

arguments = (
    str(root / "shared.pproj"),
    str(root / "plates"),
    "plateA.",
    ".png",
    4,
    1001,
    1003,
    24,
    1,
)
expected = [json.loads((root / "bindings.json").read_text())["plateA."]]
measurements = []
for count in (2, 202):
    if count == 202:
        with (
            Production.open(
                root / "shared.pproj", library_path=args.library
            ) as production,
            production.transaction() as transaction,
        ):
            # Independent logical assets may share this other locator;
            # their naming must never contaminate the selected A query.
            source = ImageSequenceSource(
                root / "plates",
                SequenceNaming("plateB.", ".png", 4),
                1001,
                1003,
                1,
                24,
                1,
                (),
            )
            for index in range(200):
                transaction.import_media(source, f"Other sequence {index}")
    started = time.perf_counter()
    for _ in range(100):
        assert native.candidates(*arguments) == expected
    milliseconds = (time.perf_counter() - started) * 10
    measurements.append({"assets": count, "queries": 100, "mean_ms": milliseconds})
(root / "lookup-timing.json").write_text(json.dumps(measurements, indent=2) + "\n")
print(json.dumps(measurements))
