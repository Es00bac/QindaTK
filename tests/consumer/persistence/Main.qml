// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    width: Tk.Theme.size.panelWidth
    height: Tk.Theme.size.panelHeight

    Tk.DocumentManager {
        objectName: "qmlDocumentManager"
        Component.onCompleted: createReferenceDocument()
    }
}
