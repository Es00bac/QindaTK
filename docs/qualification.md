<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Linux platform and backend qualification (WP-088)

`tools/qualify/qualify.py` builds QindaTK in the configurations we claim on
Linux. It runs each operation on each configuration and records every result.
It writes `build/qualify/results/<run>/results.json` and `results.md`; with
`--evidence` it also copies them into
[evidence/WP-088-results.md](universal-toolkit/implementation/evidence/WP-088-results.md)
and its JSON twin. A configuration counts as supported only for the operations
that passed in the latest recorded run. `docs/platform-support.md` summarizes
the claims.

## Running it

```sh
python3 tools/qualify/qualify.py --list              # the plan, nothing runs
python3 tools/qualify/qualify.py --smoke             # Qt 6.11, FFmpeg off: matrix + install
python3 tools/qualify/qualify.py --full --evidence   # everything, then update the evidence
```

Narrower runs combine `--qt 6.11,6.8`, `--variants full,no-alsa`,
`--matrix-variant full`, `--configs wayland-opengl,xcb-software`,
`--ops render,atspi`, `--skip-build` (configure, then reuse the existing
binaries) and `--no-install`. The exit status is 1 when any row failed.
Unqualified rows do not fail the run.

Builds use `build/qualify/<qt>-<variant>`, Debug, ccache
(`CCACHE_BASEDIR=~/work_SPaC3`, `CCACHE_NOHASHDIR=1`) and the user's limit for
this work, `cmake --build <dir> --target … -- -j12 -l12` (AGENTS.md build
policy; D-201). A variant builds only what its checks need:

- every variant builds the library, the QML plugins, `qindatk_widgets` and
  `qtk-preview`;
- the matrix variant also builds the test binaries its operations run.

A cold full-library build takes about an hour at this limit. `--full` makes up
to 18 of them (9 variants × 2 Qt versions). Variants whose resolved engines
match an earlier variant's are not rebuilt; their results are reused and
marked as such. On Qt 6.8, no-quick3d and no-shadertools match full, because
the SDK has neither module.

## What it covers

**1. Matrix.** Qt 6.11.1 (system `/usr`) and the isolated Qt 6.8.3 SDK
(`QINDATK_QT68_PREFIX`, default `~/work_SPaC3/QindaTK-universal/build/qt68-sdk/6.8.3/gcc_64`).
Runtime configurations:

| Configuration | Platform | Scene graph | Display |
| --- | --- | --- | --- |
| `offscreen-software` | offscreen | software | none |
| `offscreen-rhi` | offscreen | RHI (any API) | none |
| `wayland-software` | wayland | software | headless Weston, pixman |
| `wayland-opengl` | wayland | RHI OpenGL | headless Weston, GL |
| `wayland-vulkan` | wayland | RHI Vulkan | headless Weston, GL |
| `xcb-software` | xcb | software | rootful Xwayland inside headless Weston |
| `xcb-opengl` | xcb | RHI OpenGL | rootful Xwayland inside headless Weston |

The `render` operation comes first on each configuration. It runs
`qtk-preview fixtures/render_probe.qml --grab … --wait 4500` (`--gpu` on RHI
configurations). The fixture logs the platform plugin and `GraphicsInfo.api`,
and the grabbed centre pixel must be the probe colour. If Qt fell back to
another platform or API, the configuration is **unqualified** and so is every
other operation on it: a fallback never counts as a pass.

**2. Operations** (`qualify_matrix.py`, one row each):

