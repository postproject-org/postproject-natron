// SPDX-License-Identifier: GPL-2.0-or-later
#include "adapter.h"
#include <Python.h>
#include <filesystem>
#include <functional>
#include <memory>

namespace {
using namespace pilot;
struct OwnedResolution {
  std::string production;
  std::string binding;
  Resolution value;
};

// No Python or Natron object is accessed while native I/O runs without the GIL.
class AllowThreads {
public:
  AllowThreads() : state_(PyEval_SaveThread()) {}
  ~AllowThreads() { PyEval_RestoreThread(state_); }

private:
  PyThreadState *state_;
};
template <class F> auto native(F operation) {
  AllowThreads released;
  return operation();
}

PyObject *guard(const std::function<PyObject *()> &operation) {
  try {
    return operation();
  } catch (const std::bad_alloc &) {
    return PyErr_NoMemory();
  } catch (const std::exception &error) {
    PyErr_SetString(PyExc_RuntimeError, error.what());
    return nullptr;
  }
}

std::string uuid_text(const Uuid &uuid) {
  constexpr char hex[] = "0123456789abcdef";
  std::string text;
  for (std::size_t i = 0; i != uuid.bytes().size(); ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10)
      text += '-';
    const auto byte = uuid.bytes()[i];
    text += hex[byte >> 4];
    text += hex[byte & 15];
  }
  return text;
}

PyObject *failure(const Error &error) {
  PyObject *details =
      Py_BuildValue("{s:i,s:s}", "code", static_cast<int>(error.code()),
                    "message", error.message().c_str());
  if (!details)
    return nullptr;
  if (const auto *conflict = error.transactionConflict()) {
    const auto base = conflict->base_revision_id
        ? std::optional<std::string>(uuid_text(*conflict->base_revision_id))
        : std::nullopt;
    PyObject *value = Py_BuildValue(
        "{s:i,s:s,s:z,s:K,s:s,s:K}", "kind",
        static_cast<int>(conflict->key.kind), "target_id",
        uuid_text(conflict->key.target_id).c_str(), "base_revision_id",
        base ? base->c_str() : nullptr, "base_sequence",
        static_cast<unsigned long long>(conflict->base_revision_sequence),
        "superseding_revision_id",
        uuid_text(conflict->superseding_revision_id).c_str(),
        "superseding_sequence",
        static_cast<unsigned long long>(
            conflict->superseding_revision_sequence));
    if (!value || PyDict_SetItemString(details, "conflict", value) < 0) {
      Py_XDECREF(value);
      Py_XDECREF(details);
      return nullptr;
    }
    Py_DECREF(value);
  }
  if (details) {
    PyErr_SetObject(PyExc_RuntimeError, details);
    Py_DECREF(details);
  }
  return nullptr;
}

bool sequence_args(PyObject *args, std::string &path,
                   ImageSequenceInput &sequence, std::string *reader = nullptr,
                   std::string *selected = nullptr) {
  const char *production, *directory, *prefix, *suffix, *reader_id = "",
                                                        *choice = "";
  int padding;
  long long start, end;
  unsigned numerator, denominator;
  const char *format = reader ? "ssssiLLIIss" : "ssssiLLII";
  if (!PyArg_ParseTuple(args, format, &production, &directory, &prefix, &suffix,
                        &padding, &start, &end, &numerator, &denominator,
                        &reader_id, &choice))
    return false;
  if (padding < 1 || padding > 20 || numerator == 0 || denominator == 0) {
    PyErr_SetString(PyExc_ValueError,
                    "Invalid sequence padding or rational rate");
    return false;
  }
  path = production;
  sequence = {directory,   {prefix, suffix, static_cast<std::uint8_t>(padding)},
              start,       end,
              1,           numerator,
              denominator, {}};
  if (reader)
    *reader = reader_id;
  if (selected)
    *selected = choice;
  return true;
}

