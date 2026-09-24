// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT (docs/extension-registry.md): registering a local descriptor
// only reads JSON. Loading is always a separate, explicit user action because
// a native plugin runs with the host process's permissions.
Tk.Flex {
    id: control
    objectName: "extensionManager"
    property Tk.ExtensionRegistry registry: null
    property string tooltip: qsTr("Manage trusted local extensions")
    property string actionMessage: ""
    readonly property var entries: registry ? registry.extensions : []

    direction: Tk.Flex.Column
    align: Tk.Flex.Stretch
    gap: Tk.Theme.space.sm
    padding: Tk.Theme.space.md
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip

    function addDescriptor(path) {
        if (!registry || !path.trim().length) return false
        const ok = registry.registerDescriptor(path.trim())
        actionMessage = ok ? qsTr("Descriptor added. Review it before loading.")
                           : qsTr("Could not add descriptor. Enter an absolute local JSON path.")
        if (ok) descriptorPath.text = ""
        return ok
    }
    function changeLoaded(id, shouldLoad) {
        if (!registry) return false
        const ok = shouldLoad ? registry.load(id) : registry.unload(id)
        const entry = registry.extension(id)
        actionMessage = ok ? (shouldLoad ? qsTr("Extension loaded: %1").arg(id)
                                      : qsTr("Extension unloaded: %1").arg(id))
                           : (entry.diagnostic || qsTr("Extension action failed"))
        return ok
    }

    Tk.Flex {
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.sm
        Tk.Overline { title: qsTr("Extensions"); wide: true; Tk.Flex.grow: 1 }
        Tk.Caption { text: qsTr("%1 known").arg(control.entries.length) }
    }
    Tk.Caption {
        text: qsTr("Native extensions run in this application. Add only a descriptor you trust, then choose Load.")
        wrapMode: Text.WordWrap
    }
    Tk.Flex {
        direction: Tk.Flex.Row
        gap: Tk.Theme.space.xs
        Tk.TextField {
            id: descriptorPath
            objectName: "extensionDescriptorPath"
            Tk.Flex.grow: 1
            placeholderText: qsTr("Absolute path to extension JSON")
            tooltip: qsTr("Local extension descriptor path")
            onAccepted: control.addDescriptor(text)
        }
        Tk.Button {
            objectName: "extensionAdd"
            text: qsTr("Add descriptor")
            iconName: "plus"
            tooltip: qsTr("Read the selected local extension descriptor")
            available: !!control.registry && descriptorPath.text.trim().length > 0
            onClicked: control.addDescriptor(descriptorPath.text)
        }
    }
    Tk.Caption {
        objectName: "extensionActionMessage"
        visible: control.actionMessage.length > 0
        text: control.actionMessage
        wrapMode: Text.WordWrap
    }
    Tk.Scroll {
        objectName: "extensionListScroll"
        Tk.Flex.grow: 1
        overflowY: Tk.Scroll.Auto
        Tk.Flex {
            id: listColumn
            width: parent.width
            direction: Tk.Flex.Column
            align: Tk.Flex.Stretch
            gap: Tk.Theme.space.xs
            Tk.Caption {
                visible: control.entries.length === 0
                text: qsTr("No extensions have been added.")
            }
            Repeater {
                model: control.entries
                delegate: Item {
                    required property var modelData
                    readonly property var entry: modelData
                    objectName: "extension_" + entry.id
                    width: listColumn.width
                    implicitHeight: entryContent.height
                    Accessible.role: Accessible.ListItem
                    Accessible.name: entry.id + ", " + (entry.loaded ? qsTr("loaded") : qsTr("unloaded"))
                    Accessible.description: entry.diagnostic || ""

                    Rectangle {
                        anchors.fill: parent
                        color: Tk.Theme.color.controlBg
                        radius: Tk.Theme.radius.sm
                        border.width: Tk.Theme.size.border
                        border.color: Tk.Theme.color.controlBorder
                    }
                    Tk.Flex {
                        id: entryContent
                        width: parent.width
                        align: Tk.Flex.Stretch
                        gap: Tk.Theme.space.xs
                        padding: Tk.Theme.space.sm
                        direction: Tk.Flex.Column
                        Tk.Flex {
                            direction: Tk.Flex.Row
                            align: Tk.Flex.Center
                            gap: Tk.Theme.space.sm
                            Tk.Label { text: entry.id; Tk.Flex.grow: 1 }
                            Tk.Mono { text: entry.version || qsTr("Unknown version") }
                            Tk.Badge { text: entry.loaded ? qsTr("LOADED") : qsTr("UNLOADED") }
                            Tk.Button {
                                objectName: "extensionLoad_" + entry.id
                                text: entry.loaded ? qsTr("Unload") : qsTr("Load")
                                variant: entry.loaded ? "default" : "accent"
                                tooltip: entry.loaded ? qsTr("Stop and unload this extension")
                                                      : qsTr("Run this trusted native extension")
                                available: entry.loaded || (entry.installed && !entry.diagnostic)
                                onClicked: control.changeLoaded(entry.id, !entry.loaded)
                            }
                        }
                        Tk.Caption {
                            objectName: "extensionDiagnostic_" + entry.id
                            visible: !!entry.diagnostic
                            text: entry.diagnostic || ""
                            color: Tk.Theme.color.warning
                            wrapMode: Text.WordWrap
                        }
                        Tk.Caption {
                            text: entry.descriptorPath || qsTr("Descriptor is unavailable")
                            wrapMode: Text.WrapAnywhere
                        }
                        Tk.Caption {
                            visible: !!entry.operations && entry.operations.length > 0
                            text: qsTr("Operations: %1").arg((entry.operations || []).join(", "))
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }
}
