// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QindaTK as Tk

// Read-only presentation of JobModel. Workers own execution; retry is a host
// request so the application can revalidate inputs before creating a new job.
Tk.Box {
    id: control
    objectName: "taskCenter"
    property var jobModel: null
    property bool showFinished: true
    property string expandedJobId: ""
    property real listHeight: Tk.Theme.size.fieldWidth * 3
    signal cancelRequested(string jobId)
    signal retryRequested(string jobId)
    signal resultRequested(string jobId, var result)

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Background tasks")

    function cancelTask(jobId) {
        cancelRequested(jobId)
        return jobModel && jobModel.cancelJob ? jobModel.cancelJob(jobId) : false
    }
    function requestRetry(jobId) {
        retryRequested(jobId)
    }
    function openResult(jobId, result) {
        resultRequested(jobId, result)
    }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        width: parent ? parent.width : Tk.Theme.size.fieldWidth
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Overline { title: qsTr("Tasks"); wide: true; Tk.Flex.grow: 1 }
            Tk.Label {
                objectName: "taskCenterCounts"
                text: control.jobModel
                    ? qsTr("%1 running · %2 queued").arg(control.jobModel.runningCount).arg(control.jobModel.queuedCount)
                    : qsTr("No job service")
            }
            Tk.Button {
                objectName: "taskCenterClearFinished"
                text: qsTr("Clear finished")
                tooltip: qsTr("Remove completed task records from the model")
                enabled: !!control.jobModel
                onClicked: if (control.jobModel) control.jobModel.clearFinished()
            }
        }
        ListView {
            id: taskList
            objectName: "taskCenterList"
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Background tasks")
            model: control.jobModel
            clip: true
            spacing: Tk.Theme.space.xs
            implicitHeight: Math.min(Math.max(contentHeight, Tk.Theme.size.control), control.listHeight)
            height: implicitHeight
            Tk.Flex.shrink: 0
            boundsBehavior: Flickable.StopAtBounds
            delegate: Tk.Box {
                id: taskRow
                required property string jobId
                required property string label
                required property string jobState
                required property int progress
                required property string documentId
                required property string viewId
                required property string error
                required property var result
                objectName: "taskRow_" + jobId
                width: taskList.width
                visible: control.showFinished || ["queued", "running", "cancelling"].includes(jobState)
                implicitHeight: visible ? taskBody.implicitHeight + Tk.Theme.space.sm * 2 : 0
                padding: Tk.Theme.space.sm
                color: jobState === "failed" ? Tk.Theme.color.dangerSubtle : Tk.Theme.color.surface
                borderColor: jobState === "failed" ? Tk.Theme.color.danger : Tk.Theme.color.divider
                Accessible.role: Accessible.ListItem
                Accessible.name: label + ", " + jobState

                Tk.Flex {
                    id: taskBody
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    width: parent.width
                    Tk.Flex {
                        direction: Tk.Flex.Row
                        gap: Tk.Theme.space.sm
                        Tk.Label { objectName: "taskLabel"; text: taskRow.label; elide: Text.ElideRight; Tk.Flex.grow: 1 }
                        Tk.Badge { text: taskRow.jobState; variant: taskRow.jobState === "failed" ? "danger" : "default" }
                        Tk.Button {
                            objectName: "taskDetails_" + taskRow.jobId
                            text: control.expandedJobId === taskRow.jobId ? qsTr("Hide details") : qsTr("Details")
                            tooltip: qsTr("Show task details")
                            onClicked: control.expandedJobId = control.expandedJobId === taskRow.jobId ? "" : taskRow.jobId
                        }
                        Tk.Button {
                            objectName: "taskCancel_" + taskRow.jobId
                            visible: ["queued", "running", "cancelling"].includes(taskRow.jobState)
                            text: taskRow.jobState === "cancelling" ? qsTr("Canceling…") : qsTr("Cancel")
                            tooltip: qsTr("Cancel this background task")
                            enabled: taskRow.jobState !== "cancelling"
                            onClicked: control.cancelTask(taskRow.jobId)
                        }
                        Tk.Button {
                            objectName: "taskRetry_" + taskRow.jobId
                            visible: ["failed", "cancelled", "stale"].includes(taskRow.jobState)
                                && !(taskRow.result && taskRow.result.committed)
                            text: qsTr("Retry…")
                            tooltip: qsTr("Ask the application to revalidate and retry this task")
                            onClicked: control.requestRetry(taskRow.jobId)
                        }
                        Tk.Button {
                            objectName: "taskOpenResult_" + taskRow.jobId
                            visible: taskRow.jobState === "succeeded" && !!(taskRow.result && taskRow.result.committed)
                            text: qsTr("Open result")
                            tooltip: qsTr("Open the committed task result")
                            onClicked: control.openResult(taskRow.jobId, taskRow.result)
                        }
                    }
                    Tk.ProgressBar {
                        objectName: "taskProgress_" + taskRow.jobId
                        visible: ["queued", "running", "cancelling"].includes(taskRow.jobState)
                        from: 0
                        to: 100
                        value: taskRow.progress
                        tooltip: qsTr("Task progress")
                    }
                    Tk.Label {
                        objectName: "taskDetail_" + taskRow.jobId
                        visible: control.expandedJobId === taskRow.jobId
                        wrapMode: Text.Wrap
                        text: taskRow.error.length > 0 ? taskRow.error
                            : qsTr("Document %1 · View %2").arg(taskRow.documentId || qsTr("none"))
                                .arg(taskRow.viewId || qsTr("none"))
                    }
                }
            }
        }
        Tk.EmptyState {
            objectName: "taskCenterEmpty"
            visible: !control.jobModel || taskList.count === 0
            title: qsTr("No background tasks")
            text: qsTr("Long-running work will appear here.")
        }
    }
}
