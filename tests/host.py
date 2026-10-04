"""Executed inside pinned Natron; every production operation uses native C++."""
# ruff: noqa: F821

import json
import os
import shutil
import sys
from pathlib import Path

root = Path(os.environ["POSTPROJECT_NATRON_ROOT"])
repo = Path(os.environ["POSTPROJECT_NATRON_REPO"])
sys.path[:0] = [str(repo / "build"), str(repo / "plugin")]
import _postproject_natron as native
from postproject_reader import (
    ResolutionRequest,
    associate_reader,
    association,
    refresh_reader,
    sequence_arguments,
    verify_reader,
)

production = root / "shared.pproj"
original = root / "plates"
moved = root / "moved"
expected = json.loads((root / "bindings.json").read_text())
reader = app.createReader(str(original / "plateA.####.png"))
assert reader is not None
arguments = sequence_arguments(reader, production, 1001, 1003, 24)
choices = native.candidates(*arguments)
assert choices == [expected["plateA."]], choices
value = associate_reader(reader, arguments, choices[0])
assert value["binding"] == expected["plateA."]
assert verify_reader(reader) == 1
page = refresh_reader(reader)
assert page["events"] > 0 and page["through"] > 0

project = root / "sequence.ntp"
name = reader.getScriptName()
assert app.saveProjectAs(str(project))
reopened = app.loadProject(str(project))
reader = reopened.getNode(name)
assert association(reader)["binding"] == value["binding"]
shutil.move(original, moved)
request = ResolutionRequest(reopened, reader, moved, "plates")
try:
    details = request.apply()
    assert len(details["recorded_locators"]) == 2, details
    assert details["availability"] == 1 and len(details["candidates"]) == 1, details
    assert reader.getParam("filename").get() == str(moved / "plateA.####.png")
finally:
    request.close()
assert verify_reader(reader) == 1
assert refresh_reader(reader)["events"] > 0
assert reopened.saveProject(str(project))

# Canonical binding survives save/reopen; ordinary filename remains usable offline.
offline = production.with_suffix(".offline")
production.rename(offline)
try:
    fallback = reopened.loadProject(str(project)).getNode(name)
    assert fallback.getParam("filename").get() == str(moved / "plateA.####.png")
    assert association(fallback)["binding"] == value["binding"]
finally:
    offline.rename(production)
(root / "normal-passed").write_text(
    "native sequence adoption, move, verification, revisions, reopen\n"
)
print("Natron native normal path passed")

# Decode a genuine frame through Natron's Reader/Writer, not just parameter reads.
writer = app.createWriter(str(root / "readback.####.png"))
writer.setScriptName("AcceptanceWrite")
writer.connectInput(0, app.createReader(str(moved / "plateA.####.png")))
