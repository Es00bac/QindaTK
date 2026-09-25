// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QindaTK as Tk

// Internal part of Tk.BlockEditor: one block card (ListView delegate). It
// holds no edit state of its own; the editor's `drafts` survive recycling.
Tk.Box {
    id: card
    required property string modelData // the block ID (BlockEditor.presentedIds)
    required property int index
    property var editor: null // the owning Tk.BlockEditor (var: a typed property would make a load cycle)
    readonly property var block: (editor ? editor.findBlock(modelData) : null) || ({ id: modelData, type: "", data: ({}) })
    readonly property string blockId: modelData
    readonly property bool textual: editor ? editor.isTextType(block.type) : false
    readonly property bool locked: !!block.locked
    readonly property bool selected: !!editor && (editor.selectedBlockId === block.id || editor.inSelection(block.id))
    readonly property int depth: block.depth || 0
    readonly property var previous: editor && index > 0 ? editor.presentedBlocks[index - 1] : null
    readonly property var contentItem_: content.item
    // Header label: the block kind, plus its title or language when it has one.
    readonly property string typeLabel: {
        const names = { text: qsTr("Text"), heading: qsTr("Heading"), quote: qsTr("Quote"), list: qsTr("List item"),
                        todo: qsTr("To-do"), code: qsTr("Code"), table: qsTr("Table"), chart: qsTr("Chart"),
                        image: qsTr("Image"), embed: qsTr("Embed"), ink: qsTr("Ink"), divider: qsTr("Divider") }
        const name = names[block.type] || String(block.type || "")
        const detail = block.data ? (block.data.title || block.data.language || "") : ""
        return detail.length > 0 ? qsTr("%1 · %2").arg(name).arg(detail) : name
    }

    objectName: "notebookBlock_" + blockId
    color: selected ? Tk.Theme.color.panelAlt : Tk.Theme.color.panel
    borderColor: selected ? Tk.Theme.color.accent : Tk.Theme.color.border
    borderWidth: Tk.Theme.size.border
    radius: Tk.Theme.radius.md
    padding: Tk.Theme.space.sm
    paddingLeft: Tk.Theme.space.sm + depth * Tk.Theme.space.lg
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("%1 block, %2").arg(block.type)
                         .arg(block.data && (block.data.title || block.data.text) || block.id)
    Accessible.description: (locked ? qsTr("Read only. ") : "") + (depth > 0 ? qsTr("Nested level %1. ").arg(depth) : "")
                            + String(block.error || "")

    Tk.Flex {
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.xs
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.Label {
                objectName: "blockType_" + card.blockId
                text: card.typeLabel
                muted: !card.selected
                accent: card.selected
                horizontalAlignment: Text.AlignLeft
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Badge {
                visible: card.locked
                text: qsTr("Read only")
                variant: "warning"
                tooltip: qsTr("This block is read only")
            }
            Tk.Badge {
                visible: card.block.supported === false
                text: qsTr("Preserved")
                tooltip: qsTr("This block type is kept unchanged but has no editor here")
            }
            Tk.IconButton {
                objectName: "blockMoveUp_" + card.blockId
                iconName: "arrow-up"
                tooltip: qsTr("Move block earlier")
                enabled: card.editor.capabilities.canMove && !card.locked && card.index > 0
                onClicked: card.editor.moveBlock(card.blockId, -1)
            }
            Tk.IconButton {
                objectName: "blockMoveDown_" + card.blockId
                iconName: "arrow-down"
                tooltip: qsTr("Move block later")
                enabled: card.editor.capabilities.canMove && !card.locked
                         && card.index < card.editor.presentedBlocks.length - 1
                onClicked: card.editor.moveBlock(card.blockId, 1)
            }
            Tk.IconButton {
                objectName: "blockIndent_" + card.blockId
                iconName: "list-indent-increase"
                tooltip: qsTr("Nest this block under the one above")
                enabled: card.editor.capabilities.canNest !== false && !card.locked && card.index > 0
                onClicked: card.editor.nestBlock(card.blockId, "indent")
            }
            Tk.IconButton {
                objectName: "blockOutdent_" + card.blockId
                iconName: "list-indent-decrease"
                tooltip: qsTr("Move this block out one level")
                enabled: card.editor.capabilities.canNest !== false && !card.locked && card.depth > 0
                onClicked: card.editor.nestBlock(card.blockId, "outdent")
            }
            Tk.IconButton {
                objectName: "blockDelete_" + card.blockId
                iconName: "trash-2"
                tooltip: qsTr("Delete block")
                enabled: card.editor.capabilities.canDelete && !card.locked
                onClicked: card.editor.deleteBlock(card.blockId)
            }
        }
        Loader {
            id: content
            objectName: "blockContent_" + card.blockId
            sourceComponent: card.textual ? textComponent
                             : card.block.type === "table" ? tableComponent
                             : card.block.type === "chart" ? chartComponent : mediaComponent
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            visible: card.textual
            Tk.Button {
                objectName: "blockSplit_" + card.blockId
                text: qsTr("Split at cursor")
                iconName: "scissors"
                tooltip: qsTr("Split this block at the caret")
                enabled: card.editor.capabilities.canSplit && !card.locked
                onClicked: {
                    const field = card.contentItem_ && card.contentItem_.textEditor ? card.contentItem_.textEditor : null
                    card.editor.splitBlock(card.block, field ? field.cursorPosition : 0)
                }
            }
            Tk.Button {
                objectName: "blockMerge_" + card.blockId
                text: qsTr("Merge with previous")
                iconName: "arrow-up-to-line"
                tooltip: qsTr("Join this block's text to the block above")
                enabled: card.editor.capabilities.canMerge && !card.locked && !!card.previous
                         && card.editor.isTextType(card.previous.type)
                onClicked: card.editor.mergeWithPrevious(card.block)
            }
            Tk.Button {
                objectName: "blockEvaluate_" + card.blockId
                visible: card.block.type === "code"
                text: qsTr("Run")
                iconName: "play"
                tooltip: qsTr("Evaluate this code block with the script runtime")
                enabled: !card.editor.scriptRuntime || !card.editor.scriptRuntime.running
                onClicked: card.editor.evaluate(card.blockId)
            }
            Tk.Spacer { size: Tk.Theme.space.xs }
        }
        Tk.Caption {
            objectName: "blockOutput_" + card.blockId
            visible: card.block.type === "code" && !!card.block.data && !!card.block.data.status
            text: !card.block.data ? "" : card.block.data.status === "failed"
                  ? qsTr("Error: %1").arg(card.block.data.error || "")
                  : [card.block.data.output || "", card.block.data.result !== undefined && card.block.data.result !== null
                     ? qsTr("Result: %1").arg(JSON.stringify(card.block.data.result)) : ""].filter(function(line) { return line.length > 0 }).join("\n")
            color: card.block.data && card.block.data.status === "failed" ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
            wrapMode: Text.WordWrap
        }
        Tk.Caption {
            objectName: "blockError_" + card.blockId
            visible: String(card.block.error || "").length > 0
            text: card.block.error || ""
            color: Tk.Theme.color.warning
            wrapMode: Text.WordWrap
        }
    }
    // A delegate reused for the same block ID gets no property change, so
    // reload the text part explicitly (drafts may have changed meanwhile).
    ListView.onReused: if (card.contentItem_ && card.contentItem_.load) card.contentItem_.load()
    TapHandler {
        onTapped: {
            if (card.editor.notebook) card.editor.selectedBlockId = card.blockId
            card.editor.blockSelectionRequested(card.blockId)
        }
    }

    Component { id: textComponent; Tk.BlockEditorText { editor: card.editor; block: card.block } }
    Component { id: tableComponent; Tk.BlockEditorTable { editor: card.editor; block: card.block } }
    Component { id: chartComponent; Tk.BlockEditorChart { editor: card.editor; block: card.block } }
    Component { id: mediaComponent; Tk.BlockEditorMedia { editor: card.editor; block: card.block } }
}
