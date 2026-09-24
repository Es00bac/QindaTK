// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "SlideAuthoring"
    when: windowShown
    visible: true
    width: 760
    height: 520

    Component {
        id: canvasComponent
        Tk.SlideCanvas {
            width: 700
            height: 460
            documentId: "deck-1"
            slideId: "slide-9"
            slideSize: ({ width: 960, height: 540 })
            objects: [{ id: "title-1", kind: "text", text: "A title", bounds: ({ x: 20, y: 30, width: 200, height: 60 }) }]
            selectedObjectIds: ["title-1"]
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    function test_insert_text_is_host_request_without_local_mutation() {
        const canvas = createTemporaryObject(canvasComponent, this)
        verify(canvas !== null)
        const requested = createTemporaryObject(spyComponent, this,
            { target: canvas, signalName: "objectInsertRequested" })
        const originalObjects = canvas.objects.length
        const insert = findChild(canvas, "slideInsertText")
        verify(insert !== null && insert.enabled)

        insert.click()

        compare(requested.count, 1)
        compare(requested.signalArguments[0][0], "text")
        compare(requested.signalArguments[0][1].width, 288)
        verify(String(requested.signalArguments[0][2]).indexOf("slide-9:insert:") === 0)
        compare(canvas.objects.length, originalObjects)
    }
}
