<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed WP-040 canvas consumer

This standalone project links the staged `QindaTK::qindatk` target and imports
the installed `QindaTK` QML module. It creates a CanvasDocument, ToolController
and CanvasView, draws a rectangle through the tool's gesture contract, checks
one commit and view coordinate mapping, then checks the software render surface
before the edit, after the edit and after undo. The pixel checks detect missing
render updates even when the tile scheduler reports zero pending work.

Configure against a refreshed staged prefix, build without overriding the
system's configured `MAKEOPTS`, and run headless:

```sh
cmake -S tests/consumer/wp040 -B build/consumer-wp040 -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/qindatk-stage -DCMAKE_BUILD_TYPE=Debug
cmake --build build/consumer-wp040
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  build/consumer-wp040/qindatk_wp040_consumer
```

When using a Qt SDK different from the system Qt, include that SDK and the
QindaTK stage in `CMAKE_PREFIX_PATH`; at runtime point `LD_LIBRARY_PATH` at
their library directories and `QT_PLUGIN_PATH` at the same Qt SDK's plugins.
Mixing Qt libraries from different minor versions can fail before QML loads.