PyObject *candidates_py(PyObject *, PyObject *args) {
  return guard([&]() -> PyObject * {
    std::string path;
    ImageSequenceInput sequence{};
    if (!sequence_args(args, path, sequence))
      return nullptr;
    const auto result = native([&] { return candidates(path, sequence); });
    if (!result)
      return failure(result.error());
    PyObject *list = PyList_New(0);
    if (!list)
      return nullptr;
    for (const auto &binding : *result) {
      PyObject *value = PyUnicode_FromString(binding.c_str());
      if (!value || PyList_Append(list, value) < 0) {
        Py_XDECREF(value);
        Py_DECREF(list);
        return nullptr;
      }
      Py_DECREF(value);
    }
    return list;
  });
}

PyObject *associate_py(PyObject *, PyObject *args) {
  return guard([&]() -> PyObject * {
    std::string path, reader, selected;
    ImageSequenceInput sequence{};
    if (!sequence_args(args, path, sequence, &reader, &selected))
      return nullptr;
    const auto result =
        native([&] { return associate(path, sequence, selected, reader); });
    return result ? PyUnicode_FromString(result->c_str())
                  : failure(result.error());
  });
}

void release_resolution(PyObject *capsule) {
  delete static_cast<OwnedResolution *>(
      PyCapsule_GetPointer(capsule, "pilot.Resolution"));
}

PyObject *resolve_py(PyObject *, PyObject *args) {
  return guard([&]() -> PyObject * {
    const char *path, *binding, *directory, *root = "";
    if (!PyArg_ParseTuple(args, "sss|s", &path, &binding, &directory, &root))
      return nullptr;
    auto result =
        native([&] { return resolve(path, binding, directory, root); });
    if (!result)
      return failure(result.error());
    auto owner = std::make_unique<OwnedResolution>(
        OwnedResolution{path, binding, *std::move(result)});
    PyObject *capsule =
        PyCapsule_New(owner.get(), "pilot.Resolution", release_resolution);
    if (capsule)
      owner.release();
    return capsule;
  });
}

PyObject *details_py(PyObject *, PyObject *args) {
  return guard([&]() -> PyObject * {
    PyObject *capsule;
    if (!PyArg_ParseTuple(args, "O", &capsule))
      return nullptr;
    const auto *owner = static_cast<OwnedResolution *>(
        PyCapsule_GetPointer(capsule, "pilot.Resolution"));
    if (!owner)
      return nullptr;
    PyObject *choices = PyList_New(0), *missing = PyList_New(0);
    if (!choices || !missing) {
      Py_XDECREF(choices);
      Py_XDECREF(missing);
      return nullptr;
    }
    for (const auto &candidate : owner->value.candidates) {
      if (!candidate.sequence_naming)
        continue;
      const auto &naming = *candidate.sequence_naming;
      const auto directory = locatorFilePath(candidate.uri);
      if (!directory) {
        Py_DECREF(choices);
        Py_DECREF(missing);
        return failure(directory.error());
      }
      const auto pattern =
          (std::filesystem::path(*directory) /
           (naming.prefix + std::string(naming.padding, '#') + naming.suffix))
              .string();
      PyObject *value = Py_BuildValue(
          "{s:s,s:s,s:s,s:s,s:i}", "pattern", pattern.c_str(), "directory",
          directory->c_str(), "prefix", naming.prefix.c_str(), "suffix",
          naming.suffix.c_str(), "padding", naming.padding);
      if (!value || PyList_Append(choices, value) < 0) {
        Py_XDECREF(value);
        Py_DECREF(choices);
        Py_DECREF(missing);
        return nullptr;
      }
      Py_DECREF(value);
    }
    for (const auto &issue : owner->value.issues) {
      for (const auto frame : issue.frames) {
        PyObject *value = PyLong_FromLongLong(frame);
        if (!value || PyList_Append(missing, value) < 0) {
          Py_XDECREF(value);
          Py_DECREF(choices);
          Py_DECREF(missing);
          return nullptr;
        }
        Py_DECREF(value);
      }
    }
    PyObject *locators = PyList_New(0);
    if (!locators) {
      Py_DECREF(choices);
      Py_DECREF(missing);
      return nullptr;
    }
    for (const auto &locator : owner->value.locators) {
      PyObject *value = PyUnicode_FromString(locator.locator.uri.c_str());
      if (!value || PyList_Append(locators, value) < 0) {
        Py_XDECREF(value);
        Py_DECREF(locators);
        Py_DECREF(choices);
        Py_DECREF(missing);
        return nullptr;
      }
      Py_DECREF(value);
    }
    return Py_BuildValue("{s:i,s:N,s:N,s:N}", "availability",
                         static_cast<int>(owner->value.availability),
                         "candidates", choices, "missing_frames", missing,
                         "recorded_locators", locators);
  });
}

