// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "RulerMarkerAccessibility"
    when: windowShown
    visible: true
    width: 420
    height: 120

    Component { id: spyComponent; SignalSpy {} }
    Component {
        id: controlledRulerComponent
        Tk.Ruler {
            width: 320
            markers: [{ id: "left", position: 40, kind: "indent-left", draggable: true }]
            onMarkerMoved: function(markerId, position) {
                markers = [{ id: markerId, position: position,
                             kind: "indent-left", draggable: true }]
            }
            onTabRemoved: function(markerId) {
                markers = markers.filter(function(marker) { return marker.id !== markerId })
            }
        }
    }
    Component {
        id: readonlyRulerComponent
        Tk.Ruler {
            width: 320
            markers: [{ id: "fixed", position: 40, kind: "indent-right", draggable: false }]
        }
    }
    Component {
        id: tabsRulerComponent
        Tk.Ruler {
            width: 320
            markers: [{ id: "tabA", position: 80, kind: "tab", draggable: true }]
            onTabRemoved: markers = []
        }
    }

    function marker(ruler, markerId) {
        return findChild(ruler, "rulerMarker_" + markerId)
    }

    function spyOn(ruler, signalName) {
        const spy = createTemporaryObject(spyComponent, root,
            { target: ruler, signalName: signalName })
        verify(spy !== null)
        return spy
    }

    function make(component) {
        const ruler = createTemporaryObject(component, root)
        verify(ruler !== null)
        waitForRendering(ruler)
        return ruler
    }

    function test_focusKeyboardAccessibleActionsAndOwnerUpdates() {
        const ruler = make(controlledRulerComponent)
        const moved = spyOn(ruler, "markerMoved")
        let item = marker(ruler, "left")
        verify(item !== null)
        compare(item.objectName, "rulerMarker_left")
        compare(item.Accessible.role, Accessible.Slider)
        compare(item.Accessible.name, "Left indent")
        verify(item.Accessible.focusable)
        verify(item.Accessible.description.indexOf("Position 40 pixels") >= 0)

        item.forceActiveFocus()
        verify(item.activeFocus)
        verify(item.Accessible.focused)
        keyClick(Qt.Key_Right)
        compare(moved.count, 1)
        compare(moved.signalArguments[0][0], "left")
        compare(moved.signalArguments[0][1], 40 + Tk.Theme.space.unit)
        compare(ruler.markers[0].position, 40 + Tk.Theme.space.unit)
        item = marker(ruler, "left")
        tryVerify(function() { return item && item.activeFocus })
        verify(item.Accessible.description.indexOf("Position 44 pixels") >= 0)

        item.Accessible.increaseAction()
        compare(moved.count, 2)
        compare(ruler.markers[0].position, 40 + 2 * Tk.Theme.space.unit)
        item = marker(ruler, "left")
        tryVerify(function() { return item && item.activeFocus })
        verify(item.Accessible.description.indexOf("Position 48 pixels") >= 0)

        ruler.markers = [{ id: "left", position: 100, kind: "indent-left", draggable: true }]
        compare(moved.count, 2)
        verify(marker(ruler, "left").Accessible.description.indexOf("Position 100 pixels") >= 0)

        ruler.markers = [{ id: "left", position: 0, kind: "indent-left", draggable: true }]
        const atStart = marker(ruler, "left")
        atStart.forceActiveFocus()
        keyClick(Qt.Key_Left)
        compare(ruler.markers[0].position, 0)
        compare(moved.count, 2)
        ruler.markers = [{ id: "left", position: ruler.width, kind: "indent-left", draggable: true }]
        const atEnd = marker(ruler, "left")
        atEnd.Accessible.increaseAction()
        compare(ruler.markers[0].position, ruler.width)
        compare(moved.count, 2)
    }

    function test_readonlyMarkerAndDisabledRulerRejectEveryEditPath() {
        const ruler = make(readonlyRulerComponent)
        const moved = spyOn(ruler, "markerMoved")
        const item = marker(ruler, "fixed")
        verify(item !== null)
        compare(item.Accessible.role, Accessible.StaticText)
        compare(item.Accessible.name, "Right indent")
        verify(!item.Accessible.focusable)
        item.Accessible.increaseAction()
        item.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(ruler.markers[0].position, 40)
        compare(moved.count, 0)

        ruler.enabled = false
        ruler.markers = [{ id: "fixed", position: 40, kind: "indent-right", draggable: true }]
        const disabled = marker(ruler, "fixed")
        verify(disabled !== null)
        verify(!disabled.Accessible.focusable)
        disabled.Accessible.increaseAction()
        compare(ruler.markers[0].position, 40)
        compare(moved.count, 0)
        ruler.markers = [{ id: "fixed", position: 60, kind: "indent-right", draggable: true }]
        compare(ruler.markers[0].position, 60)
        compare(moved.count, 0)
    }

    function test_keyboardDeleteRemovesTabsOnly() {
        const ruler = make(tabsRulerComponent)
        const removed = spyOn(ruler, "tabRemoved")
        const tab = marker(ruler, "tabA")
        verify(tab !== null)
        tab.forceActiveFocus()
        keyClick(Qt.Key_Delete)
        compare(removed.count, 1)
        compare(removed.signalArguments[0][0], "tabA")
        compare(ruler.markers.length, 0)
    }
}
