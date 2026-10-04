"""Host lifetime checks independent of rendering and the native module."""

import importlib.util
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch


class Parameter:
    def __init__(self, value=""):
        self.value = value

    def get(self):
        return self.value

    def set(self, value):
        self.value = value

    def setPersistent(self, persistent):
        self.persistent = persistent

    def setVisible(self, visible):
        pass


class Reader:
    def __init__(self):
        self.parameters = {
            "filename": Parameter("plate.####.png"),
            "postprojectAssociation": Parameter(
                '{"production": "test.pproj", "binding": "test-binding"}'
            ),
        }
        self.deleted = False

    def getScriptName(self):
        return "Reader1"

    def getParam(self, name):
        if self.deleted:
            raise AssertionError("Accessed a deleted host object")
        return self.parameters.get(name)

    def createStringParam(self, name, label):
        self.parameters[name] = Parameter()
        return self.parameters[name]

    def refreshUserParamsGUI(self):
        pass


class RequestTests(unittest.TestCase):
    def setUp(self):
        self.native = Mock()
        self.native.details.return_value = {
            "availability": 1,
            "candidates": [{"pattern": "moved/plate.####.png"}],
        }
        source = Path(__file__).parents[1] / "plugin/postproject_reader.py"
        spec = importlib.util.spec_from_file_location("request_bridge", source)
        self.bridge = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, {"_postproject_natron": self.native}):
            spec.loader.exec_module(self.bridge)
        self.reader = Reader()
        self.app = SimpleNamespace(getNode=lambda name: self.reader)

    def request(self):
        request = self.bridge.ResolutionRequest(self.app, self.reader, "moved")
        request.future.result()
        self.addCleanup(request.close)
        return request

    def test_deleted_reader_with_reused_name_is_discarded(self):
        request = self.request()
        self.reader.deleted = True
        self.reader = Reader()
        self.assertIsNone(request.apply())
        self.native.confirm.assert_not_called()

    def test_new_request_supersedes_older_generation(self):
        old = self.request()
        current = self.request()
        self.assertIsNone(old.apply())
        self.native.confirm.assert_not_called()
        self.assertIsNotNone(current.apply())
        self.native.confirm.assert_called_once()


if __name__ == "__main__":
    unittest.main()
