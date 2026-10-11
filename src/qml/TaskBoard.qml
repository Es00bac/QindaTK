// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Host snapshots are authoritative; movement and editing are proposals.
// AGENT-CONTRACT (docs/controls.md, CalDavProvider.conflicts): a move names
// the target status and index within it (-1 = end), so reorder within a
// column and moves between columns share moveRequested. Tasks may carry
// `syncState` ("synced", "conflict"); `conflicts` lists the provider's
// conflict entries ({collection, id, remoteFields, remoteDeleted}) and each
// resolution is a conflictResolutionRequested(requestId, taskId, choice).
Tk.Box {
    id: control
    objectName: "taskBoard"
    property var columns: [] // [{status,label,tasks:[{taskId,title,assignee,dueDate,priority,syncState}]}]
    // Category names resolve through this host palette; color is document data.
    property var categoryColors: ({})
    property var conflicts: []
    property bool offline: false
    property string errorText: ""
    property string filterText: ""
    property string tooltip: qsTr("Task board")
    property string lastRequestId: ""
    property int _requestSerial: 0
    property string requestState: "idle"
    property string requestMessage: ""
    // Pointer drag state: the dragged task and where it would land.
    property string dragTaskId: ""
    property string dropStatus: ""
    property int dropIndex: -1
    readonly property var taskConflicts: conflicts.filter(function(entry) { return String(entry.collection || "tasks") === "tasks" })
    signal moveRequested(string requestId, string taskId, string fromStatus, string toStatus, int targetIndex)
    signal editRequested(string requestId, string taskId)
    signal conflictResolutionRequested(string requestId, string taskId, string choice)
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: offline ? qsTr("Offline. Changes require host confirmation.") : errorText

    function nextId(prefix) { return prefix + "-" + Date.now() + "-" + (++_requestSerial) }
    function move(taskId, from, to, targetIndex) {
        if (!taskId || offline) return false
        const id = nextId("task-move"); lastRequestId = id; requestState = "pending"; requestMessage = ""
        moveRequested(id, taskId, from, to, targetIndex === undefined ? -1 : targetIndex)
        return true
    }
    function edit(taskId) { if (!taskId || offline) return false; const id = nextId("task-edit"); lastRequestId = id; requestState = "pending"; editRequested(id, taskId); return true }
    function resolveConflict(taskId, choice) {
        if (!taskId || offline || (choice !== "local" && choice !== "remote")) return false
        const id = nextId("task-conflict"); lastRequestId = id; requestState = "pending"; requestMessage = ""
        conflictResolutionRequested(id, taskId, choice)
        return true
    }
    function acknowledge(id, state, message) { if (id !== lastRequestId || requestState !== "pending") return false; requestState = state; requestMessage = message || ""; return true }
    function includes(task) { return filterText.length === 0 || (String(task.title || "") + " " + String(task.assignee || "")).toLowerCase().includes(filterText.toLowerCase()) }
    function columnIndex(status) { for (let i = 0; i < columns.length; ++i) if (String(columns[i].status) === status) return i; return -1 }
    function taskIndex(status, taskId) {
        const column = columns[columnIndex(status)]
        return column ? column.tasks.findIndex(function(task) { return String(task.taskId) === taskId }) : -1
    }
    // Keyboard moves: up/down reorder inside the column, left/right change column.
    function shift(taskId, status, delta, across) {
        const index = taskIndex(status, taskId)
        if (index < 0) return false
        if (across) {
            const target = columns[columnIndex(status) + delta]
            return target ? move(taskId, status, String(target.status), -1) : false
        }
        const count = columns[columnIndex(status)].tasks.length
        if (index + delta < 0 || index + delta >= count) return false
        return move(taskId, status, status, index + delta)
    }
    function titleOf(taskId) {
        for (let c = 0; c < columns.length; ++c)
            for (let t = 0; t < columns[c].tasks.length; ++t)
                if (String(columns[c].tasks[t].taskId) === taskId) return String(columns[c].tasks[t].title || taskId)
        return taskId
    }
    // Maps a scene point to {status, index} using the column and card geometry.
    function dropTarget(scenePoint) {
        for (let c = 0; c < columnRepeater.count; ++c) {
            const column = columnRepeater.itemAt(c)
            const local = column.mapFromItem(null, scenePoint.x, scenePoint.y)
            if (local.x < 0 || local.x > column.width || local.y < 0 || local.y > column.height) continue
            const cards = column.cards()
            let index = 0
            for (let i = 0; i < cards.length; ++i) {
                const card = cards[i]
                const point = card.mapFromItem(null, scenePoint.x, scenePoint.y)
                if (point.y > card.height / 2) index = i + 1
            }
            return { status: column.statusId, index: index }
        }
        return null
    }
    function finishDrag(taskId, fromStatus) {
        const status = dropStatus, index = dropIndex
        dragTaskId = ""; dropStatus = ""; dropIndex = -1
        if (!status) return false
        const from = taskIndex(fromStatus, taskId)
        // Dropping below itself in the same column counts the card once.
        const target = status === fromStatus && from >= 0 && index > from ? index - 1 : index
        if (status === fromStatus && target === from) return false
        return move(taskId, fromStatus, status, target)
    }

    Tk.Flex {
        direction: Tk.Flex.Column; gap: Tk.Theme.space.sm; width: parent.width
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center
            Tk.Heading { level: 2; text: qsTr("Tasks"); Tk.Flex.grow: 1 }
            Tk.TextField { objectName: "taskFilter"; placeholderText: qsTr("Filter tasks"); tooltip: qsTr("Filter by task or assignee"); Accessible.name: qsTr("Filter tasks"); onTextChanged: control.filterText = text }
            Tk.Badge { text: control.offline ? qsTr("Offline") : control.requestState; variant: control.offline ? "warning" : control.requestState === "conflict" ? "warning" : "default" }
        }
        Tk.Notice { objectName: "taskRequestNotice"; visible: control.offline || control.requestState === "rejected" || control.requestState === "conflict" || control.requestState === "offline"; variant: control.requestState === "rejected" && !control.offline ? "danger" : "warning"; title: control.offline ? qsTr("Offline board") : control.requestState === "conflict" ? qsTr("Changed elsewhere") : control.requestState === "offline" ? qsTr("Server unreachable") : qsTr("Task update failed"); text: control.requestMessage || control.errorText || qsTr("Moves are proposals until the host accepts them.") }
        Tk.Box {
            objectName: "taskConflicts"
            visible: control.taskConflicts.length > 0
            width: parent.width; padding: Tk.Theme.space.sm
            color: Tk.Theme.color.panelAlt; borderColor: Tk.Theme.color.warning
            Accessible.role: Accessible.Grouping; Accessible.name: qsTr("Sync conflicts")
            Tk.Flex {
                direction: Tk.Flex.Column; gap: Tk.Theme.space.xs; width: parent.width
                Tk.Label { text: qsTr("%n task(s) changed both here and on the server", "", control.taskConflicts.length); font.weight: Font.DemiBold }
                Repeater {
                    model: control.taskConflicts
                    delegate: Tk.Flex {
                        id: conflictRow
                        required property var modelData
                        readonly property string taskId: String(modelData.id || "")
                        objectName: "taskConflict_" + taskId
                        direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center; width: parent ? parent.width : 0
                        Tk.Icon { name: "triangle-alert"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.warning }
                        Tk.Label {
                            Tk.Flex.grow: 1; Tk.Flex.basis: 0; elide: Text.ElideRight
                            text: conflictRow.modelData.remoteDeleted ? qsTr("%1 · deleted on the server").arg(control.titleOf(conflictRow.taskId))
                                : qsTr("%1 · server has “%2” in %3").arg(control.titleOf(conflictRow.taskId)).arg(String((conflictRow.modelData.remoteFields || {}).title || ""))
                                    .arg(String((conflictRow.modelData.remoteFields || {}).status || ""))
                        }
                        Tk.Button { objectName: "taskKeepLocal"; small: true; text: qsTr("Keep mine"); tooltip: qsTr("Write this device's version to the server"); enabled: !control.offline; onClicked: control.resolveConflict(conflictRow.taskId, "local") }
                        Tk.Button { objectName: "taskUseRemote"; small: true; text: conflictRow.modelData.remoteDeleted ? qsTr("Delete here") : qsTr("Use server"); tooltip: qsTr("Replace this device's version with the server's"); enabled: !control.offline; onClicked: control.resolveConflict(conflictRow.taskId, "remote") }
                    }
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm
            Repeater {
                id: columnRepeater
                model: control.columns
                delegate: Tk.Box {
                    id: columnBox
                    required property var modelData
                    required property int index
                    property string statusId: String(modelData.status || "")
                    function cards() { const out = []; for (let i = 0; i < cardRepeater.count; ++i) { const card = cardRepeater.itemAt(i); if (card && card.visible) out.push(card) } return out }
                    objectName: "taskColumn_" + statusId
                    Tk.Flex.grow: 1; Tk.Flex.basis: Tk.Theme.size.fieldWidth
                    padding: Tk.Theme.space.sm; color: control.dropStatus === statusId ? Tk.Theme.color.selection : Tk.Theme.color.panelAlt
                    borderColor: control.dropStatus === statusId ? Tk.Theme.color.accent : Tk.Theme.color.divider
                    Accessible.role: Accessible.List; Accessible.name: String(modelData.label || statusId)
                    Tk.Flex {
                        direction: Tk.Flex.Column; gap: Tk.Theme.space.xs; width: parent.width
                        Tk.Flex {
                            direction: Tk.Flex.Row; gap: Tk.Theme.space.xs
                            Tk.Overline { title: String(columnBox.modelData.label || columnBox.statusId); wide: true; Tk.Flex.grow: 1 }
                            Tk.Badge { text: String(columnBox.modelData.tasks.length) }
                        }
                        Repeater {
                            id: cardRepeater
                            model: columnBox.modelData.tasks
                            delegate: Tk.Box {
                                id: card
                                required property var modelData
                                required property int index
                                property string taskId: String(modelData.taskId || "")
                                // AGENT-GUARD: a column delegate can be torn down while a new
                                // columns snapshot rebuilds nested repeaters; read its status
                                // through this guarded copy, never columnBox directly.
                                readonly property string statusId: columnBox ? columnBox.statusId : ""
                                readonly property bool conflicted: String(modelData.syncState || "") === "conflict"
                                objectName: "taskCard_" + taskId; visible: control.includes(modelData)
                                opacity: control.dragTaskId === taskId ? Tk.Theme.opacity.disabled : 1
                                padding: Tk.Theme.space.sm; color: Tk.Theme.color.surface
                                borderColor: card.activeFocus ? Tk.Theme.color.focus : conflicted ? Tk.Theme.color.warning : Tk.Theme.color.controlBorder
                                activeFocusOnTab: true
                                Accessible.role: Accessible.ListItem; Accessible.name: String(modelData.title || "") + ", " + columnBox.statusId
                                Accessible.description: [modelData.assignee, modelData.dueDate, conflicted ? qsTr("sync conflict") : ""].filter(Boolean).join(" · ")
                                Keys.onPressed: function(event) {
                                    if (!(event.modifiers & Qt.ControlModifier)) return
                                    const moves = {}
                                    moves[Qt.Key_Up] = [-1, false]; moves[Qt.Key_Down] = [1, false]
                                    moves[Qt.Key_Left] = [-1, true]; moves[Qt.Key_Right] = [1, true]
                                    const step = moves[event.key]
                                    if (!step) return
                                    control.shift(card.taskId, columnBox.statusId, step[0], step[1])
                                    event.accepted = true
                                }
                                TapHandler { onTapped: card.forceActiveFocus() }
                                DragHandler {
                                    id: dragger
                                    target: null
                                    enabled: !control.offline
                                    onActiveChanged: {
                                        if (active) { control.dragTaskId = card.taskId; return }
                                        if (control.dragTaskId === card.taskId) control.finishDrag(card.taskId, columnBox.statusId)
                                    }
                                    onCentroidChanged: {
                                        if (!active) return
                                        const target = control.dropTarget(centroid.scenePosition)
                                        control.dropStatus = target ? target.status : ""
                                        control.dropIndex = target ? target.index : -1
                                    }
                                }
                                Tk.Flex {
                                    direction: Tk.Flex.Column; gap: Tk.Theme.space.xs; width: parent.width
                                    Tk.Flex {
                                        direction: Tk.Flex.Row; gap: Tk.Theme.space.xs; align: Tk.Flex.Center; width: parent.width
                                        Tk.Icon { name: "grip-vertical"; size: Tk.Theme.size.iconSm; color: Tk.Theme.color.textMuted }
                                        Tk.Label { text: String(card.modelData.title || qsTr("Untitled task")); wrapMode: Text.Wrap; font.weight: Font.DemiBold; Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
                                        Tk.Badge { objectName: "taskSyncState"; visible: card.conflicted; text: qsTr("Conflict"); variant: "warning" }
                                    }
                                    Tk.TaskCardDetails {
                                        width: parent.width
                                        categories: card.modelData.categories || []
                                        categoryColors: control.categoryColors
                                        fields: card.modelData.extraFields || []
                                    }
                                    Tk.Label { text: [card.modelData.assignee, card.modelData.dueDate, card.modelData.priority].filter(Boolean).join(" · "); muted: true }
                                    Tk.Flex {
                                        direction: Tk.Flex.Row; gap: Tk.Theme.space.xs; wrap: Tk.Flex.Wrap; width: parent.width
                                        Tk.Button { text: qsTr("Edit"); small: true; tooltip: qsTr("Request edit for %1").arg(card.taskId); enabled: !control.offline; onClicked: control.edit(card.taskId) }
                                        Tk.IconButton { objectName: "taskMoveUp"; small: true; iconName: "arrow-up"; tooltip: qsTr("Move up (Ctrl+Up)"); enabled: !control.offline && card.index > 0; onClicked: control.shift(card.taskId, columnBox.statusId, -1, false) }
                                        Tk.IconButton { objectName: "taskMoveDown"; small: true; iconName: "arrow-down"; tooltip: qsTr("Move down (Ctrl+Down)"); enabled: !control.offline && card.index + 1 < cardRepeater.count; onClicked: control.shift(card.taskId, columnBox.statusId, 1, false) }
                                        Repeater {
                                            model: control.columns
                                            delegate: Tk.Button {
                                                required property var modelData
                                                visible: !!card && String(modelData.status || "") !== card.statusId
                                                text: String(modelData.label || modelData.status || "")
                                                small: true
                                                Accessible.name: qsTr("Move to %1").arg(String(modelData.label || modelData.status || ""))
                                                tooltip: Accessible.name; enabled: !control.offline
                                                onClicked: control.move(card.taskId, card.statusId, String(modelData.status || ""), -1)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        Tk.Label { objectName: "taskRequestState"; text: control.requestMessage || control.requestState; muted: true }
    }
    Tk.ToolTip { text: control.tooltip; visible: control.activeFocus }
}
