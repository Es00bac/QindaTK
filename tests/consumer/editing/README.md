<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed editing consumer fixture

This standalone project checks WP-012 through the installed public package.
It finds `QindaTK`, links only `QindaTK::qindatk`, and imports `QindaTK as Tk`.
QML instantiates the public `PropertyBinding`, `SelectionModel` and
`ValidationModel`; C++ owns the reference `PropertyDocument`, `UndoService`,
entity IDs and validation snapshot. The consumer edits a value through QML,
then checks atomic commit,
undo and redo through the public C++ API. It also checks that selection keeps
the exact entity UUID and that validation rejects a result tagged with an old
revision while accepting a result for the current snapshot.

Configure against an installed prefix and run the executable offscreen:

```sh
cmake -S tests/consumer/editing -B build/consumer-editing -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/qindatk-prefix -DCMAKE_BUILD_TYPE=Debug
cmake --build build/consumer-editing
QT_QPA_PLATFORM=offscreen build/consumer-editing/qindatk_editing_consumer
```

For direct Ninja builds, use the live job and load limits from
`portageq envvar MAKEOPTS`; never override Portage configuration or reduce
parallelism. No command-local `MAKEOPTS` or environment wrapper is used.
