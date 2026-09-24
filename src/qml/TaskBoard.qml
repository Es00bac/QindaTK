// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Host snapshots are authoritative; movement and editing are proposals.
Tk.Box {
    id: control
    objectName: "taskBoard"
    property var columns: [] // [{status,label,tasks:[{taskId,title,assignee,dueDate,priority}]}]
    property bool offline: false
    property string errorText: ""
    property string filterText: ""
    property string tooltip: qsTr("Task board")
    property string lastRequestId: ""
    property int _requestSerial: 0
    property string requestState: "idle"
    property string requestMessage: ""
    signal moveRequested(string requestId, string taskId, string fromStatus, string toStatus)
    signal editRequested(string requestId, string taskId)
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip
    Accessible.description: offline ? qsTr("Offline. Changes require host confirmation.") : errorText

    function move(taskId, from, to) { if (!taskId || offline) return false; const id = "task-move-" + Date.now() + "-" + (++_requestSerial); lastRequestId = id; requestState = "pending"; requestMessage = ""; moveRequested(id, taskId, from, to); return true }
    function edit(taskId) { if (!taskId || offline) return false; const id = "task-edit-" + Date.now() + "-" + (++_requestSerial); lastRequestId = id; requestState = "pending"; editRequested(id, taskId); return true }
    function acknowledge(id, state, message) { if (id !== lastRequestId || requestState !== "pending") return false; requestState = state; requestMessage = message || ""; return true }
    function includes(task) { return filterText.length === 0 || (String(task.title || "") + " " + String(task.assignee || "")).toLowerCase().includes(filterText.toLowerCase()) }

    Tk.Flex {
        direction: Tk.Flex.Column; gap: Tk.Theme.space.sm; width: parent.width
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm; align: Tk.Flex.Center
            Tk.Heading { level: 2; text: qsTr("Tasks"); Tk.Flex.grow: 1 }
            Tk.TextField { objectName: "taskFilter"; placeholderText: qsTr("Filter tasks"); tooltip: qsTr("Filter by task or assignee"); Accessible.name: qsTr("Filter tasks"); onTextChanged: control.filterText = text }
            Tk.Badge { text: control.offline ? qsTr("Offline") : control.requestState; variant: control.offline ? "warning" : "default" }
        }
        Tk.Notice { visible: control.offline || control.requestState === "rejected"; variant: control.offline ? "warning" : "danger"; title: control.offline ? qsTr("Offline board") : qsTr("Task update failed"); text: control.requestMessage || control.errorText || qsTr("Moves are proposals until the host accepts them.") }
        Tk.Flex {
            direction: Tk.Flex.Row; gap: Tk.Theme.space.sm
            Repeater {
                model: control.columns
                delegate: Tk.Box {
                    required property var modelData
                    property string statusId: String(modelData.status || "")
                    objectName: "taskColumn_" + statusId
                    Tk.Flex.grow: 1; Tk.Flex.basis: Tk.Theme.size.fieldWidth
                    padding: Tk.Theme.space.sm; color: Tk.Theme.color.panelAlt; borderColor: Tk.Theme.color.divider
                    Accessible.role: Accessible.List; Accessible.name: String(modelData.label || statusId)
                    Tk.Flex {
                        direction: Tk.Flex.Column; gap: Tk.Theme.space.xs; width: parent.width
                        Tk.Flex {
                            direction: Tk.Flex.Row; gap: Tk.Theme.space.xs
                            Tk.Overline { title: String(modelData.label || statusId); wide: true; Tk.Flex.grow: 1 }
                            Tk.Badge { text: String(modelData.tasks.length) }
                        }
                        Repeater {
                            model: modelData.tasks
                            delegate: Tk.Box {
                                required property var modelData
                                property string taskId: String(modelData.taskId || "")
                                objectName: "taskCard_" + taskId; visible: control.includes(modelData)
                                padding: Tk.Theme.space.sm; color: Tk.Theme.color.surface; borderColor: Tk.Theme.color.controlBorder
                                Accessible.role: Accessible.ListItem; Accessible.name: String(modelData.title || "") + ", " + statusId; Accessible.description: String(modelData.assignee || "") + " · " + String(modelData.dueDate || "")
                                Tk.Flex {
                                    direction: Tk.Flex.Column; gap: Tk.Theme.space.xs; width: parent.width
                                    Tk.Label { text: String(modelData.title || qsTr("Untitled task")); wrapMode: Text.Wrap; font.weight: Font.DemiBold }
                                    Tk.Label { text: [modelData.assignee, modelData.dueDate, modelData.priority].filter(Boolean).join(" · "); muted: true }
                                    Tk.Flex {
                                        direction: Tk.Flex.Row; gap: Tk.Theme.space.xs
                                        Tk.Button { text: qsTr("Edit"); tooltip: qsTr("Request edit for %1").arg(taskId); enabled: !control.offline; onClicked: control.edit(taskId) }
                                        Repeater {
                                            model: control.columns
                                            delegate: Tk.Button {
                                                required property var modelData
                                                visible: String(modelData.status || "") !== statusId
                                                text: String(modelData.label || modelData.status || "")
                                                small: true
                                                Accessible.name: qsTr("Move to %1").arg(String(modelData.label || modelData.status || ""))
                                                tooltip: Accessible.name; enabled: !control.offline
                                                onClicked: control.move(taskId, statusId, String(modelData.status || ""))
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
