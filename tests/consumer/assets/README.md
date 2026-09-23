<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed asset services consumer

This standalone consumer links the staged `QindaTK::qindatk` package, writes a
small temporary PNG, registers it with the public `ResourceResolver`, and
requests a thumbnail through `AssetProvider`. It checks the asynchronous
result and then loads installed QML instances of `JobModel`,
`ResourceResolver`, `AssetProvider`, `DocumentResourceLinks`,
`AssetThumbnail`, `AssetWaveform`, and `AssetFilmstrip`.

Configure with `-DCMAKE_PREFIX_PATH=<staged-prefix>`, build using the
configured job and load limits, and run with `QT_QPA_PLATFORM=offscreen` and
`QT_QUICK_BACKEND=software`.
