// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// Every base type instantiates and reports a sane implicit size.
TestCase {
    id: root
    name: "Smoke"
    when: windowShown
    width: 400
    height: 300

    Component { id: labelComponent; Tk.Label { text: "Hello" } }
    Component { id: overlineComponent; Tk.Overline { title: "Section" } }
    Component { id: boxComponent; Tk.Box { padding: 4; borderWidth: 1; Tk.Label { text: "Boxed" } } }
    Component { id: panelComponent; Tk.Panel { title: "Layers"; width: 200; Tk.Label { text: "body" } } }
    Component { id: iconComponent; Tk.Icon { name: "layers"; size: 16 } }

    function test_label() {
        const label = createTemporaryObject(labelComponent, root)
        verify(label.implicitWidth > 10)
        compare(label.font.pixelSize, Tk.Theme.font.body)
    }

    function test_overline_uppercases() {
        const item = createTemporaryObject(overlineComponent, root)
        compare(item.text, "SECTION")
    }

    function test_box_adds_padding_and_border() {
        const box = createTemporaryObject(boxComponent, root)
        const inner = box.contentItem.children[0]
        compare(box.implicitWidth, inner.implicitWidth + 10)
        compare(box.implicitHeight, inner.implicitHeight + 10)
    }

    function test_panel_header_height() {
        const panel = createTemporaryObject(panelComponent, root)
        compare(panel.headerItem.implicitHeight, Tk.Theme.size.header)
        verify(panel.implicitHeight > Tk.Theme.size.header)
    }

    function test_icon_resolves_alias() {
        const icon = createTemporaryObject(iconComponent, root)
        verify(icon.available)
        verify(Tk.Icons.has("alert-triangle"))
        compare(Tk.Icons.resolve("alert-triangle"), "triangle-alert")
        verify(!Tk.Icons.has("no-such-icon"))
    }

    function test_theme_presets() {
        verify(Tk.Theme.presets().indexOf("sloom-dark") >= 0)
        verify(Tk.Theme.dark)
        compare(Tk.Theme.space.md, 8)
        compare(Tk.Theme.size.control, 24)
    }
}