PyObject *confirm_py(PyObject *, PyObject *args) {
  return guard([&]() -> PyObject * {
    PyObject *capsule;
    if (!PyArg_ParseTuple(args, "O", &capsule))
      return nullptr;
    const auto *owner = static_cast<OwnedResolution *>(
        PyCapsule_GetPointer(capsule, "pilot.Resolution"));
    if (!owner)
      return nullptr;
    const auto result = native([&] {
      return confirm(owner->production, owner->binding, owner->value, 0);
    });
    if (!result)
      return failure(result.error());
    Py_RETURN_NONE;
  });
}

PyObject *verify_py(PyObject *, PyObject *args) {
  return guard([&]() -> PyObject * {
    const char *path, *binding, *directory, *prefix, *suffix;
    int padding;
    if (!PyArg_ParseTuple(args, "sssssi", &path, &binding, &directory, &prefix,
                          &suffix, &padding))
      return nullptr;
    if (padding < 1 || padding > 20) {
      PyErr_SetString(PyExc_ValueError, "Invalid padding");
      return nullptr;
    }
    const auto result = native([&] {
      return verify(path, binding, directory,
                    {prefix, suffix, static_cast<std::uint8_t>(padding)});
    });
    return result ? PyLong_FromLong(static_cast<int>(*result))
                  : failure(result.error());
  });
}

PyObject *refresh_py(PyObject *, PyObject *args) {
  return guard([&]() -> PyObject * {
    const char *path, *binding;
    unsigned long long after;
    if (!PyArg_ParseTuple(args, "ssK", &path, &binding, &after))
      return nullptr;
    const auto result = native([&] { return refresh(path, binding, after); });
    if (!result)
      return failure(result.error());
    return Py_BuildValue("{s:K,s:K,s:s}", "through",
                         static_cast<unsigned long long>(result->through),
                         "events",
                         static_cast<unsigned long long>(result->events),
                         "name", result->asset_name.c_str());
  });
}

PyMethodDef methods[] = {
    {"candidates", candidates_py, METH_VARARGS, "Bounded naming-aware lookup."},
    {"associate", associate_py, METH_VARARGS, "Explicit adoption or import."},
    {"resolve", resolve_py, METH_VARARGS, "Read-only sequence resolution."},
    {"details", details_py, METH_VARARGS, "Copy host-bound result data."},
    {"confirm", confirm_py, METH_VARARGS,
     "Confirm an unambiguous current decision."},
    {"verify", verify_py, METH_VARARGS, "Verify selected sequence content."},
    {"refresh", refresh_py, METH_VARARGS,
     "Read a bounded revision page and events."},
    {nullptr, nullptr, 0, nullptr}};
PyModuleDef module = {PyModuleDef_HEAD_INIT,
                      "_postproject_natron",
                      "Native Reader adapter.",
                      -1,
                      methods,
                      nullptr,
                      nullptr,
                      nullptr,
                      nullptr};
} // namespace

PyMODINIT_FUNC PyInit__postproject_natron() { return PyModule_Create(&module); }
