<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Consume an uninstalled toolkit build

After configuring and building QindaTK, a consumer can use:

```
cmake -S <consumer-source> -B <consumer-build> \
  -DQindaTK_DIR=<toolkit-build>/cmake-build
```

`find_package(QindaTK 0.1 REQUIRED CONFIG)` exports QindaTK::qindatk and
QindaTK::agent. The generated package sets QindaTK_BUILD_TREE=true and
QindaTK_QML_DIR to `<toolkit-build>/qml/QindaTK` (the directory containing
qmldir); its parent is the QML import root. Both `<qindatk/header.h>` and
existing unqualified SDK includes resolve against the live source. Header
symlinks, or copied headers where symlinks are unavailable, are generated only
inside `<toolkit-build>/include/qindatk`.

This is a development build export, not an installation. Keep its source and
build directories available while consumers use them. Build consumers after
changing toolkit C++ layouts, and ensure their runtime QML import root matches
the linked library. The normal installed package config and Portage ownership
remain unchanged. Do not use an old installed executable with a new build's
library merely by changing LD_LIBRARY_PATH or QML_IMPORT_PATH.

Qualification includes a separate CMake project using find_package, both
exported targets, prefixed public headers, a linked executable and a headless
run. The reusable source is `tests/cmake/build_tree_consumer`; configure it in
a separate build directory with the same QindaTK_DIR, build through the normal
approved build wrapper and run `qtk-consumer`. QindaOffice can use this package
to qualify new ABI changes before the
coordinated Portage toolkit/consumer release.
