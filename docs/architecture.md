<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Architecture

QindaTK is one QML module, `QindaTK`, backed by one shared library,
`libqindatk`. C++ owns everything that needs an algorithm or process-wide
state; QML owns everything that is chrome.

```
application QML  ── import QindaTK as Tk ──►  QindaTK (QML module)
                                              ├── qml/*.qml      controls, chrome, text, Box, Scroll
                                              └── libqindatk.so  C++ types registered with QML_ELEMENT
                                                  ├── Flex, Grid, Stack (+ *Attached)   layout engines
                                                  ├── Box seating helper: LayoutInfo    (QuickPrivate)
                                                  ├── Theme, Density                    singletons
                                                  ├── Icon, Icons (registry)            Lucide paths, SVG
                                                  └── DockModel                          arrangement authority
application C++  ── QindaTK::qindatk (CMake target) ──►  <qindatk/theme.h> <qindatk/dock_model.h> ...

QindaTK.QindaQt (QML-only module)  ──►  QindaQtTheme: QindaQt.Tokens (QST-1) → Theme.applyQst()
```

## C++ / QML split

| Lives in C++ | Why |
| --- | --- |
| `Flex`, `Grid`, `Stack` and their attached types | Layout algorithms need iteration, freezing, track sizing; QML bindings cannot express them without loops. They also need `polish()` batching. |
| `Theme`, `Density` | Process-wide state with derived values; typed colour properties for tooling; JSON and QST adoption. |
| `Icon`, `Icons` | Path parsing, `QPainter` rendering, the generated Lucide table. |
| `DockModel` | Arrangement state with invariants (lane renumbering, group contiguity, zone rules) that must be testable without a scene graph. |
| `LayoutInfo` | Reads `QQuickItemPrivate` (anchors, explicit size) — the only private-API use in the toolkit. |

Everything else is QML: controls are `QtQuick.Templates` types with theme
driven visuals; `Box`, `Scroll`, `Panel`, `Island` compose the C++
containers. This keeps visuals editable without a compiler and keeps the
algorithms out of JavaScript.

## Singletons: ownership and threading

`Theme`, `Density`, `Icons` and `LayoutInfo` are `QML_SINGLETON` types
whose `create()` returns one process-wide instance with
`QJSEngine::CppOwnership`, so every QML engine in the process shares the
same theme and the same icon registry, and C++ code reaches the same
objects through `Theme::instance()`, `Density::instance()`,
`IconRegistry::instance()`.

All of them are **main-thread only**. They are plain `QObject`s with no
locking; the layout containers are `QQuickItem`s. Do not touch them from a
worker thread.

Change propagation: `Density::changed` → `Theme::rescale()` → every ladder
republishes its keys → `Theme::changed` and `ThemeColors::changed` /
`ThemeFont::changed`. QML bindings on `Tk.Theme.color.*`, `Tk.Theme.font.*`
and the ladders re-evaluate from those signals; `Theme.generation`
increments on every update for code that wants a single counter.

## Layout containers: invalidation and convergence

`Flex`, `Grid` and `Stack` share one scheme (`LayoutContainer` in
`layout_attached.h`):

1. Anything that can change the result calls `invalidateLayout()`: a
   container property, a child added/removed, a child's `implicitWidth`,
   `implicitHeight` or `visible`, an attached property, or the container's
   own size. `invalidateLayout()` calls `QQuickItem::polish()`; the actual
   work runs in `updatePolish()` before the next frame, so a burst of
   changes costs one layout.
2. `doLayout()` reads children's **implicit** sizes and attached values,
   never their `width`/`height`, and then sets each child's position and
   size. A child that binds its own `width` is not consulted, so it cannot
   fight the container.
3. The container sets its own **implicit** size from the content (plus
   padding). It never sets its own `width`/`height`; the parent does.
4. If a child reacts to the new size by changing its implicit size
   (wrapping `Text`, a nested container that polishes later), the
   container is invalidated again. Invalidation during a layout is
   remembered (`m_dirtyDuringLayout`) and turns into one more `polish()`.
   Layouts converge in one or two extra passes; a genuine oscillation would
   show up as Qt's polish-loop warning.
5. `relayout()` runs the layout synchronously (tests, measurement).

