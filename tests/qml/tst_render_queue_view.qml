// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "RenderQueueView"
    when: windowShown
    visible: true
    width: 900
    height: 700

    Component {
        id: queueFixture
        Item {
            width: 860
            height: 660
            Tk.RenderQueueView {
                id: queue
                objectName: "queue"
                anchors.fill: parent
                jobs: [
                    { jobId: "stable-queued", label: "Queued encode", sourceSnapshot: "scene-A", sourceVersion: "v7",
                      outputFormat: "MP4", outputPath: "/tmp/a.mp4", state: "queued", progress: 0,
                      warning: "", error: "", outputManifest: null },
                    { jobId: "stable-failed", label: "Failed encode", sourceSnapshot: "scene-B", sourceVersion: "v2",
                      outputFormat: "MOV", outputPath: "/tmp/b.mov", state: "failed", progress: 33,
                      warning: "", error: "Encoder missing", outputManifest: null },
                    { jobId: "stable-done", label: "Completed encode", sourceSnapshot: "scene-C", sourceVersion: "v4",
                      outputFormat: "PNG", outputPath: "/tmp/c.png", state: "succeeded", progress: 100,
                      warning: "", error: "", outputManifest: {path: "/tmp/c.json", validated: true} }
                ]
            }
        }
    }

    Component { id: spyComponent; SignalSpy { } }

    function test_snapshots_show_states_and_route_stable_requests() {
        const fixture = createTemporaryObject(queueFixture, this)
        verify(fixture)
        const queue = findChild(fixture, "queue")
        const list = findChild(queue, "renderQueueList")
        compare(list.count, 3)
        tryVerify(function() { return list.itemAtIndex(0) && list.itemAtIndex(1) && list.itemAtIndex(2) })

        const queued = list.itemAtIndex(0)
        const failed = list.itemAtIndex(1)
        const done = list.itemAtIndex(2)
        verify(findChild(queued, "renderJobState_stable-queued"))
        compare(findChild(queued, "renderJobState_stable-queued").text, "queued")
        compare(findChild(failed, "renderJobDiagnostic_stable-failed").text, "Encoder missing")
        verify(!findChild(queued, "renderOpenOutput_stable-queued").visible)
        verify(findChild(done, "renderOpenOutput_stable-done").visible)

        const cancelSpy = createTemporaryObject(spyComponent, this, {target: queue, signalName: "cancelRequested"})
        const retrySpy = createTemporaryObject(spyComponent, this, {target: queue, signalName: "retryRequested"})
        const outputSpy = createTemporaryObject(spyComponent, this, {target: queue, signalName: "openOutputRequested"})
        findChild(queued, "renderCancel_stable-queued").click()
        findChild(failed, "renderRetry_stable-failed").click()
        findChild(done, "renderOpenOutput_stable-done").click()
        compare(cancelSpy.signalArguments[0][0], "stable-queued")
        compare(retrySpy.signalArguments[0][0], "stable-failed")
        compare(outputSpy.signalArguments[0][0], "stable-done")
        compare(outputSpy.signalArguments[0][1].path, "/tmp/c.json")
    }
}
