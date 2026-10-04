"""Natron Reader bridge; production operations live in the C++ extension."""

import json
import re
import uuid
from concurrent.futures import ThreadPoolExecutor
from fractions import Fraction
from pathlib import Path

import _postproject_natron as native

PARAMETER = "postprojectAssociation"


def association(reader):
    parameter = reader.getParam(PARAMETER)
    return json.loads(parameter.get()) if parameter and parameter.get() else None


def save_association(reader, value):
    parameter = reader.getParam(PARAMETER)
    if parameter is None:
        parameter = reader.createStringParam(PARAMETER, "PostProject association")
        parameter.setPersistent(True)
        parameter.setVisible(False)
        reader.refreshUserParamsGUI()
    parameter.set(json.dumps(value, sort_keys=True))


def sequence_arguments(reader, production, first, last, rate):
    pattern = Path(reader.getParam("filename").get())
    match = re.fullmatch(r"(.*?)(#+)(\.[^/]+)", pattern.name)
    if match is None:
        raise ValueError("Select a compact sequence with a #-padded Reader pattern")
    rational = Fraction(str(rate)).limit_denominator(1_000_000)
    return (
        str(production),
        str(pattern.parent),
        match[1],
        match[3],
        len(match[2]),
        first,
        last,
        rational.numerator,
        rational.denominator,
    )


def associate_reader(reader, arguments, selected=""):
    previous = association(reader)
    reader_id = previous["reader"] if previous else str(uuid.uuid4())
    binding = native.associate(*arguments, reader_id, selected)
    value = {
        "production": arguments[0],
        "binding": binding,
        "reader": reader_id,
        "cursor": 0,
    }
    save_association(reader, value)
    return value


def refresh_reader(reader):
    value = association(reader)
    if value is None:
        raise ValueError("Associate the Reader first")
    page = native.refresh(value["production"], value["binding"], value["cursor"])
    value["cursor"] = page["through"]
    save_association(reader, value)
    return page


def verify_reader(reader):
    value = association(reader)
    if value is None:
        raise ValueError("Associate the Reader first")
    pattern = Path(reader.getParam("filename").get())
    match = re.fullmatch(r"(.*?)(#+)(\.[^/]+)", pattern.name)
    if match is None:
        raise ValueError("Reader no longer names a #-padded sequence")
    return native.verify(
        value["production"],
        value["binding"],
        str(pattern.parent),
        match[1],
        match[3],
        len(match[2]),
    )


class ResolutionRequest:
    """Copy inputs on the host thread; apply only to the same object/generation."""

    def __init__(self, app, reader, directory, root_name=""):
        self.app = app
        self.name = reader.getScriptName()
        self.reader = reader
        self.filename = reader.getParam("filename").get()
        self.value = association(reader)
        if self.value is None:
            raise ValueError("Associate the Reader first")
        self.closed = False
        self.executor = ThreadPoolExecutor(max_workers=1)
        self.future = self.executor.submit(
            native.resolve,
            self.value["production"],
            self.value["binding"],
            str(directory),
            root_name,
        )

    def apply(self):
        if self.closed or self.app.getNode(self.name) is None:
            return None
        if (
            self.reader.getParam("filename").get() != self.filename
            or association(self.reader) != self.value
        ):
            return None
        owner = self.future.result()
        details = native.details(owner)
        if details["availability"] == 1 and len(details["candidates"]) == 1:
            native.confirm(owner)
            # Copy into the Reader before releasing the adapter's result owner.
            self.reader.getParam("filename").set(details["candidates"][0]["pattern"])
        return details

    def close(self):
        self.closed = True
        self.future.cancel()
        self.executor.shutdown(wait=True, cancel_futures=True)


_requests = []


def cancel_all(app=None):
    for request in _requests:
        request.close()
    _requests.clear()


def selected_reader(app):
    nodes = app.getSelectedNodes()
    if len(nodes) != 1 or nodes[0].getParam("filename") is None:
        raise ValueError("Select one Reader")
    return nodes[0]


def menu_associate(app):
    from qtpy.QtWidgets import QFileDialog, QInputDialog, QMessageBox

    try:
        reader = selected_reader(app)
        production, _ = QFileDialog.getOpenFileName(
            None, "Choose production", "", "PostProject (*.pproj)"
        )
        if not production:
            return
        first, ok = QInputDialog.getInt(None, "Sequence", "First frame", 1001)
        if not ok:
            return
        last, ok = QInputDialog.getInt(None, "Sequence", "Last frame", first)
        if not ok:
            return
        arguments = sequence_arguments(
            reader, production, first, last, app.getProjectParam("frameRate").get()
        )
        choices = native.candidates(*arguments)
        selected = ""
        if choices:
            selected, ok = QInputDialog.getItem(
                None, "Adopt sequence", "Choose representation", choices, 0, False
            )
            if not ok:
                return
        associate_reader(reader, arguments, selected)
    except (RuntimeError, ValueError) as error:
        QMessageBox.warning(None, "PostProject", str(error))


def menu_refresh(app):
    from qtpy.QtWidgets import QMessageBox

    try:
        page = refresh_reader(selected_reader(app))
        QMessageBox.information(
            None,
            "PostProject",
            f"{page['name']}: {page['events']} events; cursor {page['through']}",
        )
    except (RuntimeError, ValueError) as error:
        QMessageBox.warning(None, "PostProject", str(error))


def menu_verify(app):
    from qtpy.QtWidgets import QMessageBox

    try:
        result = verify_reader(selected_reader(app))
        QMessageBox.information(None, "PostProject", f"Content verification: {result}")
    except (RuntimeError, ValueError) as error:
        QMessageBox.warning(None, "PostProject", str(error))


def menu_resolve(app):
    from qtpy.QtCore import QTimer
    from qtpy.QtWidgets import QFileDialog, QInputDialog, QMessageBox

    try:
        if _requests:
            raise ValueError("Finish or cancel the current resolution first")
        directory = QFileDialog.getExistingDirectory(
            None, "Search moved sequence directory"
        )
        if not directory:
            return
        root_name, ok = QInputDialog.getText(
            None, "Moved sequence", "Logical media root (blank for directory search)"
        )
        if not ok:
            return
        request = ResolutionRequest(app, selected_reader(app), directory, root_name)
        _requests.append(request)

        def complete():
            if request.closed:
                return
            if not request.future.done():
                QTimer.singleShot(50, complete)
                return
            try:
                result = request.apply()
                QMessageBox.information(
                    None, "PostProject", str(result or "Obsolete result discarded")
                )
            except RuntimeError as error:
                QMessageBox.warning(None, "PostProject", str(error))
            finally:
                request.close()
                if request in _requests:
                    _requests.remove(request)

        QTimer.singleShot(0, complete)
    except (RuntimeError, ValueError) as error:
        QMessageBox.warning(None, "PostProject", str(error))
