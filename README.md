# PostProject Reader pilot for Natron

An optional Reader association action for **Natron 2.5.0** and installed
**PostProject 0.7.0-alpha.1 development SDK** (C ABI 38, schema 17). The C++17 adapter performs
production operations through the installed header-only wrapper. A small
CPython extension exposes owning values to Natron's existing scripting seam;
Python handles menus and Reader parameters. It does not use PostProject's
Python binding inside Natron, whose bundled Python is 3.10.

The [per-target brief](BRIEF.md) records the checked source seam, selection,
ownership, family targets and acceptance boundary.

## Actions and scope

Select one image-sequence Reader. **Associate Reader** asks for an explicit
production, frame range and existing representation when lookup finds one.
Two sequences in one directory remain separate candidates by naming. An
unchosen existing candidate prevents import. The Reader's persistent hidden
`postprojectAssociation` parameter stores the canonical representation binding,
production pathname, local Reader identifier and revision cursor. Its native
filename remains the playback fallback.

**Resolve Reader** asks for a directory and optional logical media-root name.
It copies the Reader association and filename before starting one worker.
Presence resolution reports missing required frames; complete media then gets
content verification. Only an online, unambiguous result is eligible to change
the Reader. The C++ adapter confirms a new locator using the retained decision
base. Already recorded locations need no mutation. Partial/ambiguous results
show their status and leave the filename unchanged.

**Refresh Reader** reads up to 100 revisions and their events after the retained
cursor and displays the selected asset name. Repeat to catch up. **Verify
Reader** explicitly checks current sequence content; it does not record a new
fingerprint. **Cancel resolution** cancels pending work and joins running work.
Closing the GUI invokes the same cleanup.

Native I/O releases the GIL and uses copied strings; workers never access Natron
objects. The host thread discards results after cancellation, node deletion,
or a changed filename/association. A transient request-generation parameter
also rejects a replacement node reusing the script name or a superseding
request. Application looks up the current Reader and never accesses a retained
wrapper after native work. A revision base is acquired before reads
and checked again afterward. Confirmation uses that base, never a later
writer's revision. A structured conflict closes its transaction; refresh and
make a new decision. No automatic retry, media merge, snapshot isolation or
remote-production behavior is promised.

## Build against an installed package

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/absolute/postproject/install
cmake --build build
ctest --test-dir build --output-on-failure
```

Requirements: CMake 3.21, C++17, Python development headers for 3.10 or later,
and the installed PostProject C/C++ package. The module uses the Python stable
ABI with a 3.10 floor. The accepted Linux build used Python 3.14 headers and
loaded in Natron's bundled 3.10 interpreter. Installed consumers invoke no Cargo.

Development option factories/setters propagate errors immediately. Structured
conflicts expose an empty-journal base as Python `None`. Final candidate host
qualification remains pending; the accepted renderer evidence below describes
the released 0.6 pilot.

For the GUI, place `plugin/initGui.py` and `plugin/postproject_reader.py` in
Natron's user plugin directory, and place `_postproject_natron.so` on its Python
module path. Make `libpostproject.so` available to the dynamic loader, for
example through the installed package's library directory. Natron 2.5 uses Qt4;
the menu bridge uses its bundled `qtpy` abstraction. Removing these files
removes the actions. Missing native dependencies disable menu registration;
the ordinary Reader and its saved filename remain usable. An unavailable
production produces a warning and leaves the association and filename intact.

## Reproduce the accepted host paths

Use the official `Natron-2.5.0-Linux-x86_64-no-installer.tar.xz` runtime from the
upstream v2.5.0 release. `UPSTREAM` pins the corresponding source revision.
With PostProject's maintained Python binding installed in an external Python
3.11+ environment (fixture preparation and independent readback only):

```sh
python tests/run.py /tmp/natron-evidence \
  --renderer /absolute/Natron-2.5.0-Linux-x86_64-no-installer/bin/natronrenderer \
  --library /absolute/postproject/install/lib/libpostproject.so
```

The root must not exist. The runner makes genuine PNG sequences with frames
1001–1003 and four-digit padding, imports them through an independent public
producer, and executes the same Reader action helpers inside Natron. It checks
adoption, named-root move, verification, revision refresh, save/reopen, offline
production fallback and preservation of unknown metadata. Negative paths cover
missing frame 1002, two equally supported directories, cancellation, obsolete
Reader filename, owning copied results, and an independent writer invalidating
a new-locator decision. Both runs decode a real frame through Natron's Reader
and Writer and require exit status zero. A third process reopens and decodes
the saved Reader without importing the native adapter.

`tests/contract.cpp` is the installed C++ recipe for ordinary success, rollback,
an empty production without a base, partial refusal, a known-locator no-op and
stale-decision cleanup. Its byte fixtures test identity rather than decoding.
Real-host logs and separate normal/negative symbol-only traces stay in the
requested evidence directory. No full upstream rebuild or interactive GUI
acceptance is claimed by the automated rendering run.

## Source checks and limits

Pinned source: `Engine/AppManager.cpp` initializes embedded Python;
`Engine/PyParameter.cpp` exposes persistent user string parameters;
`Engine/PyNode.cpp` exposes Reader parameters; `Gui/PyGuiApp.cpp` and
`Gui/GuiApplicationManager.cpp` connect user menus. No OpenFX effect hook,
host patch, internal PostProject header or database access is used.

This pilot covers one compact, contiguous sequence and explicit manual refresh.
It does not register renders or implement job workers. Multiple simultaneous
menu requests are limited to one joined worker; cancellation may wait for fingerprint
I/O already running. Latencies are informational, not hard CI budgets. Only
the pinned Linux host/runtime route is accepted. All adapter sources are
licensed GPL-2.0-or-later; the experiment does not imply upstream endorsement.
