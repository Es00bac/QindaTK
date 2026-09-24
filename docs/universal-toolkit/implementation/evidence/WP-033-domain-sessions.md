<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-033 / WP-040 — Domain document sessions, persistence, recovery and undo

**State: lane report for Sol (not accepted).** This closes the domain-session
gap recorded in [WP-033](WP-033.md) and [WP-040](WP-040.md): sessions,
history, atomic save, recovery, workspace restore and format import/export
now work through a model/codec boundary. `CanvasDocument` is the first
backend that is not a `PropertyDocument`. The contract is in
[document-models.md](../../../document-models.md), the canvas side is in
[canvas.md](../../../canvas.md#persistence), and the decision is
[D-046](../../../decisions-application.md#d-046--compose-domain-documents-through-a-model-codec-and-one-history-engine)
(provisional number).

Branch `work/wp033-domain-opus` in worktree `QindaTK-wp033-domain-opus`,
based on integration `e106c36`. Build directories: `build/wp033d` (Qt 6.11.1,
Debug, Ninja) and `build/wp033d-qt68` (isolated Qt 6.8.3 SDK). Builds used
the user's run-local limit, `cmake --build <dir> --parallel 4`.

## What is delivered

| Requirement | Delivered |
| --- | --- |
| Model adapter and codec registry | `DocumentModel` (identity, kind, header, change hook, `prepareCommand`/`publishCommand`/`composeCommands`), `DocumentCommand`, `DocumentCodec` (schema identity, content recognition, bounded `inspect`/`decode` on any thread, `create`/`adopt`/`capture` on the GUI thread), thread-safe `DocumentCodecRegistry` with the reference and canvas codecs. `DocumentSession` holds a model and codec; `document` is null for a canvas and generic code uses `model`/`kind`/`schemaId`/`capture()`. The reference v1/v2 JSON codec, `PropertyDocument`, `EditTransaction` and the session/manager APIs are source compatible (the existing suites and consumers pass unchanged). |
| Shared history | `UndoService` keeps one history per document UUID for any model. Commits prepare, record, then publish; rejected commits, undo and redo change nothing and emit nothing. One live history per model: `addModel` refuses a second service and `adoptModel` moves it. A standalone canvas keeps its 256-step behaviour on the same engine (private service), and a session moves those steps into the shared service. Entry IDs stay stable across merges; a merge counter protects save marks. |
| Canvas history and native format | Canvas steps are the applied `CanvasEdit`: touched-object deltas plus layer, image (shared `CanvasTiledImagePtr`) and page deltas. Page, layer and image changes are recorded and bump the revision; `addImage` previously changed content without one. Commits reject duplicate targets, stale `before` states, stale reset generations, missing layer/image references, removing a referenced image or non-empty/last layer, non-finite geometry and exceeded limits. The native binary format round-trips pages, layers, objects, tiled images (exact premultiplied pixels), 64-bit IDs and counters, revision and reset generation, and validates everything before adoption. `CanvasView`, `ToolController` and the render APIs are unchanged. |
| Save and recovery | GUI capture (O(1) for a canvas) → worker encode, recovery append and `QSaveFile` replacement (no direct-write fallback) → GUI receipt. The receipt must match the capture; a stale one marks clean only the captured history position and keeps recovery. Version-2 recovery frames carry the schema and the complete header under a header hash; version-1 reference frames stay readable. Bad hashes/headers, unavailable codecs and oversized decoded resources fail without touching the files. Workspace records carry `schemaId` and restore through `openDocument`; `FormatRegistry` exports sessions through `captureSnapshot()`, adopts imports through `DocumentManager::adoptBytes`, and refuses snapshots whose schema a provider does not list. |
| Mixed application proof | `tst_domain_session`, `tst_canvas_persistence`, `tst_canvas_session.qml` and the installed `tests/consumer/domain_sessions` fixture (below). |

## Exact commands

```sh
git -C /home/cabewse/work_SPaC3/QindaTK worktree add -b work/wp033-domain-opus \
    /home/cabewse/work_SPaC3/QindaTK-wp033-domain-opus e106c3615280d945be4d7ca61c1d2d2034198121
cd /home/cabewse/work_SPaC3/QindaTK-wp033-domain-opus
W=$PWD
# Qt 6.11.1
cmake -S . -B build/wp033d -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DQINDATK_QML_INSTALL_DIR=$W/build/wp033d-stage/qml
cmake --build build/wp033d --parallel 4
ctest --test-dir build/wp033d --output-on-failure
cmake --build build/wp033d --parallel 4 --target all_qmllint
cmake --install build/wp033d --prefix $W/build/wp033d-stage
for c in domain_sessions persistence editing workspace wp040; do
  cmake -S tests/consumer/$c -B build/consumer-$c-qt611 -G Ninja \
        -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=$W/build/wp033d-stage
  cmake --build build/consumer-$c-qt611 --parallel 4
done
# run each consumer with LD_LIBRARY_PATH=$W/build/wp033d-stage/lib64,
# QT_QPA_PLATFORM=offscreen and QT_QUICK_BACKEND=software
# Qt 6.8.3 (isolated SDK)
SDK=/home/cabewse/work_SPaC3/QindaTK-universal/build/qt68-sdk/6.8.3/gcc_64
cmake -S . -B build/wp033d-qt68 -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_PREFIX_PATH=$SDK -DQt6_DIR=$SDK/lib/cmake/Qt6 \
      -DQINDATK_QML_INSTALL_DIR=$W/build/wp033d-qt68-stage/qml
cmake --build build/wp033d-qt68 --parallel 4
ctest --test-dir build/wp033d-qt68 --output-on-failure
cmake --build build/wp033d-qt68 --parallel 4 --target all_qmllint
# base comparison for the GPU flake (the base commit's own test binary)
mkdir -p build/base-src && git archive e106c36 | tar -x -C build/base-src
cmake -S build/base-src -B build/base-build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/base-build --parallel 4 --target tst_canvas_render
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=rhi build/base-build/tests/tst_canvas_render sceneGraphBackend
# focused
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QML_IMPORT_PATH=build/wp033d/qml \
    build/wp033d/tests/tst_canvas_persistence
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QML_IMPORT_PATH=build/wp033d/qml \
    build/wp033d/tests/tst_domain_session
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QML_IMPORT_PATH=build/wp033d/qml \
    build/wp033d/tests/tst_qml -input tests/qml/tst_canvas_session.qml
```

## Results

| Suite | Result | Contract risk covered |
| --- | --- | --- |
| `tst_canvas_persistence` | 9/9 (7 cases) | Exact roundtrip (layers, a layer name with a leading U+FEFF and an object name with an unpaired surrogate, locked layer, partial edge tiles with alpha 128, ID 2^60+3, z 2^55+7, counters, revision, reset generation, extensions; byte-identical re-encode). Decoder rejects a duplicate ID, an ID beyond its counter, dangling image and layer references, a NaN transform, a 70,000 px image before any tile allocation, an incomplete tile grid, a decoded-pixel budget, a future version, a checksum change and truncation, each with its field path. Seven rejected commits (duplicate target, dangling image, removing a referenced image, stale reset generation, infinite geometry, stale `before`, last layer) and one refused undo leave encoded state, revision, step count, undo text and every damage/edit/reset/change/history signal untouched. Page, layer and image mutations are recorded and undone exactly (image pointer identity restored). Declared QML initial values create no undo steps. A macro whose edits cancel out records no step, and cancelling one closes it. The commit-time file budget refuses the edit that would make a canvas unsaveable; the fullest accepted canvas saves with the largest allowed extensions and reopens. A 30,000 × 30,000 image declaration fails on its pixel budget before tiles are reserved. |
| `tst_domain_session` | 7/7 (5 cases) | Property and canvas sessions together: independent undo through one facade, Save As, Save All, clean close and reopen with byte-identical canvas state, workspace capture/save/load/restore of both kinds with stable views. The one-live-history rule and the standalone → session history move. Canvas recovery after an external writer replaces the file: save refused, recovery offered, restored in a new manager with equal content, foreign bytes not a baseline, Save As clears recovery. A damaged newest frame, no canvas codec, and a tight decoded-pixel limit each fail while the recovery file sizes stay identical; a version-1 reference frame restores and version-2 frames append after it. A stale receipt marks exactly the captured position (undo to it is clean, one step further is dirty), and a reset during a save stays dirty with recovery kept. The reference and text providers refuse the canvas schema, and the canvas provider exports, detects by signature, imports and adopts it. |
| `tst_qml` → `tst_canvas_session.qml` | 3/3 | `manager.createDocument("qindatk.canvas-document.v1")`, `session.model` bound to `Tk.CanvasView` and `Tk.ToolController`, a drawn rectangle committed, `manager.undoService.activeModel` and undo from QML. |
| Existing suites | unchanged and passing | `tst_document_session`, `tst_edit_transaction` (stable merged entry IDs), `tst_format_registry` (built-in providers unchanged), `tst_workspace_session`, canvas core/tools/render/input suites. |

**Qt 6.11.1 gate:** the full build succeeded with no new warnings (only the
existing `-Wmissing-field-initializers` warnings in `tst_workspace_session.cpp`
and `format_registry.cpp`). After the review fixes below, `ctest` passed
**98/98** in 184.3 s: the 96 integration tests plus the two new C++ suites;
the QML case runs inside `tst_qml`. `all_qmllint` exited 0 before the fixes,
and the fixes change no QML file or registered property; the regenerated
catalog is identical. Its 616 warning lines are the existing
unqualified-access warnings in module QML; none of them names a type changed
here.

`tst_canvas_render_rhi` is flaky on this host. It failed in two of the four
full runs by timing out on the first GPU pixel; in a failing run the whole
grabbed frame is empty (alpha 0 at the corner, where a good frame shows the
canvas background). Built from `e106c36` itself with the same flags, the case
passed **6 of 12** interleaved runs, against **7 of 12** for this branch. The
flake therefore predates this branch. A scratch replica shows the same
empty frames with the integration library too. The newer integration build
after `debd004`, which changes how scene-graph tiles are built, passed 12 of
12.

**Qt 6.8.3 gate (isolated SDK):** the full build (840 steps, 16 min 35 s at
`--parallel 4` on a host at load 65) produced only the existing warnings.
`ctest` passed **98/98** in 108.9 s, and again (**98/98**, 139.7 s) after an
incremental rebuild with the review fixes. `all_qmllint` exited 0. Its 1,729
warning lines are Qt 6.8 lint limitations, such as `ThemeMetrics` property
maps and positioner attached types. None of them names a property this
branch adds.

**Installed consumers (Qt 6.11 stage):** the new `domain_sessions` consumer
passed: property and canvas sessions, shared undo, save, reopen, conflict
recovery and the QML session model. The existing `persistence`, `editing`,
`workspace` and `wp040` consumers passed unchanged. Sol's in-progress
`tests/consumer/wp033` and `examples/workbench` (`--check`), built read-only
from the integration checkout into this lane's build directories, also
passed against this stage. The same seven projects also built and passed against
the Qt 6.8 stage. All fourteen runs were repeated after the review fixes,
against reinstalled stages, running with `LD_LIBRARY_PATH` set to the stage and the
SDK and `QT_PLUGIN_PATH` set to the SDK plugins. `ldd` confirmed that the SDK
`libQt6Core` and this lane's `libqindatk` were loaded.

## Measured bounds

- **One-object gesture in a large canvas** (`oneObjectGestureInLargeCanvasRetainsOnlyTheDelta`).
  The canvas holds 100,000 objects and encodes to 19,312,455 bytes. The test
  ran 300 select-tool move gestures in a session under the default 8 MiB
  budget. Each gesture took 0.11 ms (Debug), and 256 steps (the history
  limit) retain 153,600 bytes, about 600 bytes per step. One whole-scene
  snapshot is larger than the whole budget.
- **Capture versus encode** (scratch benchmark against the Debug library at
  host load 65; noisy). Capture: 0.003 ms for 10,000 objects plus a
  2048×1536 image, and 0.005 ms for 100,000 objects. Encode on the worker:
  151–646 ms (14.5 MB) and 0.5–2.4 s (19.3 MB). Decode followed by adopt took
  0.6 s + 0.5 s and 3.8 s + 3.2 s. Adopt matches the existing fixture loader
  (0.3 s and 3.3 s), which does the same per-object placement and spatial
  indexing.
- **Decompression bound.** A probe showed that `qUncompress` ignores the size
  its header declares: a 36,618-byte blob declaring 1,024 bytes inflated to
  8,388,608. Canvas tiles are therefore stored raw, and every decoded
  allocation is charged against `CanvasDocumentLimits` before it is made.

## Independent review

A read-only review of the new engine, canvas, format, recovery and save code
found five defects. All five are fixed and guarded:

| Finding | Fix | Regression |
| --- | --- | --- |
| A canvas macro whose edits cancel out recorded an empty step that could be neither undone nor cancelled | `DocumentCommand::isNoOp()`: no-op steps are never recorded, cancelling such a macro closes it, and a merge that nets to nothing keeps both steps | `groupedEditsThatCancelOutLeaveNoStep` |
| Names were UTF-8; `QString::fromUtf8` drops a leading U+FEFF and an unpaired surrogate (probed), so such a name saved but never reopened | Names are UTF-16 code units | `nativeRoundtripIsExact` |
| The per-kind commit limits allowed canvases larger than the encoded-file limit (for example 2,000,000 objects at 177+ bytes each) | Exact encoded size tracked incrementally; an edit that grows the file past the limit, less the extensions reserve, is refused | `encodedBudgetKeepsEveryCommitSaveable` |
| The decoder reserved an image's tile list before charging its declared pixels | The declared size is charged first | `decoderRejectsInvalidFilesBeforeAdoption` ("declared pixels before tiles") |
| A binding reading the canvas history during a transfer could re-attach it to its standalone service, leaving two live histories (low confidence) | `adoptModel` moves the history with no signal between release and attach, then notifies both services. Canvas history accessors are null-safe | covered by the transfer assertions in `tst_domain_session` |

The review found nothing substantive in the undo engine's bookkeeping,
recovery frame parsing and locking, the save worker's thread use, or the
manager's identity and baseline checks.

## Visual changes

None. No module QML control or visual changed, so no screenshots were
needed. The QML contract is covered by `tst_canvas_session.qml` and the
installed consumer.

## Unsupported or open

- `DocumentManager::openDocument` and `restoreRecovered` still decode and
  adopt synchronously on the GUI thread. Opening 100,000 objects took several
  seconds in Debug under load. `decode` is thread-safe, so an asynchronous
  open is possible behind the same API, but it is not wired up.
- Save and autosave write full snapshots (debounced), not deltas. Canvas
  tiles are uncompressed. `autosaveNow()` and a `FormatRegistry` session
  export encode on the GUI thread, because the export preflight hashes the
  bytes it approves. Explicit and timed saves encode on a worker.
- No workbook or rich-text adapter is claimed. [Adding a domain](../../../document-models.md#adding-a-domain)
  lists the steps those lanes' local histories need.
- `GestureRouter` still targets `PropertyDocument` transactions. The canvas
  `CanvasNativeFormat` provider is opt-in, so the built-in registry and its
  dialogs are unchanged.
- Recovery frames written by this version are version 2 and are not readable
  by older builds. The canvas active layer is editor state and is not saved.

## Integration notes for Sol

- Other lanes also touch these files: `src/CMakeLists.txt` and
  `tests/CMakeLists.txt` (appended sources and tests), `docs/index.md` (one
  row), `docs/decisions-application.md` (D-046 is provisional; the timeline
  lane also claims D-045, which collides with the chart D-045), and
  `docs/controls.md` (the `CanvasDocument` row). Regenerate `docs/catalog.json`
  after merging; this branch changes only the entries for the types it
  touches.
- `WP-033.md`, `WP-040.md`, `plan.json` and the ledger were not edited.
  WP-040's "WP-033 dependency open" statement and the WP-033 acceptance
  status are yours to update after integration.
- Behaviour changes worth reviewing: a `PropertyDocument` can no longer be
  recorded by two `UndoService`s. `openReferenceDocument` reports "Unrecognized
  document format" for bytes no codec recognizes and "The file holds another
  document kind" for a canvas file. Canvas page, layer and image
  changes are now undo steps. `setPageRect`/`setPageColor` still apply
  without history while a QML component is initializing. The timed autosave
  now runs on a worker.
