<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed command consumer fixture

This standalone CMake project verifies command services through the installed
public package. It discovers `QindaTK`, links only `QindaTK::qindatk`, and
imports the QML module with the required `QindaTK as Tk` prefix. C++ owns two
document scopes, a descriptor, scoped handlers, their enabled state, and a
shortcut map. QML consumes the public `CommandBinding` and `CommandButton`.

The smoke checks a customized shortcut and dispatches it through
`ShortcutMap`, verifies a disabled reason in both bindings, then activates the
second document and confirms both the button and shortcut call that document's
handler with its expected document ID. It does not load an optional engine.

Configure against a staged install prefix, then build and run:

```sh
cmake -S tests/consumer/commands -B build/consumer-commands -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/qindatk-prefix -DCMAKE_BUILD_TYPE=Debug
cmake --build build/consumer-commands
QT_QPA_PLATFORM=offscreen build/consumer-commands/qindatk_commands_consumer
```

For direct Ninja builds, use the live job and load limits from
`portageq envvar MAKEOPTS`; never override Portage configuration or reduce
parallelism. No command-local `MAKEOPTS` or environment wrapper is used.
