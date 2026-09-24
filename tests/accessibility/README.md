<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Linux AT-SPI qualification

This is a manual external-client check for `AccessibilityAdapter`; it checks
the desktop AT-SPI tree and does not use `QAccessible::queryAccessibleInterface`
inside the application process.

Build QindaTK, then build and start the small C++ fixture in a graphical
session with Qt's Linux accessibility bridge enabled:

```sh
cmake -S . -B build/wp017-atspi -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/wp017-atspi
cmake -S tests/accessibility -B build/wp017-atspi-action -G Ninja \
  -DQINDA_BUILD_DIR="$PWD/build/wp017-atspi" -DCMAKE_BUILD_TYPE=Debug
cmake --build build/wp017-atspi-action
QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 build/wp017-atspi-action/atspi_action_fixture \
  "$PWD/build/wp017-atspi/qml" "$PWD/tests/accessibility/atspi_fixture.qml"
```

In another terminal in the same desktop session, run:

```sh
python3 tests/accessibility/atspi_probe.py --invoke-press
```

The client prints AT-SPI roles, names, descriptions, values and action names.
It invokes Intro's `press` action through the desktop bus. The fixture handler
commits an increment through `EditTransaction`/`UndoService`, refreshes the
accessible snapshot, and prints `ACTION_HANDLER_CALLED value=2 undo=1`.
The offscreen Verse clip is included to check model-backed virtual items.
Without `--invoke-press`, the probe is read-only. This is an AT-SPI client
probe, separate from the `QAccessible` in-process test.

The same client can invoke the workbench's actual semantic rename button. In
one terminal, start the staged workbench in the graphical session with Qt's
Linux accessibility bridge enabled:

```sh
QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 \
  build/workbench-qt68/qindatk_workbench
```

In another terminal in that session, run:

```sh
python3 tests/accessibility/atspi_probe.py --invoke-workbench-rename
```

The probe locates the accessible `Apply name` button, invokes its desktop
`press` action over AT-SPI, and exits nonzero unless the external action
client reports success. This exercises the workbench `CommandButton` and
`workbench.rename` command route in the application process.
