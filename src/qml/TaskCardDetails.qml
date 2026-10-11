// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// D-336: card metadata remains host-owned; category colours are record data.
Tk.Flex {
    id: control
    property var categories: [] // names, or {name, color}
    property var categoryColors: ({})
    property var fields: [] // [{label, value}]
    direction: Tk.Flex.Column
    gap: Tk.Theme.space.xs
    visible: categories.length > 0 || fields.length > 0
    Tk.Flex {
        width: parent.width
        direction: Tk.Flex.Row
        wrap: Tk.Flex.Wrap
        gap: Tk.Theme.space.xs
        Repeater {
            model: control.categories
            delegate: Tk.Box {
                id: categoryBox
                required property var modelData
                readonly property string name: typeof modelData === "string" ? modelData : String(modelData.name || "")
                objectName: "taskCategory_" + name
                padding: Tk.Theme.space.xs
                radius: Tk.Theme.radius.sm
                color: Tk.Theme.color.surface
                tooltip: name
                Accessible.role: Accessible.StaticText
                Accessible.name: name
                Tk.Flex {
                    direction: Tk.Flex.Row
                    align: Tk.Flex.Center
                    gap: Tk.Theme.space.xs
                    Rectangle {
                        objectName: "taskCategorySwatch_" + categoryBox.name
                        implicitWidth: Tk.Theme.size.iconSm
                        implicitHeight: Tk.Theme.size.iconSm
                        Tk.Flex.shrink: 0
                        radius: Tk.Theme.radius.sm
                        color: categoryBox.modelData.color || control.categoryColors[categoryBox.name] || Tk.Theme.color.accent
                        Accessible.ignored: true
                    }
                    Tk.Caption { text: categoryBox.name }
                }
            }
        }
    }
    Repeater {
        model: control.fields
        delegate: Tk.Caption {
            required property var modelData
            objectName: "taskExtraField_" + String(modelData.label || "")
            text: String(modelData.label || "") + ": " + String(modelData.value ?? "")
            wrapMode: Text.Wrap
            width: control.width
        }
    }
}
