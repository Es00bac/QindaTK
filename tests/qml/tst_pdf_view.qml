// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "PdfAnnotationRequests"
    when: windowShown
    visible: true
    width: 760
    height: 520

    Component {
        id: toolsComponent
        Tk.PdfAnnotationTools {
            width: 520
            documentId: "pdf-contract"
            pageIndex: 1
            selectedText: "review this passage"
            selectedBounds: ({ x: 40, y: 80, width: 180, height: 16 })
            annotations: [{ id: "ann-42", pageIndex: 1, kind: "highlight", status: "pending",
                           text: "review this passage", canAccept: true, canReject: true }]
            selectedAnnotationId: "ann-42"
            capabilities: ({ annotationKinds: ["highlight"], canAccept: true, canReject: true,
                             canEditForms: false, canApplyRedaction: false })
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    function test_accept_emits_stable_id_without_premature_status_change() {
        const tools = createTemporaryObject(toolsComponent, this)
        verify(tools !== null)
        const requested = createTemporaryObject(spyComponent, this,
            { target: tools, signalName: "annotationAcceptRequested" })
        const accept = findChild(tools, "pdfAnnotationAccept")
        verify(accept !== null && accept.enabled)

        accept.click()

        compare(requested.count, 1)
        compare(requested.signalArguments[0][0], "ann-42")
        compare(tools.selectedAnnotation.status, "pending")
    }
}
