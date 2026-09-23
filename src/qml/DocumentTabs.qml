// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Lifecycle-aware view tabs. Rows are rebuilt from DocumentManager state;
// close/reorder requests never mutate or infer document identity locally.
Item {
    id: tabs
    property var documentManager: null
    property string currentViewId: ""
    property string errorText: ""
    property var pendingDocuments: ({})
    property var entries: projectEntries()
    property string tooltip: qsTr("Open documents")
    readonly property string activeDocumentId: documentManager && documentManager.activeSession
                                            ? documentManager.activeSession.documentId : ""

    signal viewActivated(string documentId, string viewId)
    signal closeRequested(string documentId, string viewId)
    signal closeFailed(string documentId, string reason)
    signal reorderRequested(string viewId, string beforeViewId)

    implicitWidth: viewport.implicitWidth
    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.PageTabList
    Accessible.name: tooltip

    function projectEntries() {
        if (!documentManager || !documentManager.sessions || !documentManager.viewsFor) return []
        const byView = ({})
        const sessions = documentManager.sessions
        for (let i = 0; i < sessions.length; ++i) {
            const session = sessions[i]
            if (!session) continue
            const docId = String(session.documentId)
            const views = documentManager.viewsFor(docId)
            for (let j = 0; j < views.length; ++j)
                byView[String(views[j])] = { documentId: docId, viewId: String(views[j]), title: String(session.title),
                                             dirty: session.dirty, saving: session.saving, readOnly: session.readOnly }
        }
        const order = documentManager.viewOrder || Object.keys(byView)
        const result = []
        for (let i = 0; i < order.length; ++i) if (byView[String(order[i])]) result.push(byView[String(order[i])])
        return result
    }
    function refresh() {
        entries = projectEntries()
        const nextPending = Object.assign({}, pendingDocuments)
        for (const docId in nextPending)
            if (!documentManager || !documentManager.findSession(docId)) delete nextPending[docId]
        pendingDocuments = nextPending
    }
    function entryFor(viewId) {
        for (let i = 0; i < entries.length; ++i)
            if (entries[i].viewId === String(viewId)) return entries[i]
        return null
    }
    function activate(entry) {
        if (!entry || !documentManager || isPending(entry.documentId)) return false
        if (!documentManager.activate(entry.documentId)) return false
        viewActivated(entry.documentId, entry.viewId)
        return true
    }
    function isPending(documentId) { return pendingDocuments[String(documentId)] === true }
    function markPending(documentId, pending) {
        const next = Object.assign({}, pendingDocuments)
        if (pending) next[String(documentId)] = true
        else delete next[String(documentId)]
        pendingDocuments = next
    }
    function requestClose(viewId) {
        const entry = entryFor(viewId)
        if (!entry || !documentManager || isPending(entry.documentId)) return false
        const views = documentManager.viewsFor(entry.documentId)
        if (views.length > 1) {
            markPending(entry.documentId, true)
            if (documentManager.removeView(entry.documentId, entry.viewId)) {
                markPending(entry.documentId, false)
                refresh()
                return true
            }
            markPending(entry.documentId, false)
            return false
        }
        if (entry.dirty) {
            markPending(entry.documentId, true)
            closeRequested(entry.documentId, entry.viewId)
            return true
        }
        markPending(entry.documentId, true)
        // ClosePolicy::Cancel is the first value; it safely closes only clean sessions.
        const accepted = documentManager.closeDocument(entry.documentId, 0)
        if (!accepted) markPending(entry.documentId, false)
        refresh()
        return accepted
    }
    function completeClose(documentId, viewId, policy) {
        const entry = entryFor(viewId)
        if (!entry || entry.documentId !== String(documentId) || !isPending(documentId)) return false
        const accepted = documentManager.closeDocument(String(documentId), policy)
        if (!accepted) markPending(documentId, false)
        refresh()
        return accepted
    }
    function closeOthers(viewId) {
        if (!entryFor(viewId)) return 0
        let requested = 0
        const snapshot = entries.slice(0)
        for (let i = 0; i < snapshot.length; ++i) {
            if (snapshot[i].viewId !== String(viewId) && requestClose(snapshot[i].viewId)) ++requested
        }
        return requested
    }
    function requestReorder(viewId, targetIndex) {
        const from = entries.findIndex(function(item) { return item.viewId === String(viewId) })
        if (from < 0 || !documentManager || !documentManager.moveView) return false
        let insertion = Math.max(0, Math.min(entries.length, targetIndex + (targetIndex > from ? 1 : 0)))
        let before = entries[insertion] || null
        if (before && before.viewId === String(viewId)) before = entries[++insertion] || null
        const beforeId = before ? before.viewId : ""
        if (!documentManager.moveView(String(viewId), beforeId)) return false
        reorderRequested(String(viewId), beforeId)
        refresh()
        return true
    }
    function indexAt(x) {
        for (let i = 0; i < tabRepeater.count; ++i) {
            const delegate = tabRepeater.itemAt(i)
            if (delegate && x < delegate.x + delegate.width / 2) return i
        }
        return Math.max(0, tabRepeater.count - 1)
    }
    onDocumentManagerChanged: refresh()
    Component.onCompleted: refresh()
    Connections {
        target: tabs.documentManager
        function onChanged() { tabs.refresh() }
        function onCloseBlocked(documentId, reason) {
            tabs.markPending(documentId, false)
            tabs.errorText = reason
            tabs.closeFailed(documentId, reason)
            tabs.refresh()
        }
    }

    Tk.Flex {
        id: column
        width: tabs.width
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.Scroll {
            id: viewport
            objectName: "documentTabsViewport"
            width: tabs.width
            height: Tk.Theme.size.tab
            overflowX: Tk.Scroll.Auto
            overflowY: Tk.Scroll.Hidden
            accessibleName: tabs.tooltip
            Item {
                id: rowHost
                anchors.left: parent.left
                anchors.top: parent.top
                width: Math.max(viewport.viewportWidth,
                                tabs.entries.length * (Tk.Theme.size.fieldWidth + Tk.Theme.size.controlSm))
                implicitWidth: tabs.entries.length * (Tk.Theme.size.fieldWidth + Tk.Theme.size.controlSm)
                implicitHeight: row.implicitHeight
            Tk.Flex {
                id: row
                anchors.fill: parent
                direction: Tk.Flex.Row
                align: Tk.Flex.Stretch
                gap: Tk.Theme.space.xs
                Repeater {
                    id: tabRepeater
                    model: tabs.entries
                    delegate: Item {
                        id: tabSlot
                        required property int index
                        required property var modelData
                        Tk.Flex.shrink: 0
                        readonly property bool active: modelData.viewId === tabs.currentViewId
                            || (tabs.currentViewId.length === 0 && modelData.documentId === tabs.activeDocumentId && !tabs.entries.some(function(e, i) { return e.documentId === modelData.documentId && i < index }))
                        readonly property alias tabControl: tabButton
                        objectName: "documentTab_" + modelData.viewId
                        implicitWidth: tabButton.width + (closeButton.visible ? closeButton.width : 0)
                        implicitHeight: Tk.Theme.size.tab
                        Tk.Flex.minWidth: implicitWidth
                        T.TabButton {
                            id: tabButton
                            width: Math.max(Tk.Theme.size.fieldWidth, Math.min(Tk.Theme.size.dialogWidth / 2, label.implicitWidth + leftPadding + rightPadding))
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            readonly property string titleText: modelData.title + (modelData.dirty ? " •" : "")
                            text: titleText
                            checked: tabSlot.active
                            checkable: false
                            focusPolicy: Qt.TabFocus
                            enabled: !tabs.isPending(modelData.documentId)
                            objectName: "documentTabSelect_" + modelData.viewId
                            Accessible.role: Accessible.PageTab
                            Accessible.name: modelData.title + (modelData.dirty ? qsTr(", unsaved changes") : "")
                            Accessible.description: modelData.saving ? qsTr("Saving") : modelData.readOnly ? qsTr("Read-only") : ""
                            Accessible.selected: tabSlot.active
                            Accessible.focusable: enabled
                            onClicked: tabs.activate(modelData)
                            Keys.onPressed: function(event) {
                                if (event.key === Qt.Key_Left || event.key === Qt.Key_Right || event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                                    const next = event.key === Qt.Key_Home ? 0 : event.key === Qt.Key_End ? tabs.entries.length - 1 : index + (event.key === Qt.Key_Right ? 1 : -1)
                                    const item = tabRepeater.itemAt(Math.max(0, Math.min(tabs.entries.length - 1, next)))
                                    if (item) { item.tabControl.forceActiveFocus(Qt.TabFocusReason); tabs.activate(item.modelData) }
                                    event.accepted = true
                                }
                            }
                            contentItem: Tk.Label {
                                id: label
                                text: tabButton.titleText
                                elide: Text.ElideRight
                                color: tabSlot.active ? Tk.Theme.color.text : Tk.Theme.color.textMuted
                            }
                            background: Rectangle {
                                color: tabSlot.active ? Tk.Theme.color.panelAlt : tabButton.hovered ? Tk.Theme.color.hover : "transparent"
                                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 2; color: Tk.Theme.color.accent; visible: tabSlot.active }
                            }
                            Tk.ToolTip { text: modelData.title; visible: tabButton.hovered }
                        }
                        Tk.IconButton {
                            id: closeButton
                            anchors.left: tabButton.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: Tk.Theme.size.controlSm
                            height: Tk.Theme.size.controlSm
                            iconName: "x"
                            tooltip: modelData.dirty ? qsTr("Close %1 with unsaved changes").arg(modelData.title) : qsTr("Close %1").arg(modelData.title)
                            Accessible.name: tooltip
                            enabled: !tabs.isPending(modelData.documentId)
                            onClicked: tabs.requestClose(modelData.viewId)
                        }
                        DragHandler {
                            target: null
                            onActiveChanged: {
                                if (active) return
                                const point = row.mapFromItem(tabSlot, centroid.position.x, 0)
                                tabs.requestReorder(modelData.viewId, tabs.indexAt(point.x))
                            }
                        }
                    }
                }
            }
            }
        }
        Tk.Caption {
            objectName: "documentTabError"
            visible: tabs.errorText.length > 0
            text: tabs.errorText
            color: Tk.Theme.color.danger
            wrapMode: Text.Wrap
        }
    }
}
