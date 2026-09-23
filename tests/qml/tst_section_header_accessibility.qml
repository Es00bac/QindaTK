// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "SectionHeaderAccessibility"
    when: windowShown
    visible: true
    width: 400
    height: 240

    Component {
        id: headerComponent
        Tk.SectionHeader {
            title: "Audio"
            count: "3"
            collapsible: true
            width: 300
        }
    }
    Component {
        id: groupComponent
        Tk.PropertyGroup {
            title: "Document"
            width: 300
            Tk.PropertyRow { label: "Name"; Tk.Label { text: "Scene" } }
        }
    }
    Component {
        id: focusSceneComponent
        FocusScope {
            width: 300
            height: 72
            Tk.Button { objectName: "beforeHeader"; text: "Before"; width: 100; y: 0 }
            Tk.SectionHeader {
                objectName: "keyboardHeader"
                title: "Audio"
                collapsible: true
                width: 300
                y: Tk.Theme.size.control
            }
            Tk.Button { objectName: "afterHeader"; text: "After"; width: 100; y: Tk.Theme.size.control * 2 }
        }
    }

    function test_focusKeyboardAndAccessiblePressShareToggle() {
        const scene = createTemporaryObject(focusSceneComponent, root)
        const header = findChild(scene, "keyboardHeader")
        verify(header !== null)
        compare(header.implicitHeight, Tk.Theme.size.row)
        compare(header.Accessible.role, Accessible.Button)
        compare(header.Accessible.name, "Audio")
        verify(header.Accessible.focusable)
        verify(header.Accessible.description.indexOf("Expanded") >= 0)

        const before = findChild(scene, "beforeHeader")
        verify(before !== null)
        before.forceActiveFocus()
        keyClick(Qt.Key_Tab)
        verify(header.activeFocus)
        verify(header.Accessible.focused)

        keyClick(Qt.Key_Space)
        verify(header.collapsed)
        verify(header.Accessible.description.indexOf("Collapsed") >= 0)
        keyClick(Qt.Key_Return)
        verify(!header.collapsed)
        keyClick(Qt.Key_Enter)
        verify(header.collapsed)

        header.Accessible.pressAction()
        verify(!header.collapsed)
    }

    function test_pointerUsesSameToggleAndDisabledCannotToggle() {
        const header = createTemporaryObject(headerComponent, root, { y: 50 })
        verify(header !== null)
        mouseClick(header, header.width / 2, header.height / 2)
        verify(header.collapsed)

        header.enabled = false
        header.Accessible.pressAction()
        compare(header.collapsed, true)
        compare(header.Accessible.role, Accessible.Button)
        verify(header.Accessible.description.indexOf("Disabled") >= 0)
        mouseClick(header, header.width / 2, header.height / 2)
        compare(header.collapsed, true)
        verify(!header.Accessible.focusable)
    }

    function test_noncollapsibleIsStaticAndCannotToggle() {
        const header = createTemporaryObject(headerComponent, root, { y: 100, collapsible: false })
        verify(header !== null)
        compare(header.Accessible.role, Accessible.StaticText)
        verify(!header.Accessible.focusable)
        header.forceActiveFocus()
        keyClick(Qt.Key_Space)
        header.Accessible.pressAction()
        mouseClick(header, header.width / 2, header.height / 2)
        compare(header.collapsed, false)
    }

    function test_largeCountMovesBelowTitleInsideNarrowHeader() {
        const formerMode = Tk.Density.modeName
        const formerTextScale = Tk.Density.textScale
        try {
            Tk.Density.modeName = "touch"
            Tk.Density.textScale = 3
            const header = createTemporaryObject(headerComponent, root, {
                title: "INPUTS",
                count: "TextField · SearchField · TextArea · NumberField · Slider · ComboBox · ColorField",
                width: 220,
                y: 0
            })
            verify(header !== null)
            waitForRendering(header)

            const title = findChild(header, "sectionTitle")
            const count = findChild(header, "sectionCount_INPUTS")
            verify(title !== null && count !== null)
            verify(title.width > 0)
            verify(count.visible)
            compare(count.wrapMode, Text.WordWrap)
            verify(count.width <= header.width)
            verify(count.height > Tk.Theme.size.row)
            verify(header.implicitHeight >= count.y + count.height)
        } finally {
            Tk.Density.modeName = formerMode
            Tk.Density.textScale = formerTextScale
        }
    }

    function test_propertyGroupSyncsUserAndProgrammaticCollapse() {
        const group = createTemporaryObject(groupComponent, root, { y: 150 })
        verify(group !== null)
        const header = findChild(group, "groupHeader")
        const body = findChild(group, "groupBody")
        verify(header !== null && body !== null)

        header.forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(group.collapsed, true)
        compare(header.collapsed, true)
        verify(!body.visible)
        verify(group.Accessible.description.indexOf("Collapsed") >= 0)

        group.collapsed = false
        compare(header.collapsed, false)
        verify(body.visible)
        verify(group.Accessible.description.indexOf("Expanded") >= 0)

        header.Accessible.pressAction()
        compare(group.collapsed, true)
        compare(header.collapsed, true)
        verify(!body.visible)
    }
}
