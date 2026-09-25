// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Comments and tracked changes are anchored by the review model. Buttons
// emit stable IDs and never mutate an anchor or revision locally.
Item {
    id: control
    property var items: [] // [{id,kind,author,time,summary,body,status,location,threadId,replies,canAccept,canReject,error}]
    property string selectedItemId: ""
    property string replyDraft: ""
    property string errorText: ""
    property string tooltip: qsTr("Review comments and tracked changes")
    readonly property var selectedItem: itemById(selectedItemId)
    readonly property bool selectedIsChange: selectedItem !== null && selectedItem.kind === "change"
    signal navigateRequested(string itemId, var location)
    signal acceptChangeRequested(string changeId)
    signal rejectChangeRequested(string changeId)
    signal replyRequested(string threadId, string text)
    signal resolveThreadRequested(string threadId)

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: body.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    function itemById(id) {
        for (let i = 0; i < items.length; ++i) if (items[i].id === id) return items[i]
        return null
    }
    function selectItem(id) {
        if (enabled && itemById(id)) selectedItemId = id
    }
    function navigate() {
        if (enabled && selectedItem)
            navigateRequested(selectedItem.id, selectedItem.location || null)
    }
    function sendReply() {
        if (enabled && selectedItem && selectedItem.threadId && replyDraft.trim().length > 0)
            replyRequested(selectedItem.threadId, replyDraft.trim())
    }

    Tk.Flex {
        id: body
        width: control.width > 0 ? control.width : implicitWidth
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Overline { title: qsTr("REVIEW"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Caption { text: qsTr("%1 open").arg(control.items.length) }
        }
        Tk.Caption {
            objectName: "reviewError"
            visible: control.errorText.length > 0
            color: Tk.Theme.color.danger
            text: control.errorText
            wrapMode: Text.WordWrap
        }
        Tk.Scroll {
            objectName: "reviewListScroll"
            implicitHeight: Math.min(reviewList.implicitHeight, Tk.Theme.size.menuItem * 8)
            overflowY: Tk.Scroll.Auto
            Tk.Flex {
                id: reviewList
                direction: Tk.Flex.Column
                align: Tk.Flex.Stretch
                gap: Tk.Theme.space.xs
                Repeater {
                    model: control.items
                    // AGENT-NOTE: rows read from the start and elide at the end.
                    delegate: Tk.ListRow {
                        required property var modelData
                        objectName: "reviewItem_" + modelData.id
                        text: (modelData.kind === "change" ? qsTr("Change") : qsTr("Comment"))
                              + " · " + (modelData.author || qsTr("Unknown author")) + " · " + modelData.summary
                        selected: modelData.id === control.selectedItemId
                        tooltip: qsTr("Select review item by %1").arg(modelData.author || qsTr("Unknown author"))
                        onClicked: control.selectItem(modelData.id)
                    }
                }
            }
        }
        Tk.Caption {
            objectName: "reviewItemDetail"
            visible: control.selectedItem !== null
            text: control.selectedItem
                  ? qsTr("%1 · %2 · %3").arg(control.selectedItem.author || qsTr("Unknown author"))
                    .arg(control.selectedItem.time || qsTr("Time unavailable"))
                    .arg(control.selectedItem.body || control.selectedItem.summary || "") : ""
            wrapMode: Text.WordWrap
        }
        Tk.Caption {
            objectName: "reviewAnchorStatus"
            visible: control.selectedItem !== null
                     && (control.selectedItem.status === "orphaned" || !!control.selectedItem.error)
            color: Tk.Theme.color.warning
            text: control.selectedItem
                  ? control.selectedItem.error || qsTr("This review anchor could not be resolved.") : ""
            wrapMode: Text.WordWrap
        }
        Tk.TextArea {
            objectName: "reviewReply"
            visible: control.selectedItem !== null && !!control.selectedItem.threadId
            rows: 2
            text: control.replyDraft
            placeholderText: qsTr("Write a reply")
            tooltip: qsTr("Reply to the selected comment thread")
            onTextChanged: control.replyDraft = text
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.xs
            Tk.Button {
                objectName: "reviewNavigate"
                text: qsTr("Go to")
                iconName: "navigation"
                tooltip: qsTr("Navigate to the selected review anchor")
                enabled: control.selectedItem !== null && control.selectedItem.status !== "orphaned"
                onClicked: control.navigate()
            }
            Tk.Button {
                objectName: "reviewAccept"
                text: qsTr("Accept")
                iconName: "check"
                variant: "accent"
                tooltip: qsTr("Accept the selected tracked change")
                visible: control.selectedIsChange
                enabled: control.selectedIsChange && control.selectedItem.canAccept !== false
                onClicked: control.acceptChangeRequested(control.selectedItemId)
            }
            Tk.Button {
                objectName: "reviewReject"
                text: qsTr("Reject")
                iconName: "x"
                variant: "danger"
                tooltip: qsTr("Reject the selected tracked change")
                visible: control.selectedIsChange
                enabled: control.selectedIsChange && control.selectedItem.canReject !== false
                onClicked: control.rejectChangeRequested(control.selectedItemId)
            }
            Tk.Button {
                objectName: "reviewSendReply"
                text: qsTr("Reply")
                iconName: "corner-up-left"
                tooltip: qsTr("Add a reply to the selected thread")
                visible: control.selectedItem !== null && !!control.selectedItem.threadId
                enabled: control.replyDraft.trim().length > 0
                onClicked: control.sendReply()
            }
            Tk.Button {
                objectName: "reviewResolve"
                text: qsTr("Resolve thread")
                iconName: "circle-check"
                tooltip: qsTr("Resolve the selected comment thread")
                visible: control.selectedItem !== null && !!control.selectedItem.threadId
                enabled: control.selectedItem !== null && control.selectedItem.status !== "resolved"
                onClicked: control.resolveThreadRequested(control.selectedItem.threadId)
            }
        }
        Tk.Caption {
            visible: control.items.length === 0
            color: Tk.Theme.color.textMuted
            text: qsTr("No comments or tracked changes are available.")
        }
    }
}
