"""Native Reader fallback with no PostProject extension imported in this process."""
# ruff: noqa: F821

import json
import os
from pathlib import Path

root = Path(os.environ["POSTPROJECT_NATRON_ROOT"])
reopened = app.loadProject(str(root / "sequence.ntp"))
readers = [
    node for node in reopened.getChildren() if node.getParam("postprojectAssociation")
]
assert len(readers) == 1
reader = readers[0]
binding = json.loads(reader.getParam("postprojectAssociation").get())["binding"]
assert binding in json.loads((root / "bindings.json").read_text()).values()
filename = reader.getParam("filename").get()
assert filename == str(root / "moved" / "plateA.####.png")
writer = app.createWriter(str(root / "fallback.####.png"))
writer.setScriptName("FallbackWrite")
writer.connectInput(0, app.createReader(filename))
print("Natron ordinary Reader fallback passed without loading the native adapter")
