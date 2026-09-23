<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed workspace consumer

This standalone executable links the staged `QindaTK::qindatk` package,
persists and reloads typed settings and an empty workspace through public
headers, then instantiates the installed QML service types. Configure with
`-DCMAKE_PREFIX_PATH=<staged-prefix>`, build under the configured job/load
limits, and run with `QT_QPA_PLATFORM=offscreen` and
`QT_QUICK_BACKEND=software`.
