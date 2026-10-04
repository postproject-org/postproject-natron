"""Negative cases executed by real Natron through the installed native adapter."""

# ruff: noqa: F821
import os
import shutil
import subprocess
import sys
from pathlib import Path

root = Path(os.environ["POSTPROJECT_NATRON_ROOT"])
repo = Path(os.environ["POSTPROJECT_NATRON_REPO"])
sys.path[:0] = [str(repo / "build"), str(repo / "plugin")]
import _postproject_natron as native
from postproject_reader import (
    ResolutionRequest,
    associate_reader,
    save_association,
    sequence_arguments,
)

production = root / "shared.pproj"
original = root / "plates"
reader = app.createReader(str(original / "plateA.####.png"))
arguments = sequence_arguments(reader, production, 1001, 1003, 24)
value = associate_reader(reader, arguments, native.candidates(*arguments)[0])
fallback = reader.getParam("filename").get()

frame = original / "plateA.1002.png"
content = frame.read_bytes()
frame.unlink()
request = ResolutionRequest(app, reader, original)
try:
    partial = request.apply()
    assert partial["availability"] == 2, partial
    assert partial["missing_frames"] == [1002], partial
    assert reader.getParam("filename").get() == fallback
finally:
    request.close()
frame.write_bytes(content)

search = root / "candidates"
search.mkdir()
shutil.copytree(original, search / "one")
shutil.copytree(original, search / "two")
original.rename(root / "hidden")
request = ResolutionRequest(app, reader, search)
try:
    ambiguous = request.apply()
    assert ambiguous["availability"] == 4, ambiguous
    assert len(ambiguous["candidates"]) == 2, ambiguous
    assert reader.getParam("filename").get() == fallback
finally:
    request.close()
(root / "hidden").rename(original)

# A cancelled or obsolete host generation must never confirm or remap.
request = ResolutionRequest(app, reader, original)
request.close()
assert request.apply() is None
request = ResolutionRequest(app, reader, original)
reader.getParam("filename").set(str(original / "plateB.####.png"))
try:
    assert request.apply() is None
finally:
    request.close()
reader.getParam("filename").set(fallback)

# A deleted Reader's script name may be reused by a new node. Never access
# the dead wrapper or apply its result to the replacement, even with the same
# filename and saved association.
request = ResolutionRequest(app, reader, original)
name = reader.getScriptName()
# Natron can defer destruction while processing stops. Free the script name
# explicitly so the test does not depend on that asynchronous timing.
reader.setScriptName(name + "Retired")
reader.destroy()
reader = app.createReader(fallback)
reader.setScriptName(name)
assert reader.getScriptName() == name
save_association(reader, value)
try:
    assert request.apply() is None
    assert reader.getParam("filename").get() == fallback
finally:
    request.close()

# Native result strings remain owned Python values after the native owner dies.
owner = native.resolve(str(production), value["binding"], str(original))
copied = native.details(owner)
del owner
assert copied["candidates"][0]["pattern"] == fallback

# Another process changes the locator set after the decision's retained base.
decision_directory = root / "decision"
original.rename(decision_directory)
owner = native.resolve(str(production), value["binding"], str(decision_directory))
subprocess.run(
    [
        os.environ["POSTPROJECT_NATRON_PYTHON"],
        str(repo / "tests" / "writer.py"),
        str(production),
        value["binding"],
        str(search / "one"),
    ],
    check=True,
    env={
        key: value
        for key, value in os.environ.items()
        if key not in {"PYTHONHOME", "PYTHONPATH"}
    },
)
try:
    native.confirm(owner)
    raise AssertionError("Stale locator decision unexpectedly committed")
except RuntimeError as error:
    conflict = error.args[0]["conflict"]
    assert conflict["superseding_sequence"] > conflict["base_sequence"]
assert reader.getParam("filename").get() == fallback
decision_directory.rename(original)
(root / "negative-passed").write_text(
    "partial, ambiguity, cancellation, stale generation, owned results, conflict\n"
)
print("Natron native negative paths passed")
writer = app.createWriter(str(root / "readback.####.png"))
writer.setScriptName("AcceptanceWrite")
writer.connectInput(0, app.createReader(str(original / "plateA.####.png")))