| Operation | What runs | Source |
| --- | --- | --- |
| `native-windows` | `tst_detached_panel_host`, `tst_window_host` | WP-080 |
| `file-dialogs` | `tst_native_file_dialog` | WP-018 |
| `print-pdf` | `tst_print_output`, `tst_native_print` | WP-053 |
| `print-cups` | `tst_native_print` destinations, only when a CUPS scheduler with queues exists | WP-053 |
| `input` | `tst_stylus_handler` (synthetic pen, eraser and touch through Qt's tablet path), `tst_raster_stroke_input`, `tst_canvas_input`, `tst_gesture_router` | WP-040, WP-045 |
| `input-tablet-hardware` | Unqualified unless a physical pen tablet is attached, and even then a person must run it | — |
| `accessibility` | `tst_accessibility_adapter`, `tst_sheet_accessibility` (in-process `QAccessible`) | WP-017, WP-055 |
| `atspi` | `atspi_probe.py`: an external AT-SPI client presses a button and a check box in `fixtures/atspi_probe.qml` over the accessibility bus | WP-055 probe pattern |
| `media-devices` | `tst_audio_engine`: built-in null/loopback, device loss, ALSA `null`/`file` PCMs, backend report | WP-060 |
| `plugin-editors` | Every `tst_plugin*` binary once WP-066's host types exist. Until then: "not yet available" (unqualified) | WP-066 |
| `render-context-loss` | `tst_canvas_render contextLossAndBackendFallback`, `tst_canvas_render_lifecycle`, and `tst_scene_view_3d_render` on RHI configurations | WP-040, WP-076 |

`media-devices`, `print-cups` and `input-tablet-hardware` do not depend on the
window system. They run once per build and appear as configuration
`any (platform-independent)`.

**3. Optional engines omitted.** Variants `no-poppler`, `no-alsa`, `no-ffmpeg`,
`no-quick3d`, `no-widgets`, `no-nativeprint`, `no-shadertools` and `minimal`
(all off) set the switches in the top-level `CMakeLists.txt` (D-201).
Configure writes `qindatk-features.json` with what each build resolved. The
consumer `tests/consumer/qualify` configures and links against the installed
prefix only, and imports `QindaTK as Tk`. It then reports each engine:
`media.ffmpeg`, `audio.alsa` (D-203), `pdf.poppler`, `volume.gpuShaders`,
`scene3d.quick3d`, `dialogs.native` and `print.native`. The `optional-engines`
row passes when the report matches the build exactly and each missing engine
gives a reason.

**4. Install and relocation.** `cmake --install` puts each variant into
`build/qualify/stage/<name>`, and the consumer is built and installed into the
same stage. The stage then moves to `build/qualify/relocated/<name>`, where
`qtk_qualify_consumer`, `qtk_qualify_qml_only` (Qt Quick only, so the QML
plugin must find `libqindatk` itself) and the installed `qtk-preview --check`
all run with a clean environment. The `install-relocation` row fails if any of
these checks fails:

- `QML_IMPORT_TRACE` and `LD_DEBUG=libs` show a load from the worktree
  outside the moved stage;
- `ldd` resolves `libqindatk` elsewhere;
- `strings` (after `objcopy --strip-debug`) or a text scan finds the worktree
  or the old stage path in an installed file;
- a RUNPATH holds something other than `$ORIGIN…` or `/usr/…`.

The Qt 6.8 runs name only the SDK's own `lib` directory in
`LD_LIBRARY_PATH`, because that Qt is not on the linker path.

**5. Results.** Each row records Qt, variant, configuration, operation,
status (`passed`, `failed`, `unqualified`), reason, evidence lines and a log
path. `qualify_report.py` rejects a non-passed row without a reason. A QtTest
QSKIP makes the row unqualified with the skip message.

## Silence and privacy

Nothing reaches the user's desktop (AGENT-GUARD in `qualify_env.py`):

- every process runs without `DISPLAY`, `WAYLAND_DISPLAY`, the session bus or
  the input-method variables, under its own short `XDG_RUNTIME_DIR` in
  `/run/user/<uid>`;
- displays are private: a headless `weston --no-config --backend=headless`,
  plus `Xwayland :<free n> -geometry 1280x800` inside it for xcb;
- every test and probe runs under a private `dbus-run-session` whose bus can
  activate only `org.a11y.Bus`, so a file-dialog test cannot reach a portal
  and nothing can reach a screen reader.

The audio checks open only built-in and ALSA `null`/`file` devices.

## Adding a configuration or an operation

Add it to `qualify_matrix.py`: a `Configuration` (platform, scene graph,
display kind, expected API) or an `Operation` (its QtTest binaries and
functions, or a new `kind` handled in `qualify.run_operation`). The driver
runs everything listed there. Update this page and re-run with `--evidence`.
