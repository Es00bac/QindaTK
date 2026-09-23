// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "ComboPopupMotion"
    when: windowShown
    visible: true
    width: 220
    height: 140

    Component {
        id: comboComponent
        Tk.ComboBox {
            x: 8
            y: 8
            width: 180
            model: ["One", "Two", "Three", "Four", "Five", "Six"]
        }
    }
    Component {
        id: scrollingComponent
        Flickable {
            width: 220
            height: 140
            contentHeight: 300
            property alias combo: nestedCombo
            Tk.ComboBox {
                id: nestedCombo
                x: 8
                y: 100
                width: 180
                model: ["One", "Two", "Three", "Four", "Five", "Six"]
            }
        }
    }

    function test_open_popup_remeasures_after_control_moves() {
        const host = root.Window.window
        const oldWidth = host.width
        const oldHeight = host.height
        try {
            host.width = 220
            host.height = 140
            tryVerify(function() { return host.contentItem.height === 140 })
            const combo = createTemporaryObject(comboComponent, root)
            verify(combo !== null)
            combo.popup.open()
            tryVerify(function() { return combo.popup.visible })
            combo.y = 100
            tryVerify(function() {
                const top = combo.popup.contentItem.mapToItem(host.contentItem, 0, 0).y
                            - combo.popup.topPadding
                return top >= 0 && top + combo.popup.height <= combo.y
            })
            compare(combo.popup.contentItem.contentHeight > combo.popup.contentItem.height,
                    true)
            combo.popup.close()
        } finally {
            host.width = oldWidth
            host.height = oldHeight
        }
    }

    function test_open_popup_remeasures_when_ancestor_scrolls() {
        const host = root.Window.window
        const oldWidth = host.width
        const oldHeight = host.height
        try {
            host.width = 220
            host.height = 140
            tryVerify(function() { return host.contentItem.height === 140 })
            const flick = createTemporaryObject(scrollingComponent, root)
            verify(flick !== null)
            const combo = flick.combo
            combo.popup.open()
            tryVerify(function() { return combo.popup.visible })
            flick.contentY = 30
            tryVerify(function() {
                const top = combo.popup.contentItem.mapToItem(host.contentItem, 0, 0).y
                            - combo.popup.topPadding
                const comboY = combo.mapToItem(host.contentItem, 0, 0).y
                return top >= 0 && top + combo.popup.height <= comboY
            })
            combo.popup.close()
        } finally {
            host.width = oldWidth
            host.height = oldHeight
        }
    }

    function test_open_popup_remeasures_after_text_scale_change() {
        const host = root.Window.window
        const oldWidth = host.width
        const oldHeight = host.height
        const oldScale = Tk.Density.textScale
        try {
            host.width = 220
            host.height = 140
            Tk.Density.textScale = 1
            tryVerify(function() { return host.contentItem.height === 140 })
            const combo = createTemporaryObject(comboComponent, root, { y: 100 })
            verify(combo !== null)
            combo.popup.open()
            tryVerify(function() { return combo.popup.visible })
            Tk.Density.textScale = 3
            tryVerify(function() {
                const top = combo.popup.contentItem.mapToItem(host.contentItem, 0, 0).y
                            - combo.popup.topPadding
                return top >= 0 && top + combo.popup.height <= combo.y
                       && combo.popup.contentItem.contentHeight > combo.popup.contentItem.height
            })
            combo.popup.close()
        } finally {
            Tk.Density.textScale = oldScale
            host.width = oldWidth
            host.height = oldHeight
        }
    }
}
