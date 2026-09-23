// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: legend
    property list<Tk.ChartDataset> datasets
    property string tooltip: qsTr("Chart series visibility")
    property real rowGap: Tk.Theme.space.xs
    signal visibilityRequested(string seriesId, bool visible)

    implicitWidth: content.implicitWidth
    implicitHeight: content.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: legend.tooltip
    objectName: "chartLegend"

    Tk.Flex {
        id: content
        objectName: "legendItems"
        anchors.fill: parent
        direction: Tk.Flex.Row
        wrap: Tk.Flex.Wrap
        columnGap: Tk.Theme.space.md
        rowGap: legend.rowGap
        Repeater {
            model: legend.datasets
            delegate: Item {
                id: legendItem
                required property Tk.ChartDataset modelData
                readonly property var dataset: modelData
                implicitWidth: mark.width + check.implicitWidth + Tk.Theme.space.xs
                implicitHeight: Math.max(Tk.Theme.size.controlSm, check.implicitHeight)
                width: implicitWidth
                height: implicitHeight
                Accessible.role: Accessible.Grouping
                Accessible.name: dataset.name
                objectName: "legendItem_" + dataset.seriesId
                Rectangle {
                    id: mark
                    x: 0
                    y: (parent.height - height) / 2
                    width: Tk.Theme.size.iconSm
                    height: Tk.Theme.size.iconSm
                    radius: Tk.Theme.radius.xs
                    color: dataset.visible ? (dataset.hasColor ? dataset.color : Tk.Theme.color.accent)
                                           : Tk.Theme.color.textDisabled
                    border.color: Tk.Theme.color.borderStrong
                    border.width: Tk.Theme.size.border
                }
                Tk.CheckBox {
                    id: check
                    x: mark.width + Tk.Theme.space.xs
                    y: 0
                    text: dataset.name
                    checked: dataset.visible
                    tooltip: qsTr("Show %1").arg(dataset.name)
                    onToggled: {
                        dataset.visible = checked
                        legend.visibilityRequested(dataset.seriesId, checked)
                    }
                }
            }
        }
    }
}
