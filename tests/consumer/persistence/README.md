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
python3 -c 'import re, subprocess; s=subprocess.check_output(["portageq", "envvar", "MAKEOPTS"], text=True); j=re.search(r"(?:^|\s)-j\s*(\d+)", s); l=re.search(r"(?:^|\s)-l\s*(\d+)", s); assert j and l, s; subprocess.check_call(["ninja", "-C", "build/wp013-consumer", "-j"+j.group(1), "-l"+l.group(1)])'
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  build/wp013-consumer/qindatk_persistence_consumer
```

The build command reads the configured job and load limits from
`portageq envvar MAKEOPTS` and passes their equivalent to direct Ninja. It
does not change Portage's configuration.
