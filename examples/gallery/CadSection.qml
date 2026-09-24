// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "CAD"
    note: "CadViewport · CadDocument (docs/cad.md)"

    Tk.CadDocument {
        id: sketch
        Component.onCompleted: {
            const a = addPoint(0, 0)
            const b = addPoint(60, 0)
            const c = addPoint(60, 40)
            const ab = addSegment(a, b)
            const bc = addSegment(b, c)
            addConstraint("horizontal", ab, "", 0)
            addConstraint("vertical", bc, "", 0)
            addCircle(addPoint(30, 20), 8)
        }
    }
    GalleryRow {
        label: "CadViewport"
        Tk.CadViewport {
            objectName: "galleryCadViewport"
            implicitWidth: 660
            implicitHeight: 260
            document: sketch
            zoom: 3
            tooltip: qsTr("Editable CAD sketch")
        }
    }
}
