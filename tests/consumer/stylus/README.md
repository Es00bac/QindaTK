<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed StylusHandler consumer fixture

This standalone project consumes the installed `QindaTK::qindatk` target,
includes `<qindatk/stylus_handler.h>`, constructs the C++ class, and imports
`Tk.StylusHandler` from QML. It intentionally uses no source-tree include path
or local build-tree library.

Build and run from the repository root:

```sh
cmake -S tests/consumer/stylus -B build/consumer-stylus -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/consumer-stylus
QT_QPA_PLATFORM=offscreen build/consumer-stylus/qindatk_stylus_consumer
```

For direct Ninja invocations, read the configured job/load limits with
`portageq envvar MAKEOPTS` and pass the returned values unchanged.

The fixture demonstrates the public dependency contract as currently
installed: the exported CMake target carries `Qt6::QuickPrivate` transitively,
and the public header includes QtQuick's private
`qquickpointerdevicehandler_p.h`. It is a compatibility exposure, not a
promise that Qt's private API is stable. The result is specific to the Qt
version reported by `qmake6 -query QT_VERSION` on the machine running it.