`Box` and `Scroll` are QML and use anchors: `Box` sizes its padding box
with anchor margins and stretches an unanchored single child with
`anchors.fill` (see [layout.md](layout.md#box)); implicit sizes flow up
through `contentHost.implicitWidth/Height`.

Repeater objects are skipped as slots; their delegates are laid out. Items
with `Tk.Flex.ignore: true` (or `Tk.Grid.ignore`, `Tk.Stack.ignore`) keep
their own geometry.

## Implicit sizes flow bottom-up

A `Label` has the implicit size of its text. A `Flex` row's implicit width
is the sum of its children's hypothetical main sizes plus gaps and padding;
its implicit height is the tallest child plus padding. A `Box` adds padding
and border to its content's implicit size. A `Panel` adds a 24px header.
So `Tk.Panel { Tk.Flex { ... } }` reports a usable size without any
explicit numbers, and `qtk-preview` sizes its window from the root's
implicit size when `--size` is not given.

The exception to remember: a `Flex`/`Grid`/`Stack` **owns** its implicit
size. A QML binding on its `implicitHeight` is overwritten by every
layout. Wrap it in an `Item` to report a fixed size (`SectionHeader.qml`
does this).

## How applications link

**QML only.** Install the toolkit; the module lands in Qt's QML import
path (`QINDATK_QML_INSTALL_DIR`). `import QindaTK as Tk` loads
`qindatkplugin`, which links `libqindatk`. Nothing to add to the
application's CMake.

**C++ too.** `find_package(QindaTK)` provides `QindaTK::qindatk` and the
headers under `include/qindatk/` (`theme.h`, `density.h`, `dock_model.h`,
`flex.h`, `grid.h`, `grid_tracks.h`, `stack.h`, `icon.h`, `layout_info.h`,
`svg_path.h`; `*_p.h` files are not installed). Linking the backing library
also registers the QML types in-process (the generated
`QQmlModuleRegistration`), so the plugin is optional there — the qmldir
says `optional plugin qindatkplugin`.

`QindaTKConfig.cmake` also sets `QindaTK_QML_DIR` to the installed module
directory.

## Module output and import paths

- `QT_QML_OUTPUT_DIRECTORY` is `${CMAKE_BINARY_DIR}/qml`; every module of
  the repository lands there (`qml/QindaTK`, `qml/QindaTK/QindaQt`), so one
  import path serves tools, tests and examples: `-I build/dev/qml` or
  `QML_IMPORT_PATH=build/dev/qml`. `qtk-preview` and the test binaries add
  it themselves (`QINDATK_BUILD_QML_DIR`).
- QML files are globbed (`src/qml/*.qml`, `CONFIGURE_DEPENDS`); re-run
  cmake after adding a file. A file starting with `pragma Singleton` is
  registered as a singleton type automatically.
- The module's QML files are compiled into the plugin/backing library
  resources under `:/qt/qml/QindaTK/`; the generated qmldir prefers that
  path. The install step also copies the `.qml`, `qmldir` and `.qmltypes`
  files for tooling (`qmllint`, IDEs).
- `QindaTK.QindaQt` is a QML-only module built like the main one (backing
  library `qindatk_qindaqt` + plugin `qindatk_qindaqtplugin`, QML in
  resources), so it imports from the build tree and from the install
  without linking. Importing it requires `QindaQt.Tokens` at run time; it
  is meant for applications running on the QindaQt desktop.

## Conventions enforced across the tree

- SPDX header `LGPL-3.0-or-later` on every file; ≤ 500 non-blank lines per
  hand-written file (`icon_data.cpp` is generated and exempt).
- `AGENT-NOTE:` / `AGENT-GUARD:` / `AGENT-CONTRACT:` comment markers (see
  [../AGENTS.md](../AGENTS.md)). A stale marker is a defect.
- Inside the module, every QML file imports `QindaTK as Tk` and qualifies
  toolkit types; the names `Grid`, `Label`, `Button`, `ScrollBar`, `ToolTip`
  collide with QtQuick otherwise.
- Controls use `QtQuick.Templates as T`, never a QtQuick.Controls style.
- No colour, size, font or duration literals in controls; all come from
  `Tk.Theme`.

## Private-API boundary

`layout_info.cpp` includes `<QtQuick/private/qquickitem_p.h>` and
`<QtQuick/private/qquickanchors_p.h>` to answer "does this item anchor
itself?" and "was its width/height set explicitly?". That is the entire
private surface; `find_package(Qt6QuickPrivate)` exists for it alone. If a
Qt release breaks it, only `LayoutInfo` needs attention and `Box`/`Scroll`
seating is the only behaviour affected.

## Where things are

```
src/core/     C++ (see the diagram); dock_model_p.h is internal
src/qml/      QML types; one type per file, file name == type name
src/bridge/   QindaQtTheme.qml (module QindaTK.QindaQt)
tools/preview qtk-preview (main.cpp; qst_support.cpp is the optional desktop-theme publisher)
tools/scripts gen_icons.py, gen_catalog.py, screenshots.sh
packaging/    Gentoo ebuild for the user's overlay (dev-libs/qindatk)
tests/cpp     QtTest binaries; tests/qml QtQuickTest cases (one runner)
examples/     QML-only examples; each Main.qml is a ctest smoke test
docs/         this documentation; catalog.json is generated
```
