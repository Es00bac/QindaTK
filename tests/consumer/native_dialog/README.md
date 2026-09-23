<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed native-dialog consumer

Configure this standalone consumer with
`-DCMAKE_PREFIX_PATH=<staged-QindaTK-prefix>` and build under the actual
configured system job/load limits. It checks that the optional
`QindaTKWidgets` package resolves the base QindaTK target, Qt Widgets and
the installed native dialog public header. Run with
`QT_QPA_PLATFORM=offscreen`; it constructs `QApplication` and confirms the
backend is available without opening an interactive dialog.
