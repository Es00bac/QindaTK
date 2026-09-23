// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    width: 960
    height: 640

    Tk.EventLogModel {
        id: eventModel
        objectName: "installedEventModel"
    }
    Tk.DiffModel {
        id: diffModel
        objectName: "installedDiffModel"
    }
    Tk.ResourcePlanModel {
        id: resourceModel
        objectName: "installedResourceModel"
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.EventLogView {
            objectName: "installedEventLogView"
            model: eventModel
            Tk.Flex.grow: 1
            tooltip: qsTr("Installed event log")
        }
        Tk.DiffView {
            objectName: "installedDiffView"
            diffModel: diffModel
            Tk.Flex.grow: 1
            tooltip: qsTr("Installed diff")
        }
        Tk.ResourceTable {
            objectName: "installedResourceTable"
            model: resourceModel
            Tk.Flex.grow: 1
            tooltip: qsTr("Installed resource plan")
        }
    }
}
