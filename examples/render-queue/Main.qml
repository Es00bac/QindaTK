// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    id: root
    width: 1020
    height: 700
    color: Tk.Theme.color.canvas
    property string hostStatus: qsTr("Host fixture ready · no renderer attached")
    readonly property var snapshots: [
        { jobId: "render-queued-018", label: "Product launch · master", sourceSnapshot: "composition:launch-v4",
          sourceVersion: "rev-184", sourceRange: "00:00:00–00:00:24", outputFormat: "H.264 / MP4",
          outputPath: "/exports/launch-final.mp4", state: "queued", progress: 0, warning: "", error: "", outputManifest: null },
        { jobId: "render-running-017", label: "Product launch · review", sourceSnapshot: "composition:launch-v4",
          sourceVersion: "rev-183", sourceRange: "00:00:00–00:00:12", outputFormat: "ProRes / MOV",
          outputPath: "/exports/launch-review.mov", state: "running", progress: 62,
          warning: "GPU fallback: software renderer", error: "", outputManifest: null },
        { jobId: "render-failed-016", label: "Product launch · proxy", sourceSnapshot: "composition:launch-v3",
          sourceVersion: "rev-179", sourceRange: "00:00:08–00:00:16", outputFormat: "H.264 / MP4",
          outputPath: "/exports/launch-proxy.mp4", state: "failed", progress: 41,
          warning: "", error: "Encoder unavailable: hevc_nvenc", outputManifest: null },
        { jobId: "render-done-015", label: "Product launch · approved", sourceSnapshot: "composition:launch-v2",
          sourceVersion: "rev-172", sourceRange: "00:00:00–00:00:24", outputFormat: "H.264 / MP4",
          outputPath: "/exports/launch-approved.mp4", state: "succeeded", progress: 100,
          warning: "", error: "", outputManifest: { path: "/exports/launch-approved.manifest.json", validated: true, checksum: "sha256:fixture" } },
        { jobId: "render-cancelled-014", label: "Product launch · abandoned", sourceSnapshot: "composition:launch-v1",
          sourceVersion: "rev-160", sourceRange: "full range", outputFormat: "PNG sequence",
          outputPath: "/exports/launch-frames/", state: "cancelled", progress: 20,
          warning: "Partial files are not an output manifest.", error: "", outputManifest: null },
        { jobId: "render-stale-013", label: "Product launch · outdated", sourceSnapshot: "composition:launch-v0",
          sourceVersion: "rev-148", sourceRange: "full range", outputFormat: "H.264 / MP4",
          outputPath: "/exports/launch-old.mp4", state: "stale", progress: 0,
          warning: "Source changed after validation.", error: "", outputManifest: null }
    ]

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Label { text: qsTr("Render queue"); font.pixelSize: Tk.Theme.font.title; Accessible.role: Accessible.Heading }
        Tk.Label {
            text: qsTr("Each row is an immutable host snapshot. Requests below do not start rendering in this fixture.")
            color: Tk.Theme.color.textMuted
            wrapMode: Text.Wrap
        }
        Tk.RenderQueueView {
            id: queue
            objectName: "fixtureRenderQueue"
            Tk.Flex.grow: 1
            jobs: root.snapshots
            listHeight: 560
            onValidateRequested: function(jobId) { root.hostStatus = qsTr("Host validation requested · %1").arg(jobId) }
            onSubmitRequested: function(jobId) { root.hostStatus = qsTr("Host submit requested · %1").arg(jobId) }
            onCancelRequested: function(jobId) { root.hostStatus = qsTr("Host cancellation requested · %1").arg(jobId) }
            onRetryRequested: function(jobId) { root.hostStatus = qsTr("Host retry requested · %1").arg(jobId) }
            onOpenOutputRequested: function(jobId, manifest) {
                root.hostStatus = qsTr("Host open-output requested · %1 · %2").arg(jobId).arg(manifest.path)
            }
        }
        Tk.Label { objectName: "fixtureHostStatus"; text: root.hostStatus; color: Tk.Theme.color.accent }
    }
}
