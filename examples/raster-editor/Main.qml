// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk
import QindaTK.Raster as Raster

Raster.RasterView {
    width: 1000; height: 720
    documentSize: Qt.size(512, 384)
    source: "data:image/svg+xml;utf8," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="128" height="96"><rect x="8" y="8" width="70" height="60" rx="10" fill="#22d3ee" opacity="0.8"/><circle cx="90" cy="58" r="28" fill="#f7ca75" opacity="0.8"/></svg>')
    selectionRect: Qt.rect(48, 40, 180, 120)
    fitMode: "page"
}
