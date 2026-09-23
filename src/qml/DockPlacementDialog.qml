// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// AGENT-CONTRACT: drafts describe intent only; DockModel owns arrangement.
// Host ownership keeps this dialog alive when applying rebuilds panel frames.
Tk.Dialog {
    id: dialog
    required property var host
    property string panelId: ""
    property var choices: []
    property var openingModel: null
    property string openingWorkspace: ""
    readonly property var selected: choices[destination.currentIndex] || ({})
    readonly property bool validPanel: host.enabled && host.model !== null
        && host.model === openingModel && host.workspace === openingWorkspace
        && host.model.hasPanel(panelId) && host.panelInfo(panelId).visible === true
    objectName: "dockPlacementDialog"
    title: qsTr("Place %1").arg(host.panelTitle(panelId))
    subtitle: qsTr("Choose a destination, or move a floating panel by its horizontal and vertical position. Apply commits; Cancel leaves placement unchanged.")
    primaryText: qsTr("Apply")
    primaryEnabled: validPanel && choices.length > 0 && !horizontal.inputInvalid && !vertical.inputInvalid

    function begin(id) {
        if (!host.enabled || !host.model || !host.model.hasPanel(id)) return
        const info = host.panelInfo(id)
        if (!info.visible) return
        panelId = id
        openingModel = host.model
        openingWorkspace = host.workspace
        const list = []
        if (info.floating) list.push({ text: qsTr("Move floating panel"), kind: "move" })
        const zones = ["left", "right", "top", "bottom", "center", "overlay"]
        const names = [qsTr("Left"), qsTr("Right"), qsTr("Top"), qsTr("Bottom"), qsTr("Center"), qsTr("Overlay")]
        for (let i = 0; i < zones.length; ++i)
            if (host.model.acceptsZone(id, zones[i]))
                list.push({ text: qsTr("Dock in %1 zone").arg(names[i]), kind: "zone", zone: zones[i] })
        const placements = ["before", "after", "lane-before", "lane-after", "tab"]
        const labels = [qsTr("Before %1"), qsTr("After %1"), qsTr("Lane before %1"), qsTr("Lane after %1"), qsTr("Tab with %1")]
        for (const target of host.model.panelIds()) {
            const p = host.panelInfo(target)
            if (target === id || (!p.docked && !p.collapsed) || !host.model.acceptsZone(id, p.zone)) continue
            for (let j = 0; j < placements.length; ++j)
                list.push({ text: labels[j].arg(host.panelTitle(target)), kind: "beside", target: target, placement: placements[j] })
        }
        if (info.group && (info.docked || info.collapsed)) {
            for (const lane of host.model.lanes(info.zone)) {
                for (const slot of lane.slots) {
                    if (slot.panelIds.indexOf(id) < 0) continue
                    for (let k = 0; k < slot.panelIds.length; ++k)
                        list.push({ text: qsTr("Tab position %1 of %2").arg(k + 1).arg(slot.panelIds.length), kind: "order", index: k, group: info.group })
                }
            }
        }
        choices = list
        destination.currentIndex = 0
        horizontal.value = info.x || 0
        vertical.value = info.y || 0
        open()
    }

    function applyPlacement() {
        if (!primaryEnabled) return
        const choice = selected
        const info = host.panelInfo(panelId)
        if (choice.kind === "move" && info.floating) {
            host.model.moveFloating(panelId, Math.max(0, Math.min(horizontal.value, horizontal.to)),
                                    Math.max(0, Math.min(vertical.value, vertical.to)))
        } else if (choice.kind === "zone" && host.model.acceptsZone(panelId, choice.zone)) {
            host.model.dock(panelId, choice.zone)
        } else if (choice.kind === "beside" && host.model.hasPanel(choice.target)) {
            const target = host.panelInfo(choice.target)
            if ((!target.docked && !target.collapsed) || !host.model.acceptsZone(panelId, target.zone)) return
            host.model.dockBeside(panelId, choice.target, choice.placement)
        } else if (choice.kind === "order" && info.group === choice.group && (info.docked || info.collapsed)) {
            host.model.moveInGroup(panelId, choice.index)
        } else return
        host.panelPlacementAccepted(panelId)
    }
    onOpened: placementApply.forceActiveFocus(Qt.TabFocusReason)
    onAccepted: applyPlacement()
    onClosed: host.focusPanelPlacement(panelId)

    footer: Tk.Box {
        padding: Tk.Theme.space.md
        borderTop: Tk.Theme.size.border
        borderColor: Tk.Theme.color.divider
        Tk.Flex {
            direction: Tk.Flex.Row
            justify: Tk.Flex.End
            gap: Tk.Theme.space.sm
            Tk.Button {
                objectName: "dialogSecondary"
                text: qsTr("Cancel")
                onClicked: dialog.reject()
                Accessible.onPressAction: if (enabled) dialog.reject()
            }
            Tk.Button {
                id: placementApply
                objectName: "dialogPrimary"
                text: qsTr("Apply")
                variant: "accent"
                available: dialog.primaryEnabled
                onClicked: dialog.accept()
                Accessible.onPressAction: if (enabled) dialog.accept()
            }
        }
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.ComboBox {
            id: destination
            objectName: "dockPlacementDestination"
            tooltip: qsTr("Panel destination")
            model: dialog.choices
            textRole: "text"
        }
        Tk.NumberField {
            id: horizontal
            objectName: "dockPlacementX"
            visible: dialog.selected.kind === "move"
            tooltip: qsTr("Horizontal position")
            label: qsTr("Horizontal position")
            from: 0
            to: Math.max(0, dialog.host.width - (dialog.host.panelInfo(dialog.panelId).width || 0))
            stepSize: Tk.Theme.space.lg
            decimals: 1
        }
        Tk.NumberField {
            id: vertical
            objectName: "dockPlacementY"
            visible: dialog.selected.kind === "move"
            tooltip: qsTr("Vertical position")
            label: qsTr("Vertical position")
            from: 0
            to: Math.max(0, dialog.host.height - (dialog.host.panelInfo(dialog.panelId).height || 0))
            stepSize: Tk.Theme.space.lg
            decimals: 1
        }
    }
}
