// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (D-327, docs/controls.md): the character map behind an
// office suite's Insert ▸ Symbol — a block picker, the host's recent
// characters, a grid of the block's characters in `fontFamily`, and the
// selected character large with its code point. Selection is view state;
// characterChosen(text) (double-click, Return, the Insert button or a recent
// chip) is the only output: the host inserts the text and keeps `recent`.
// Characters are code points, so astral ones (emoji, math alphanumerics)
// work; C0/C1 controls and lone surrogates are never listed.
T.Control {
    id: map

    // [{name, first, last}] inclusive code point ranges.
    property var blocks: defaultBlocks
    property int currentBlock: 0
    property string fontFamily: Tk.Theme.font.family
    property var recent: []
    property int columns: 16
    property string currentCharacter: characters.length ? characters[0] : ""
    property string tooltip: qsTr("Character map")
    readonly property var characters: charactersOf(blocks[currentBlock])
    readonly property var defaultBlocks: [
        { name: qsTr("Latin-1 Supplement"), first: 0x00A1, last: 0x00FF },
        { name: qsTr("Latin Extended-A"), first: 0x0100, last: 0x017F },
        { name: qsTr("Greek and Coptic"), first: 0x0370, last: 0x03FF },
        { name: qsTr("Cyrillic"), first: 0x0400, last: 0x04FF },
        { name: qsTr("General Punctuation"), first: 0x2010, last: 0x205E },
        { name: qsTr("Currency Symbols"), first: 0x20A0, last: 0x20C0 },
        { name: qsTr("Letterlike Symbols"), first: 0x2100, last: 0x214F },
        { name: qsTr("Number Forms"), first: 0x2150, last: 0x218B },
        { name: qsTr("Arrows"), first: 0x2190, last: 0x21FF },
        { name: qsTr("Mathematical Operators"), first: 0x2200, last: 0x22FF },
        { name: qsTr("Miscellaneous Technical"), first: 0x2300, last: 0x23FF },
        { name: qsTr("Box Drawing"), first: 0x2500, last: 0x257F },
        { name: qsTr("Geometric Shapes"), first: 0x25A0, last: 0x25FF },
        { name: qsTr("Miscellaneous Symbols"), first: 0x2600, last: 0x26FF },
        { name: qsTr("Dingbats"), first: 0x2700, last: 0x27BF }
    ]

    signal characterChosen(string text)

    function charactersOf(block) {
        const out = []
        if (!block) return out
        const first = Math.max(0, Number(block.first) || 0)
        const last = Math.min(0x10FFFF, Number(block.last) || 0, first + 4095)
        for (let code = first; code <= last; ++code) {
            if (code < 0x20 || (code >= 0x7F && code <= 0x9F) || (code >= 0xD800 && code <= 0xDFFF)) continue
            out.push(String.fromCodePoint(code))
        }
        return out
    }
    function codePointLabel(text) {
        if (!text) return ""
        return "U+" + text.codePointAt(0).toString(16).toUpperCase().padStart(4, "0")
    }
    function choose(text) {
        if (!text || !enabled) return false
        currentCharacter = text
        characterChosen(text)
        return true
    }

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight * 2
    padding: 0
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    contentItem: Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Flex.shrink: 0
            Tk.Caption { text: qsTr("Subset") }
            Tk.ComboBox {
                objectName: "characterMapBlock"
                small: true
                model: map.blocks.map(block => String(block.name))
                currentIndex: map.currentBlock
                tooltip: qsTr("Unicode block")
                onActivated: function(index) {
                    map.currentBlock = index
                    map.currentCharacter = map.characters.length ? map.characters[0] : ""
                    grid.currentIndex = 0
                }
                Tk.Flex.grow: 1
            }
        }
        Tk.Flex {
            objectName: "characterMapRecent"
            visible: map.recent.length > 0
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.Flex.shrink: 0
            Tk.Caption { text: qsTr("Recent") }
            Repeater {
                model: map.recent
                delegate: Tk.Button {
                    required property string modelData
                    required property int index
                    objectName: "characterRecent_" + index
                    small: true
                    text: modelData
                    font.family: map.fontFamily
                    tooltip: map.codePointLabel(modelData)
                    onClicked: map.choose(modelData)
                }
            }
        }
        Rectangle {
            Tk.Flex.grow: 1
            Tk.Flex.basis: 0
            color: Tk.Theme.color.inputBg
            border.width: Tk.Theme.size.border
            border.color: grid.activeFocus ? Tk.Theme.color.inputFocusBorder : Tk.Theme.color.inputBorder
            radius: Tk.Theme.radius.sm
            GridView {
                id: grid
                objectName: "characterMapGrid"
                anchors.fill: parent
                anchors.margins: Tk.Theme.size.border
                clip: true
                model: map.characters
                cellWidth: Math.max(Tk.Theme.size.control, Math.floor(width / Math.max(1, map.columns)))
                cellHeight: Tk.Theme.size.controlLg
                keyNavigationEnabled: true
                activeFocusOnTab: true
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: 0
                currentIndex: Math.max(0, map.characters.indexOf(map.currentCharacter))
                onCurrentIndexChanged: if (currentIndex >= 0 && currentIndex < map.characters.length) map.currentCharacter = map.characters[currentIndex]
                Keys.onReturnPressed: map.choose(map.currentCharacter)
                Keys.onEnterPressed: map.choose(map.currentCharacter)
                Accessible.role: Accessible.Table
                Accessible.name: String((map.blocks[map.currentBlock] || {}).name || "")
                T.ScrollBar.vertical: Tk.ScrollBar { }
                delegate: Rectangle {
                    id: cell
                    required property string modelData
                    required property int index
                    readonly property bool current: modelData === map.currentCharacter
                    objectName: "characterCell_" + map.codePointLabel(modelData).slice(2)
                    width: grid.cellWidth
                    height: grid.cellHeight
                    color: current ? Tk.Theme.color.selection : hover.hovered ? Tk.Theme.color.hover : "transparent"
                    border.width: current && grid.activeFocus ? Tk.Theme.size.border : 0
                    border.color: Tk.Theme.color.focus
                    Accessible.role: Accessible.Cell
                    Accessible.name: map.codePointLabel(modelData)
                    Accessible.selected: current
                    Tk.Label {
                        anchors.centerIn: parent
                        text: cell.modelData
                        font.family: map.fontFamily
                        font.pixelSize: Tk.Theme.font.large
                    }
                    HoverHandler { id: hover }
                    // AGENT-NOTE: a MouseArea, not a TapHandler: inside the
                    // GridView a TapHandler's second tap is not counted as a
                    // double tap, so double-click would never choose.
                    MouseArea {
                        anchors.fill: parent
                        onClicked: { grid.currentIndex = cell.index; grid.forceActiveFocus() }
                        onDoubleClicked: map.choose(cell.modelData)
                    }
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.md
            Tk.Flex.shrink: 0
            Rectangle {
                objectName: "characterMapPreview"
                implicitWidth: Tk.Theme.size.action
                implicitHeight: Tk.Theme.size.action
                color: Tk.Theme.color.panel
                border.width: Tk.Theme.size.border
                border.color: Tk.Theme.color.border
                radius: Tk.Theme.radius.sm
                Tk.Label {
                    objectName: "characterMapGlyph"
                    anchors.centerIn: parent
                    text: map.currentCharacter
                    font.family: map.fontFamily
                    font.pixelSize: Tk.Theme.font.display
                }
            }
            Tk.Mono { objectName: "characterMapCode"; text: map.codePointLabel(map.currentCharacter); Tk.Flex.grow: 1 }
            Tk.Button {
                objectName: "characterMapInsert"
                text: qsTr("Insert")
                variant: "accent"
                enabled: map.currentCharacter.length > 0
                tooltip: qsTr("Insert %1").arg(map.codePointLabel(map.currentCharacter))
                onClicked: map.choose(map.currentCharacter)
            }
        }
    }
}
