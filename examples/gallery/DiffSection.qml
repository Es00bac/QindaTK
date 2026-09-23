// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Differences"
    note: "DiffView · DiffModel · host transaction requests"
    Tk.DiffModel { id: changes }
    Component.onCompleted: changes.setDocuments(
        ["config:", "  timeout: 10", "mode: safe"],
        ["config:", "  timeout: 20", "mode: safe"], "gallery-local-1", "gallery-review-1")
    Tk.DiffView {
        width: 940
        height: 160
        diffModel: changes
        tooltip: qsTr("Gallery configuration diff")
        Tk.Flex.shrink: 0
    }
}
