<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed platform services consumer

This standalone consumer compiles against staged public QindaTK headers and
imports the installed QML module. It instantiates clipboard, drag/drop,
accessibility, locale, gesture, exact TimeMap, window, file-dialog and desktop
services and checks their stable object names. It also instantiates the format
registry, font catalog, import/export dialogs, command bars, typed inspector
and font/calendar controls. It deliberately links only the base QindaTK
package, proving that Qt Widgets is optional.
The same fixture also instantiates navigation, bounded editor, preferences,
history/notification/task and transport surfaces from the installed module.
The package's focused tests exercise behavior; this consumer checks packaging
and public type registration on the selected Qt runtime.

Configure with `-DCMAKE_PREFIX_PATH=<staged-prefix>`, build with the actual
configured system job and load limits, and run using the offscreen platform.
