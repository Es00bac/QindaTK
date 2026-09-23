<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed platform services consumer

This standalone consumer compiles against staged public QindaTK headers and
imports the installed QML module. It instantiates clipboard, drag/drop,
accessibility, locale and gesture services and checks their stable object names.
The package's focused tests exercise behavior; this consumer checks packaging
and public type registration on the selected Qt runtime.

Configure with `-DCMAKE_PREFIX_PATH=<staged-prefix>`, build with the actual
configured system job and load limits, and run using the offscreen platform.
