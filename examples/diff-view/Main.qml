// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    width: 960
    height: 620
    color: Tk.Theme.color.bg
    Tk.DiffModel {
        id: changes
    }
    Component.onCompleted: changes.setDocuments(
        ["function total(items) {", "    return items.length;", "}", "", "export { total };"],
        ["function total(items) {", "    return items.reduce((sum, item) => sum + item.value, 0);",
         "}", "", "export { total };"], "working-1", "review-1")
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.md
        gap: Tk.Theme.space.sm
        Tk.Label { text: qsTr("Code review"); font.pixelSize: Tk.Theme.font.title }
        Tk.DiffView {
            diffModel: changes
            tooltip: qsTr("Code review differences")
            Tk.Flex.grow: 1
        }
    }
}
