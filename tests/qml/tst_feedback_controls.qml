// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "FeedbackControls"
    when: windowShown
    visible: true
    width: 760
    height: 600

    Component { id: toastComponent; Tk.Toast { message: "A background task finished."; actionText: "Open"; actionId: "open.result" } }
    Component { id: historyComponent; Tk.HistoryView { undoService: historyStub; width: 500 } }
    Component {
        id: notificationsComponent
        Item {
            width: 500
            height: 500
            property alias entries: entries
            ListModel { id: entries }
            Tk.NotificationCenter { id: center; objectName: "notificationCenter"; notificationModel: entries; anchors.fill: parent }
            Component.onCompleted: entries.append({ notificationId: "notify-1", severity: "warning", title: "Missing media", message: "Relink the source file.", source: "Import", timestamp: "Now", read: false, repeatCount: 1, actions: [{ id: "media.relink", label: "Relink" }] })
        }
    }
    Component {
        id: queuedToastComponent
        Item {
            width: 500
            height: 300
            property alias notices: notices
            property alias toast: toast
            property alias firstCenter: firstCenter
            property alias secondCenter: secondCenter
            Tk.NotificationModel { id: notices }
            Tk.Toast { id: toast; notificationModel: notices; duration: Tk.Theme.motion.base * 2 }
            Tk.NotificationCenter { id: firstCenter; notificationModel: notices; width: 240; height: 200 }
            Tk.NotificationCenter { id: secondCenter; notificationModel: notices; x: 250; width: 240; height: 200 }
        }
    }
    Component {
        id: tasksComponent
        Item {
            width: 680
            height: 500
            property alias entries: entries
            ListModel { id: entries }
            Tk.TaskCenter { id: center; objectName: "taskCenter"; jobModel: entries; anchors.fill: parent }
            Component.onCompleted: {
                entries.append({ jobId: "job.running", label: "Waveform peaks", jobState: "running", progress: 42, documentId: "doc-A", viewId: "view-A", error: "", result: ({}) })
                entries.append({ jobId: "job.failed", label: "Render", jobState: "failed", progress: 30, documentId: "doc-A", viewId: "view-B", error: "Codec unavailable", result: ({}) })
                entries.append({ jobId: "job.done", label: "Export", jobState: "succeeded", progress: 100, documentId: "doc-A", viewId: "view-C", error: "", result: ({committed: true, destination: "file:///tmp/out.qtk"}) })
            }
        }
    }
    QtObject {
        id: historyStub
        property var historyEntries: [
            { entryId: "step-1", label: "Create item", index: 1, applied: true, clean: true },
            { entryId: "step-2", label: "Rename item", index: 2, applied: true, clean: false },
            { entryId: "step-3", label: "Move item", index: 3, applied: false, clean: false }
        ]
        property int historyIndex: 2
        property int cleanIndex: 1
        property bool clean: false
        property bool canUndo: true
        property bool canRedo: false
        property string undoText: "Undo Move"
        property string redoText: ""
        property string lastError: ""
        signal navigated(string entryId)
        signal navigatedToIndex(int index)
        function navigateToHistoryEntry(entryId) { navigated(entryId); return true }
        function navigateToHistoryIndex(index) { navigatedToIndex(index); return true }
        function undo() { return true }
        function redo() { return false }
    }

    function test_toast_action_emits_stable_action_id_and_dismisses() {
        const toast = createTemporaryObject(toastComponent, root, { visible: true, duration: Tk.Theme.motion.tooltipTimeout * 2 })
        verify(toast)
        const spy = createTemporaryObject(signalSpyComponent, root, { target: toast, signalName: "actionRequested" })
        const action = findChild(toast, "toastAction")
        verify(action && action.visible)
        action.click()
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], "open.result")
        compare(toast.visible, false)
    }

    function test_toast_uses_theme_timeout_role() {
        const toast = createTemporaryObject(toastComponent, root)
        verify(toast)
        compare(toast.duration, Tk.Theme.motion.toastTimeout)
    }

    function test_toast_timer_pauses_with_focus_then_expires() {
        const toast = createTemporaryObject(toastComponent, root, { visible: true, duration: Tk.Theme.motion.base * 2 })
        verify(toast)
        toast.forceActiveFocus()
        verify(toast.paused)
        const remaining = toast.remaining
        wait(Tk.Theme.motion.base * 3)
        compare(toast.remaining, remaining)
        root.forceActiveFocus()
        tryVerify(function() { return !toast.visible }, Tk.Theme.motion.base * 4)
    }

    function test_queued_failure_toast_expires_but_stays_in_both_centers() {
        const fixture = createTemporaryObject(queuedToastComponent, root)
        verify(fixture)
        const id = fixture.toast.announce("Render failed", "Codec unavailable", "error", "render")
        verify(id.length > 0)
        tryVerify(function() { return fixture.toast.visible })
        compare(fixture.toast.notificationId, id)
        compare(fixture.firstCenter.count(), 1)
        compare(fixture.secondCenter.count(), 1)
        tryVerify(function() { return !fixture.toast.visible }, Tk.Theme.motion.base * 5)
        compare(fixture.notices.count, 1)
        compare(fixture.notices.get(0).notificationId, id)
        compare(fixture.notices.post("Render failed", "Codec unavailable", "error", "render"), id)
        compare(fixture.notices.get(0).repeatCount, 2)
        compare(fixture.firstCenter.count(), 1)
        compare(fixture.secondCenter.count(), 1)
    }

    function test_history_navigates_stable_entries_and_base_state() {
        const panel = createTemporaryObject(historyComponent, root)
        verify(panel)
        const spy = createTemporaryObject(signalSpyComponent, root, { target: historyStub, signalName: "navigated" })
        const move = findChild(panel, "historyEntry_step-3")
        verify(move)
        move.click()
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], "step-3")
        compare(findChild(panel, "historyCleanState").text, "Modified")
        const baseSpy = createTemporaryObject(signalSpyComponent, root, { target: historyStub, signalName: "navigatedToIndex" })
        findChild(panel, "historyEntry_history.initial").click()
        compare(baseSpy.signalArguments[0][0], 0)
    }

    function test_notification_action_routes_stable_ids() {
        const fixture = createTemporaryObject(notificationsComponent, root)
        verify(fixture)
        const center = findChild(fixture, "notificationCenter")
        const spy = createTemporaryObject(signalSpyComponent, root, { target: center, signalName: "actionRequested" })
        const list = findChild(center, "notificationList")
        compare(list.count, 1)
        tryVerify(function() { return list.itemAtIndex(0) !== null }, 1000)
        const row = list.itemAtIndex(0)
        verify(row)
        const action = findChild(row, "notificationAction_notify-1_media.relink")
        verify(action && action.visible)
        action.click()
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], "notify-1")
        compare(spy.signalArguments[0][1], "media.relink")
    }

    function test_task_center_routes_cancel_and_retry_without_replay() {
        const fixture = createTemporaryObject(tasksComponent, root)
        verify(fixture)
        const center = findChild(fixture, "taskCenter")
        const cancelSpy = createTemporaryObject(signalSpyComponent, root, { target: center, signalName: "cancelRequested" })
        const retrySpy = createTemporaryObject(signalSpyComponent, root, { target: center, signalName: "retryRequested" })
        const list = findChild(center, "taskCenterList")
        compare(list.count, 3)
        tryVerify(function() { return list.itemAtIndex(0) && list.itemAtIndex(1) && list.itemAtIndex(2) }, 1000)
        const runningRow = list.itemAtIndex(0)
        const failedRow = list.itemAtIndex(1)
        const completedRow = list.itemAtIndex(2)
        verify(runningRow && failedRow && completedRow)
        const cancel = findChild(runningRow, "taskCancel_job.running")
        const retry = findChild(failedRow, "taskRetry_job.failed")
        const committedRetry = findChild(completedRow, "taskRetry_job.done")
        const openResult = findChild(completedRow, "taskOpenResult_job.done")
        const resultSpy = createTemporaryObject(signalSpyComponent, root, { target: center, signalName: "resultRequested" })
        verify(cancel && cancel.visible)
        verify(retry && retry.visible)
        verify(committedRetry && !committedRetry.visible)
        verify(openResult && openResult.visible)
        cancel.click()
        retry.click()
        openResult.click()
        compare(cancelSpy.count, 1)
        compare(cancelSpy.signalArguments[0][0], "job.running")
        compare(retrySpy.count, 1)
        compare(retrySpy.signalArguments[0][0], "job.failed")
        compare(resultSpy.count, 1)
        compare(resultSpy.signalArguments[0][0], "job.done")
        compare(resultSpy.signalArguments[0][1].destination, "file:///tmp/out.qtk")
    }

    Component { id: signalSpyComponent; SignalSpy { } }
}
