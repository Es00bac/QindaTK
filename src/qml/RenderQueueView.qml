// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Host-owned immutable render snapshots. Requests carry stable job identity;
// this view never changes job state or claims output exists before the host says so.
Tk.Box {
    id: control
    objectName: "renderQueueView"

    property var jobs: []
    property real listHeight: Tk.Theme.size.fieldWidth * 4

    signal validateRequested(string jobId)
    signal submitRequested(string jobId)
    signal cancelRequested(string jobId)
    signal retryRequested(string jobId)
    signal openOutputRequested(string jobId, var outputManifest)

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Render queue")

    function validateJob(jobId) { validateRequested(jobId) }
    function submitJob(jobId) { submitRequested(jobId) }
    function cancelJob(jobId) { cancelRequested(jobId) }
    function retryJob(jobId) { retryRequested(jobId) }
    function openOutput(jobId, manifest) { openOutputRequested(jobId, manifest) }

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        width: parent ? parent.width : Tk.Theme.size.fieldWidth

        Tk.Overline { title: qsTr("Render queue"); wide: true }

        ListView {
            id: queueList
            objectName: "renderQueueList"
            Accessible.role: Accessible.List
            Accessible.name: qsTr("Render jobs")
            model: control.jobs
            clip: true
            spacing: Tk.Theme.space.xs
            implicitHeight: Math.min(Math.max(contentHeight, Tk.Theme.size.control), control.listHeight)
            height: implicitHeight
            Tk.Flex.shrink: 0
            boundsBehavior: Flickable.StopAtBounds

            delegate: Tk.Box {
                id: jobRow
                required property var modelData
                readonly property var job: modelData
                readonly property string jobId: String(job.jobId || "")
                readonly property string state: String(job.state || "queued")
                objectName: "renderJob_" + jobId
                width: queueList.width
                padding: Tk.Theme.space.sm
                color: state === "failed" ? Tk.Theme.color.dangerSubtle : Tk.Theme.color.surface
                borderColor: state === "failed" ? Tk.Theme.color.danger : Tk.Theme.color.divider
                implicitHeight: rowBody.implicitHeight + Tk.Theme.space.sm * 2
                Accessible.role: Accessible.ListItem
                Accessible.name: qsTr("%1, %2, source version %3, output %4")
                    .arg(jobId).arg(state).arg(String(job.sourceVersion || qsTr("unknown")))
                    .arg(String(job.outputPath || qsTr("path not set")))

                Tk.Flex {
                    id: rowBody
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.xs
                    width: parent.width

                    Tk.Flex {
                        direction: Tk.Flex.Row
                        gap: Tk.Theme.space.sm
                        Tk.Label {
                            objectName: "renderJobTitle_" + jobRow.jobId
                            text: String(job.label || job.sourceSnapshot || jobRow.jobId)
                            elide: Text.ElideRight
                            Tk.Flex.grow: 1
                        }
                        Tk.Badge {
                            objectName: "renderJobState_" + jobRow.jobId
                            text: jobRow.state
                            variant: jobRow.state === "failed" ? "danger"
                                : jobRow.state === "succeeded" ? "success"
                                : jobRow.state === "stale" ? "warning" : "default"
                        }
                        Tk.Button {
                            objectName: "renderValidate_" + jobRow.jobId
                            text: qsTr("Validate")
                            tooltip: qsTr("Ask the host to validate this immutable source and output snapshot")
                            visible: ["draft", "invalid", "stale"].includes(jobRow.state)
                            onClicked: control.validateJob(jobRow.jobId)
                        }
                        Tk.Button {
                            objectName: "renderSubmit_" + jobRow.jobId
                            text: qsTr("Submit")
                            tooltip: qsTr("Ask the host to submit this validated render snapshot")
                            visible: jobRow.state === "validated"
                            onClicked: control.submitJob(jobRow.jobId)
                        }
                        Tk.Button {
                            objectName: "renderCancel_" + jobRow.jobId
                            text: qsTr("Cancel")
                            tooltip: qsTr("Request cancellation of this render job")
                            visible: ["queued", "running", "cancelling"].includes(jobRow.state)
                            enabled: jobRow.state !== "cancelling"
                            onClicked: control.cancelJob(jobRow.jobId)
                        }
                        Tk.Button {
                            objectName: "renderRetry_" + jobRow.jobId
                            text: qsTr("Retry")
                            tooltip: qsTr("Ask the host to revalidate and retry this render job")
                            visible: ["failed", "cancelled", "stale"].includes(jobRow.state)
                            onClicked: control.retryJob(jobRow.jobId)
                        }
                        Tk.Button {
                            objectName: "renderOpenOutput_" + jobRow.jobId
                            text: qsTr("Open output")
                            tooltip: qsTr("Open output recorded in the successful job manifest")
                            visible: jobRow.state === "succeeded" && !!jobRow.job.outputManifest
                                && jobRow.job.outputManifest.validated === true
                            onClicked: control.openOutput(jobRow.jobId, jobRow.job.outputManifest)
                        }
                    }

                    Tk.Label {
                        objectName: "renderJobSource_" + jobRow.jobId
                        text: qsTr("Source %1 · version %2 · %3")
                            .arg(String(jobRow.job.sourceSnapshot || qsTr("unknown")))
                            .arg(String(jobRow.job.sourceVersion || qsTr("unknown")))
                            .arg(String(jobRow.job.sourceRange || qsTr("full range")))
                        color: Tk.Theme.color.textMuted
                        elide: Text.ElideRight
                    }
                    Tk.Label {
                        objectName: "renderJobOutput_" + jobRow.jobId
                        text: qsTr("%1 · %2").arg(String(jobRow.job.outputFormat || qsTr("format unset")))
                            .arg(String(jobRow.job.outputPath || qsTr("output path unset")))
                        color: Tk.Theme.color.textMuted
                        elide: Text.ElideRight
                    }
                    Tk.ProgressBar {
                        objectName: "renderProgress_" + jobRow.jobId
                        visible: ["queued", "running", "cancelling"].includes(jobRow.state)
                        value: Math.max(0, Math.min(100, Number(jobRow.job.progress || 0)))
                        from: 0
                        to: 100
                        tooltip: qsTr("Render progress")
                    }
                    Tk.Label {
                        objectName: "renderJobDiagnostic_" + jobRow.jobId
                        visible: String(jobRow.job.error || jobRow.job.warning || "").length > 0
                        text: String(jobRow.job.error || jobRow.job.warning || "")
                        color: jobRow.job.error ? Tk.Theme.color.danger : Tk.Theme.color.warning
                        wrapMode: Text.Wrap
                    }
                    Tk.Label {
                        objectName: "renderJobManifest_" + jobRow.jobId
                        visible: jobRow.state === "succeeded" && !!jobRow.job.outputManifest
                        text: jobRow.job.outputManifest
                            ? qsTr("Manifest: %1").arg(String(jobRow.job.outputManifest.path || qsTr("recorded")))
                            : ""
                        color: Tk.Theme.color.textMuted
                        elide: Text.ElideRight
                    }
                }
            }
        }
        Tk.EmptyState {
            objectName: "renderQueueEmpty"
            visible: queueList.count === 0
            title: qsTr("No render jobs")
            text: qsTr("Validated submissions and their output status will appear here.")
        }
    }
}
