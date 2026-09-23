<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed event, diff and resource consumer

This standalone consumer checks the staged public C++ headers and installed
QML module for WP-032. It imports all three views, appends a structured event,
compares two revision snapshots, and commits and undoes a resource allocation.

Configure with `CMAKE_PREFIX_PATH` pointing to a staged QindaTK package,
build with the configured system `MAKEOPTS` unchanged, and run with
`QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software`.
