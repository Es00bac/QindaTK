// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// D-327: Gallery (host-rendered previews, row scrolling, "More", highlight
// and activation) and CharacterMap (blocks, keyboard, recent, choice).
TestCase {
    id: root
    name: "OfficeGalleries"
    when: windowShown
    visible: true
    width: 640
    height: 480

    Component { id: spyComponent; SignalSpy {} }
    Component {
        id: stylePreview
        Rectangle {
            property var entry: ({})
            property bool selected: false
            objectName: "stylePreview"
            color: "white"
            Text { anchors.centerIn: parent; text: "AaBb"; font.bold: !!parent.entry.bold; color: parent.entry.color || "black" }
        }
    }
    Component {
        id: galleryComponent
        Tk.Gallery {
            width: 420
            preview: stylePreview
            currentId: "heading1"
            tooltip: "Styles"
            model: [{ id: "normal", label: "Normal" }, { id: "heading1", label: "Heading 1", bold: true },
                    { id: "heading2", label: "Heading 2", bold: true, color: "#1c62d4" }, { id: "title", label: "Title" },
                    { id: "quote", label: "Quote" }, { id: "emphasis", label: "Emphasis" }, { id: "strong", label: "Strong", bold: true }]
        }
    }
    Component { id: mapComponent; Tk.CharacterMap { width: 520; height: 360; recent: ["€", "→"] } }

    function test_gallery_renders_previews_and_reports_choices() {
        const gallery = createTemporaryObject(galleryComponent, root)
        verify(gallery)
        waitForRendering(gallery)
        compare(gallery.count, 7)
        compare(gallery.implicitHeight, gallery.tileHeight + Tk.Theme.space.xs * 2)
        const heading = findChild(gallery, "galleryTile_heading1")
        verify(heading !== null)
        verify(heading.selected)
        compare(String(heading.border.color), String(Tk.Theme.color.accent))
        const preview = findChild(heading, "stylePreview")
        verify(preview !== null)
        compare(preview.entry.label, "Heading 1")
        verify(preview.selected)
        compare(findChild(heading, "galleryCaption").text, "Heading 1")
        const activated = createTemporaryObject(spyComponent, root, { target: gallery, signalName: "activated" })
        const highlighted = createTemporaryObject(spyComponent, root, { target: gallery, signalName: "highlighted" })
        mouseMove(findChild(gallery, "galleryTile_normal"), 10, 10)
        tryVerify(function() { return highlighted.count > 0 })
        compare(highlighted.signalArguments[highlighted.count - 1][0], "normal")
        mouseClick(findChild(gallery, "galleryTile_title"))
        compare(activated.count, 1)
        compare(activated.signalArguments[0][0], "title")
        compare(gallery.currentId, "heading1")
        verify(gallery.activeFocus)
        keyClick(Qt.Key_Right)
        compare(gallery.focusIndex, 4)
        keyClick(Qt.Key_Return)
        compare(activated.signalArguments[1][0], "quote")
        // Rows scroll; "More" shows every tile.
        const strip = findChild(gallery, "galleryStrip")
        verify(strip.contentHeight > strip.height)
        mouseClick(findChild(gallery, "galleryDown"))
        compare(strip.contentY, gallery.tileHeight)
        mouseClick(findChild(gallery, "galleryMore"))
        const popover = findChild(gallery, "galleryPopover")
        tryCompare(popover, "opened", true)
        const all = findChild(popover.contentItem, "galleryAll")
        verify(all !== null)
        compare(all.count, 7)
        tryVerify(function() { return findChild(all, "galleryTile_strong") !== null })
        mouseClick(findChild(all, "galleryTile_strong"))
        compare(activated.signalArguments[2][0], "strong")
        tryCompare(popover, "visible", false)
    }

    function test_character_map_blocks_keyboard_and_choice() {
        const map = createTemporaryObject(mapComponent, root)
        verify(map)
        waitForRendering(map)
        compare(map.characters[0], "¡")
        compare(map.currentCharacter, "¡")
        compare(findChild(map, "characterMapCode").text, "U+00A1")
        const chosen = createTemporaryObject(spyComponent, root, { target: map, signalName: "characterChosen" })
        const grid = findChild(map, "characterMapGrid")
        grid.forceActiveFocus()
        keyClick(Qt.Key_Right)
        compare(map.currentCharacter, "¢")
        keyClick(Qt.Key_Return)
        compare(chosen.count, 1)
        compare(chosen.signalArguments[0][0], "¢")
        // Another block: arrows.
        const block = findChild(map, "characterMapBlock")
        block.currentIndex = 8
        block.activated(8)
        compare(map.currentCharacter, "←")
        const cell = findChild(grid, "characterCell_2192")
        verify(cell !== null)
        mouseClick(cell)
        compare(map.currentCharacter, "\u2192")
        mouseDoubleClickSequence(cell)
        compare(chosen.count, 2)
        compare(chosen.signalArguments[1][0], "→")
        compare(findChild(map, "characterMapCode").text, "U+2192")
        mouseClick(findChild(map, "characterRecent_0"))
        compare(chosen.signalArguments[2][0], "€")
        mouseClick(findChild(map, "characterMapInsert"))
        compare(chosen.count, 4)
        // Controls and surrogates are never listed; astral blocks work.
        map.blocks = [{ name: "C1", first: 0x80, last: 0xA0 }, { name: "Math letters", first: 0x1D400, last: 0x1D405 }]
        map.currentBlock = 0
        compare(map.characters.length, 1)
        map.currentBlock = 1
        compare(map.characters.length, 6)
        compare(map.codePointLabel(map.characters[0]), "U+1D400")
    }
}
