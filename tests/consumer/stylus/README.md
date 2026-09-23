<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed StylusHandler consumer fixture

This standalone project consumes the installed `QindaTK::qindatk` target,
includes `<qindatk/stylus_handler.h>`, constructs the C++ class, and imports
`Tk.StylusHandler` from QML. It intentionally uses no source-tree include path
or local build-tree library.

Build and run from the repository root:

```sh
cmake -S tests/consumer/stylus -B build/consumer-stylus -G Ninja -DCMAKE_BUILD_TYPE=Debug
QT_QPA_PLATFORM=offscreen build/consumer-stylus/qindatk_stylus_consumer
```

Between configure and run, read `portageq envvar MAKEOPTS`, then build with
`ninja -C build/consumer-stylus -jN -lL`, replacing `N` and `L` with that
machine's configured job and load values. Do not override `MAKEOPTS` or
reduce the limits.

The fixture tests the installed public boundary: the exported CMake target
and public header must not expose `Qt6::QuickPrivate` or Qt Quick private
include paths. The [WP-005 evidence](../../../docs/universal-toolkit/implementation/evidence/WP-005.md)
records independent staged runs with Qt 6.8.3 and 6.11.1. When running
against an isolated SDK beside another host Qt, set `LD_LIBRARY_PATH` to
that SDK's `lib` directory so transitive Qt modules resolve consistently.
