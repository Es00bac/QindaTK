<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed domain-session consumer

This standalone project links the staged `QindaTK::qindatk` target through its
installed public headers and imports the installed `QindaTK` QML module. It
opens a reference property session and a canvas session side by side and
checks independent undo through the shared `UndoService`. It saves both,
closes and reopens them by content with byte-identical canvas state, and
restores the canvas from recovery after an external writer replaces its file.
A declarative `Tk.DocumentManager` then creates a canvas session and exposes
it through `session.model`.

Configure against a refreshed staged prefix and run headless:

```sh
cmake -S tests/consumer/domain_sessions -B build/consumer-domain -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/qindatk-stage -DCMAKE_BUILD_TYPE=Debug
cmake --build build/consumer-domain
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  build/consumer-domain/qindatk_domain_session_consumer
```

When using a Qt SDK different from the system Qt, include that SDK and the
QindaTK stage in `CMAKE_PREFIX_PATH`; at runtime point `LD_LIBRARY_PATH` at
their library directories and `QT_PLUGIN_PATH` at the same Qt SDK's plugins.
