// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "PrintPreflight"
    when: windowShown
    visible: true
    width: 760
    height: 520

    Component {
        id: previewComponent
        Tk.PrintPreview {
            width: 680
            pageCount: 8
            pageRange: ({ startPage: 2, endPage: 5 })
            job: ({ id: "job-77", status: "pending", progress: 0.4, error: "", affectedStart: 2, affectedEnd: 5 })
            capabilities: ({ paperOptions: [{ id: "a4", label: "A4" }],
                            orientationOptions: [{ id: "portrait", label: "Portrait" }],
                            scaleOptions: [{ id: "fit", label: "Fit" }],
                            duplexOptions: [{ id: "simplex", label: "Simplex" }], maxCopies: 1 })
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    function test_cancel_requests_pending_job_and_waits_for_acknowledgement() {
        const preview = createTemporaryObject(previewComponent, this)
        verify(preview !== null)
        const requested = createTemporaryObject(spyComponent, this,
            { target: preview, signalName: "cancelPrintRequested" })
        const cancel = findChild(preview, "printSubmit")
        verify(cancel !== null && cancel.enabled)
        compare(cancel.text, "Cancel print")

        cancel.click()

        compare(requested.count, 1)
        compare(requested.signalArguments[0][0], "job-77")
        compare(preview.job.status, "pending")
    }
}
