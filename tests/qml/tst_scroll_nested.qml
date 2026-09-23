// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "ScrollNested"
    when: windowShown
    visible: true
    width: 320
    height: 240

    Component {
        id: nestedScene
        Item {
            width: 220
            height: 180
            Tk.Scroll {
                objectName: "outerScroll"
                width: 200
                height: 160
                Tk.Flex {
                    direction: Tk.Flex.Column
                    Item { implicitHeight: 100 }
                    Tk.Scroll {
                        objectName: "innerScroll"
                        implicitWidth: 180
                        implicitHeight: 80
                        Tk.Flex.shrink: 0
                        Item {
                            implicitWidth: 180; implicitHeight: 300
                            Tk.Button {
                                objectName: "focusTarget"
                                text: "Target"
                                y: 260; width: 80
                            }
                        }
                    }
                    Item { implicitHeight: 240 }
                }
            }
        }
    }
    Component {
        id: horizontalScene
        Tk.Scroll {
            width: 100
            height: 60
            overflowX: Tk.Scroll.Auto
            overflowY: Tk.Scroll.Hidden
            Item { implicitWidth: 300; implicitHeight: 40 }
        }
    }
    Component {
        id: canvasScene
        Item {
            width: 220
            height: 180
            Tk.Scroll {
                objectName: "canvasOuterScroll"
                width: 200
                height: 160
                Tk.Flex {
                    direction: Tk.Flex.Column
                    Item { implicitHeight: 100 }
                    Tk.Viewport {
                        objectName: "canvasViewport"
                        implicitWidth: 180
                        implicitHeight: 80
                        Tk.Flex.shrink: 0
                        Item { implicitWidth: 180; implicitHeight: 300 }
                    }
                    Item { implicitHeight: 240 }
                }
            }
        }
    }
    Component {
        id: gridScene
        Item {
            width: 220
            height: 180
            Tk.Scroll {
                objectName: "gridOuterScroll"
                width: 200
                height: 160
                Tk.Grid {
                    columns: "1fr"; rows: "100 80 240"
                    Item { Tk.Grid.row: 0; implicitHeight: 100 }
                    Tk.Scroll {
                        objectName: "gridInnerScroll"
                        Tk.Grid.row: 1
                        Item { implicitWidth: 180; implicitHeight: 300 }
                    }
                    Item { Tk.Grid.row: 2; implicitHeight: 240 }
                }
            }
        }
    }

    function test_wheelUsesInnerThenChainsAtEdge() {
        const scene = createTemporaryObject(nestedScene, root)
        const outer = findChild(scene, "outerScroll")
        const inner = findChild(scene, "innerScroll")
        verify(outer && inner)
        compare(outer.overflowX, Tk.Scroll.Hidden)
        tryVerify(() => inner.scrollableY && outer.scrollableY)
        compare(inner.height, 80)
        compare(outer.contentY, 0)
        compare(inner.contentY, 0)

        mouseWheel(inner, inner.width / 2, 15, 0, -120, Qt.NoButton)
        tryVerify(() => inner.contentY > 0)
        compare(outer.contentY, 0)

        inner.contentY = inner.contentHeight - inner.viewportHeight
        mouseWheel(inner, inner.width / 2, 15, 0, -120, Qt.NoButton)
        tryVerify(() => outer.contentY > 0)
        compare(inner.contentY, inner.contentHeight - inner.viewportHeight)
        compare(inner.flickable.boundsBehavior, Flickable.StopAtBounds)
    }

    function test_keyboardUsesFocusedViewportThenChainsAtEdge() {
        const scene = createTemporaryObject(nestedScene, root)
        const outer = findChild(scene, "outerScroll")
        const inner = findChild(scene, "innerScroll")
        verify(outer && inner)
        tryVerify(() => inner.scrollableY && outer.scrollableY)
        inner.accessibleName = "Inner inspector"
        compare(inner.Accessible.role, Accessible.Pane)
        compare(inner.Accessible.name, "Inner inspector")
        inner.forceActiveFocus()
        verify(inner.activeFocus)
        tryCompare(outer, "contentY", 20)
        const outerAfterFocus = outer.contentY

        keyClick(Qt.Key_Down)
        tryVerify(() => inner.contentY > 0)
        compare(outer.contentY, outerAfterFocus)

        inner.contentY = inner.contentHeight - inner.viewportHeight
        keyClick(Qt.Key_Down)
        tryVerify(() => outer.contentY > outerAfterFocus)
    }

    function test_focusRevealThroughNestedViewports() {
        const scene = createTemporaryObject(nestedScene, root)
        const outer = findChild(scene, "outerScroll")
        const inner = findChild(scene, "innerScroll")
        const target = findChild(scene, "focusTarget")
        verify(outer && inner && target)
        tryVerify(() => inner.scrollableY && outer.scrollableY)
        target.forceActiveFocus()
        verify(target.activeFocus)
        tryVerify(() => inner.contentY > 0 && outer.contentY > 0)
        const innerPoint = target.mapToItem(inner.contentItem, 0, 0)
        const outerPoint = target.mapToItem(outer.contentItem, 0, 0)
        verify(innerPoint.y >= inner.contentY)
        verify(innerPoint.y + target.height <= inner.contentY + inner.viewportHeight)
        verify(outerPoint.y >= outer.contentY)
        verify(outerPoint.y + target.height <= outer.contentY + outer.viewportHeight)
    }

    function test_keyboardPagingAndDisabledInnerHandoff() {
        const scene = createTemporaryObject(nestedScene, root)
        const outer = findChild(scene, "outerScroll")
        const inner = findChild(scene, "innerScroll")
        verify(outer && inner)
        tryVerify(() => inner.scrollableY && outer.scrollableY)
        inner.forceActiveFocus()
        tryCompare(outer, "contentY", 20)
        keyClick(Qt.Key_PageDown)
        verify(inner.contentY > Tk.Theme.size.row)
        keyClick(Qt.Key_End)
        compare(inner.contentY, inner.contentHeight - inner.viewportHeight)
        keyClick(Qt.Key_Home)
        compare(inner.contentY, 0)

        inner.focus = false
        outer.forceActiveFocus()
        inner.interactive = false
        keyClick(Qt.Key_Down)
        compare(inner.contentY, 0)
        verify(outer.contentY > 20)
    }

    function test_explicitHorizontalOverflowAndKeyboard() {
        const scroll = createTemporaryObject(horizontalScene, root)
        verify(scroll)
        tryVerify(() => scroll.scrollableX)
        compare(scroll.scrollableY, false)
        scroll.forceActiveFocus()
        keyClick(Qt.Key_Right)
        verify(scroll.contentX > 0)
        compare(scroll.contentY, 0)
    }

    function test_focused_viewport_releases_tab_stop_after_overflow_ends() {
        const scroll = createTemporaryObject(horizontalScene, root)
        verify(scroll)
        tryVerify(() => scroll.scrollableX)
        scroll.forceActiveFocus()
        verify(scroll.activeFocus)
        verify(scroll.activeFocusOnTab)
        scroll.width = 400
        tryVerify(() => !scroll.scrollableX)
        verify(scroll.activeFocus)
        verify(scroll.activeFocusOnTab)
        root.forceActiveFocus()
        tryVerify(() => !scroll.activeFocus)
        tryVerify(() => !scroll.activeFocusOnTab)
    }

    function test_canvasWheelChainsAtDocumentEdge() {
        const scene = createTemporaryObject(canvasScene, root)
        const outer = findChild(scene, "canvasOuterScroll")
        const canvas = findChild(scene, "canvasViewport")
        verify(outer && canvas)
        tryVerify(() => outer.scrollableY && canvas.scrollItem.scrollableY)
        mouseWheel(canvas, canvas.width / 2, 15, 0, -120, Qt.NoButton)
        tryVerify(() => canvas.scrollItem.contentY > 0)
        compare(outer.contentY, 0)

        canvas.scrollItem.contentY = canvas.scrollItem.contentHeight - canvas.scrollItem.viewportHeight
        mouseWheel(canvas, canvas.width / 2, 15, 0, -120, Qt.NoButton)
        tryVerify(() => outer.contentY > 0)
    }
    function test_canvasCtrlWheelKeepsZoomOwnership() {
        const scene = createTemporaryObject(canvasScene, root)
        const outer = findChild(scene, "canvasOuterScroll")
        const canvas = findChild(scene, "canvasViewport")
        verify(outer && canvas)
        tryVerify(() => outer.scrollableY && canvas.scrollItem.scrollableY)
        mouseWheel(canvas, canvas.width / 2, 15, 0, 120, Qt.NoButton, Qt.ControlModifier)
        tryVerify(() => canvas.zoom > 1)
        compare(outer.contentY, 0)
    }
    function test_gridCellScrollWheelHandoff() {
        const scene = createTemporaryObject(gridScene, root)
        const outer = findChild(scene, "gridOuterScroll")
        const inner = findChild(scene, "gridInnerScroll")
        verify(outer && inner)
        tryVerify(() => outer.scrollableY && inner.scrollableY)
        mouseWheel(inner, inner.width / 2, 15, 0, -120, Qt.NoButton)
        tryVerify(() => inner.contentY > 0)
        compare(outer.contentY, 0)
        inner.contentY = inner.contentHeight - inner.viewportHeight
        mouseWheel(inner, inner.width / 2, 15, 0, -120, Qt.NoButton)
        tryVerify(() => outer.contentY > 0)
    }
}
