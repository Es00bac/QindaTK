<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed persistence consumer fixture

This standalone project checks the WP-013 installed package. It configures
against `QindaTKConfig.cmake`, links `QindaTK::qindatk`, saves a typed
reference document, destroys its first manager, and opens the saved file with a
new manager. It checks the stored revision, entity identity and value, then
edits the reopened session and checks that the new revision is dirty. Finally,
it imports the installed `QindaTK` QML module and creates a document through
`Tk.DocumentManager`.

Use a staged installation prefix from a completed QindaTK build. In this
worktree the Qt 6.11 staging prefix is `build/qt611-stage`:

```sh
cmake -S tests/consumer/persistence -B build/wp013-consumer -G Ninja \
  -DCMAKE_PREFIX_PATH="$PWD/build/qt611-stage" -DCMAKE_BUILD_TYPE=Debug
cmake --build build/wp013-consumer
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  build/wp013-consumer/qindatk_persistence_consumer
```

For direct Ninja builds, use the job and load limits reported by
`portageq envvar MAKEOPTS`; never override Portage configuration or reduce
parallelism. The command above inherits the configured `-j24 -l24` settings.
