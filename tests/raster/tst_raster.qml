// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk
import QindaTK.Raster as Raster

TestCase {
    name: "RasterView"
    width: 500; height: 360
    visible: true
    when: windowShown
    Raster.RasterView {
        id: view
        width: 500; height: 360
        documentSize: Qt.size(512, 384)
        source: "data:image/svg+xml;utf8," + encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="32" height="24"><rect width="16" height="24" fill="#22d3ee"/></svg>')
        fitMode: "page"
    }
    SignalSpy { id: began; target: view; signalName: "strokeBegan" }
    SignalSpy { id: moved; target: view; signalName: "strokeMoved" }
    SignalSpy { id: ended; target: view; signalName: "strokeEnded" }
    function init() { began.clear(); moved.clear(); ended.clear(); view.paintingEnabled = true }
    function test_documentCoordinatesDoNotFollowProxyResolution() {
        tryCompare(view.surfaceItem, "width", 512)
        compare(view.surfaceItem.height, 384)
        const p = Qt.point(71, 95), doc = view.toContent(view.fromContent(p))
        fuzzyCompare(doc.x, p.x, 0.001); fuzzyCompare(doc.y, p.y, 0.001)
        view.setZoom(1.5)
        const again = view.toContent(view.fromContent(p))
        fuzzyCompare(again.x, p.x, 0.001); fuzzyCompare(again.y, p.y, 0.001)
        view.fitPage()
    }
    function test_mouseSamplesUseDocumentUnits() {
        mousePress(view.surfaceItem, 70, 60)
        mouseMove(view.surfaceItem, 110, 90, 20)
        mouseRelease(view.surfaceItem, 110, 90)
        compare(began.count, 1); verify(moved.count >= 1); compare(ended.count, 1)
        fuzzyCompare(began.signalArguments[0][0].position.x, 70, 1)
        fuzzyCompare(began.signalArguments[0][0].position.y, 60, 1)
        compare(began.signalArguments[0][0].pressure, 1)
    }
    function test_disabledPaintingDoesNotEmitStrokes() {
        view.paintingEnabled = false
        mousePress(view.surfaceItem, 70, 60)
        mouseRelease(view.surfaceItem, 70, 60)
        compare(began.count, 0); compare(ended.count, 0)
    }
    function test_displayTileKeepsDocumentAndSelectionSize() {
        view.displayRect = Qt.rect(50, 40, 120, 80)
        view.selectionRect = Qt.rect(60, 50, 30, 20)
        const image = findChild(view, "rasterImage"), selection = findChild(view, "rasterSelection")
        compare(image.x, 50); compare(image.width, 120)
        compare(view.surfaceItem.width, 512)
        compare(selection.x, 60); compare(selection.width, 30)
        view.displayRect = Qt.rect(0, 0, 512, 384)
    }
}
