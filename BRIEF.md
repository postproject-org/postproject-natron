# Natron per-target brief

Selected for native sequence consumption and C++ Result propagation against
PostProject `0.6.0-alpha.1`, C ABI 37 and schema 17. Source checked against
Natron `2.5.0`, commit `53ee17e34438005425230468916ff223cfd8a403`, pinned in
[UPSTREAM](UPSTREAM). The accepted runtime is the official Linux x86_64
no-installer package with embedded CPython 3.10 and Qt4.

## Current behavior and source seam

Readers retain a filename/pattern in their normal host parameters.
`Engine/PyNode.cpp` exposes Reader operations;
`Engine/PyParameter.cpp` exposes persistent user string parameters.
`Engine/PyAppInstance.cpp` supplies project save/load, Reader/Writer creation
and render actions. `Engine/AppManager.cpp` initializes embedded Python.
`Gui/PyGuiApp.cpp` and `Gui/GuiApplicationManager.cpp` supply user menu hooks.

The actual native seam is a CPython extension loaded by that interpreter.
`src/module.cpp` uses the Python stable ABI with a 3.10 floor and calls the
C++17 adapter in `src/association.cpp` and `src/resolution.cpp`. Python owns
menus and Reader parameters. PostProject's Python binding is used externally
for fixtures/readback only. No OpenFX project-management hook is assumed.

## User action and smallest integration

The user selects one contiguous image-sequence Reader and a production.
Naming-aware lookup distinguishes patterns in the same directory. The user
explicitly selects an existing representation or imports when none exists.
The hidden persistent `postprojectAssociation` parameter stores the canonical
binding, production path, local Reader identifier and revision cursor.
The native filename remains the playback fallback.

Resolve Reader uses a selected directory or logical root mapping. Required
frame presence precedes content verification. Only a complete, unambiguous
result can be confirmed and applied. Refresh Reader reads a bounded durable
revision page; Verify Reader checks content without recording a fingerprint.
These actions do not own render jobs.

Normal-path targets are sequence adoption/inspection, bounded locator reads,
verification, host binding, revision feed and every member of
`cpp-result-propagation`: `Result<T>`, `POSTPROJECT_TRY` and
`POSTPROJECT_TRY_ASSIGN`. Native C++ calls supply evidence beyond Blender's
Python sequence route. Partial, ambiguous and conflict cases have separate
traces and do not manufacture normal-path family counts.

## Ownership, scheduling and consistency

Native I/O releases the GIL and uses copied values. Workers never access Natron
objects; the host thread owns parameter changes. Requests retain association
and filename values and discard obsolete or cancelled results. A nonpersistent
generation parameter rejects replacements and superseding requests without
dereferencing an old Reader wrapper. One joined
worker bounds concurrent work. Cancellation can wait for native fingerprinting.

Resolution takes a revision base before reading and checks it afterward.
Confirmation uses that retained base. A known locator is a read-only no-op;
a new locator is an explicit transaction. A conflict closes the transaction
and preserves the Reader fallback. The caller refreshes and makes a new
decision. A base revision is neither a read snapshot nor a commit receipt.

C++ RAII releases native handles. The extension returns owning Python values;
copied strings survive destruction of their native result owner. A typed
result capsule retains the production/binding together with its decision.

## Build, removal and acceptance

The adapter consumes the installed CMake package and C++17 header without Cargo.
The accepted module was built using Python 3.14 headers and loaded by Natron's
3.10 interpreter. [README](README.md#build-against-an-installed-package) gives
build and plugin installation commands.

Removing the extension/menu disables these actions. Saved Readers still use
their ordinary filenames, including when the production is unavailable.
The association is inert host data, not a replacement project format.

`tests/run.py` creates genuine PNG sequences 1001–1003 with four-digit padding
and two patterns in one directory. Real Reader/Writer runs cover adoption,
unknown metadata, a root move, verification, refresh, save/reopen and offline
fallback. Negative paths cover a missing frame, two candidate directories,
cancellation, obsolete requests, owning copied values and an independent writer
invalidating a retained locator decision. A separate process reopens and
decodes without loading the adapter.

`tests/contract.cpp` tests the same installed C++ adapter's success, rollback,
empty-production/no-base case, partial refusal and known-locator no-op.
The [reproduction commands](README.md#reproduce-the-accepted-host-paths) produce
separate normal and negative evidence. Automated renderer acceptance does not
claim an interactive GUI pass.

A native module that cannot load in the pinned interpreter is a blocker.
This accepted route supplies native compound-media and projection evidence;
render registration, job workers and platforms beyond the pinned Linux runtime
remain outside this slice.
