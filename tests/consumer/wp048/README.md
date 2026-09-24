<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed WP-048 timeline consumer

This standalone application links the staged `QindaTK::qindatk` target and
imports the installed `QindaTK` QML module. Its QML root creates a
`Tk.TimelineModel` and binds it to `Tk.EditTimeline`. The C++ host populates
one track and one clip through the public typed model API, applies one typed
move, verifies a single history step and exact undo/redo positions, then
serializes and reloads the model to check stable IDs and exact tick fields.
The clip start is greater than JavaScript's exact integer limit so accidental
conversion through a QML `Number` would be detected.

Configure against a refreshed staged prefix, build, and run headless:

```sh
cmake -S tests/consumer/wp048 -B build/consumer-wp048 -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/qindatk-stage -DCMAKE_BUILD_TYPE=Debug
cmake --build build/consumer-wp048
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  build/consumer-wp048/qindatk_wp048_consumer
```

When using a Qt SDK different from system Qt, include that SDK and the QindaTK
stage in `CMAKE_PREFIX_PATH`; at runtime point `LD_LIBRARY_PATH` at their
library directories and `QT_PLUGIN_PATH` at the same Qt SDK's plugins. Mixing
Qt libraries from different minor versions can fail before QML loads.
