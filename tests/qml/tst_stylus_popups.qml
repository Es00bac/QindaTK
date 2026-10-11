// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QtTest
import QindaTK as Tk
TestCase {
    id: root
    name: "StylusPopups"
    visible: true; when: windowShown
    width: 600; height: 450
    Component {
        id: fixtureComponent
        Item {
            id: fixture
            width: 520; height: 400
            property int strokes: 0
            property int clicks: 0
            property alias dialog: dialog
            property alias button: button
            property alias siblingButton: siblingButton
            Rectangle {
                anchors.fill: parent
                color: Tk.Theme.color.canvas
                Tk.StylusHandler { onBegan: fixture.strokes++ }
            }
            Tk.Button {
                id: siblingButton
                x: 20; y: 20
                text: "Overlay action"
                onClicked: fixture.clicks++
            }
            Tk.Dialog {
                id: dialog
                width: 300
                title: "Modal action"
                modal: true
                closePolicy: T.Popup.NoAutoClose
                Tk.Button {
                    id: button
                    text: "Restore"
                    onClicked: fixture.clicks++
                }
            }
        }
    }
    function test_overlay_button_does_not_start_ink() {
        const fixture = createTemporaryObject(fixtureComponent, root)
        verify(fixture)
        mouseClick(fixture, 400, 300)
        compare(fixture.strokes, 1)
        fixture.strokes = 0
        mouseClick(fixture.siblingButton)
        compare(fixture.clicks, 1)
        compare(fixture.strokes, 0)
    }
    function test_modal_popup_button_and_background_never_ink() {
        const fixture = createTemporaryObject(fixtureComponent, root)
        verify(fixture)
        fixture.dialog.open()
        tryCompare(fixture.dialog, "opened", true)
        mouseClick(fixture.button)
        compare(fixture.clicks, 1)
        compare(fixture.strokes, 0)
        mouseClick(fixture, 5, 380)
        compare(fixture.strokes, 0)
        fixture.dialog.close()
        tryCompare(fixture.dialog, "visible", false)
        mouseClick(fixture, 400, 300)
        compare(fixture.strokes, 1)
    }
}
