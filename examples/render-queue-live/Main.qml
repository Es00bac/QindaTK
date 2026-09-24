// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    id: root
    objectName: "renderQueueLiveRoot"
    width: 1080
    height: 680
    color: Tk.Theme.color.bg
    Accessible.role: Accessible.Window
    Accessible.name: qsTr("Live render queue")

    function runDemo() {
        queueView.validateJob("demo-caption")
        queueView.submitJob("demo-caption")
    }

    Tk.Flex {
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md

        Tk.Heading { text: qsTr("Live render queue") }
        Tk.Label {
            text: qsTr("Validate and submit a fixed caption sequence. The output is written and verified by RenderQueueModel.")
            color: Tk.Theme.color.textMuted
        }
        Tk.RenderQueueView {
            id: queueView
            objectName: "liveRenderQueue"
            width: parent.width
            listHeight: Tk.Theme.size.fieldWidth * 3
            jobs: demoHost.jobs
            onValidateRequested: (jobId) => demoHost.validate(jobId)
            onSubmitRequested: (jobId) => demoHost.submit(jobId)
            onCancelRequested: (jobId) => demoHost.cancel(jobId)
            onRetryRequested: (jobId) => demoHost.retry(jobId)
            onOpenOutputRequested: (jobId, manifest) => demoHost.openOutput(jobId, manifest)
        }
        Tk.Label {
            text: qsTr("Verified first frame")
            visible: preview.visible
            color: Tk.Theme.color.textMuted
        }
        Image {
            id: preview
            objectName: "liveRenderedFrame"
            source: demoHost.previewUrl
            visible: source.toString().length > 0
            width: Math.min(parent.width, 640)
            height: width * 9 / 16
            Tk.Flex.alignSelf: Tk.Flex.Center
            fillMode: Image.PreserveAspectFit
            cache: false
            Accessible.role: Accessible.Graphic
            Accessible.name: qsTr("First rendered caption frame")
        }
        Tk.Label {
            objectName: "liveStateTrail"
            text: qsTr("State path: %1").arg(demoHost.statusTrail || qsTr("draft"))
            color: Tk.Theme.color.textMuted
        }
        Tk.Label {
            objectName: "liveOutputPath"
            text: qsTr("Output: %1").arg(demoHost.outputPath)
            color: Tk.Theme.color.textMuted
            elide: Text.ElideMiddle
        }
        Tk.Label {
            objectName: "liveError"
            visible: demoHost.lastError.length > 0
            text: demoHost.lastError
            color: Tk.Theme.color.danger
        }
    }
}
