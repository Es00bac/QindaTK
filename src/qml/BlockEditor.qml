// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The host owns block IDs, ordering, canonical data, undo and persistence.
// Editors submit snapshots; they never split, merge or reorder local copies.
Item {
    id: control
    property string notebookId: ""
    property string pageId: ""
    property var blocks: [] // [{id,type,data,locked,error}]
    property string selectedBlockId: ""
    property string focusedBlockId: ""
    property var blockTypes: [
        { id: "text", label: qsTr("Text") }, { id: "heading", label: qsTr("Heading") },
        { id: "image", label: qsTr("Image") }, { id: "code", label: qsTr("Code") },
        { id: "table", label: qsTr("Table") }, { id: "embed", label: qsTr("Embed") }, { id: "ink", label: qsTr("Ink") }
    ]
    property var capabilities: ({ canEdit: true, canInsert: true, canMove: true, canSplit: true, canMerge: true, canDelete: true })
    property var provider: null
    property var document: null
    property string viewId: ""
    property string errorText: ""
    property string tooltip: qsTr("Edit ordered notebook content blocks")
    readonly property var selectedBlock: findBlock(selectedBlockId)

    signal blockSelectionRequested(string blockId)
    signal blockFocusRequested(string blockId)
    signal blockNavigateRequested(string blockId, string direction)
    signal blockInsertRequested(string afterBlockId, string type, var data, string requestId)
    signal blockMoveRequested(string blockId, int targetIndex, string requestId)
    signal blockContentEditRequested(string blockId, var data, string requestId)
    signal blockSplitRequested(string blockId, int cursorPosition, string requestId)
    signal blockMergeRequested(string firstBlockId, string secondBlockId, string requestId)
    signal blockDeleteRequested(string blockId)
    signal blockActionRequested(string blockId, string action, string requestId)
    signal blockOpenRequested(string blockId, string targetId)

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: Tk.Theme.size.panelMinHeight
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: qsTr("Page %1, %2 content blocks").arg(pageId).arg(blocks.length)
    activeFocusOnTab: true

    function findBlock(id) {
        for (let i = 0; i < blocks.length; ++i) if (blocks[i].id === id) return blocks[i]
        return null
    }
    function indexOf(id) {
        for (let i = 0; i < blocks.length; ++i) if (blocks[i].id === id) return i
        return -1
    }
    function makeDefaultData(type) {
        if (type === "text") return { text: qsTr("Write something…") }
        if (type === "heading") return { text: qsTr("Section heading"), level: 2 }
        if (type === "code") return { text: "", language: "text" }
        if (type === "table") return { columns: ["Column 1", "Column 2"], rows: [["", ""], ["", ""]] }
        if (type === "image") return { resourceId: "", caption: "", state: "missing" }
        if (type === "embed") return { targetId: "", title: qsTr("Linked content"), state: "unavailable" }
        if (type === "ink") return { strokes: [], previewUrl: "", state: "empty" }
        return { raw: "", unsupported: true }
    }
    function insertBlock(type) {
        if (!enabled || !capabilities.canInsert) return
        const after = selectedBlockId || (blocks.length ? blocks[blocks.length - 1].id : "")
        blockInsertRequested(after, type, makeDefaultData(type), notebookId + ":" + pageId + ":block:" + Date.now())
    }
    function moveBlock(id, delta) {
        const from = indexOf(id), target = from + delta
        if (enabled && capabilities.canMove && from >= 0 && target >= 0 && target < blocks.length)
            blockMoveRequested(id, target, notebookId + ":" + pageId + ":move:" + Date.now())
    }
    function submitText(block, text) {
        if (!capabilities.canEdit || block.locked) return
        blockContentEditRequested(block.id, Object.assign({}, block.data || ({}), { text: text }),
                                  notebookId + ":" + block.id + ":edit:" + Date.now())
    }
    function splitBlock(block, cursorPosition) {
        if (capabilities.canSplit && block.type === "text" && !block.locked)
            blockSplitRequested(block.id, cursorPosition, notebookId + ":" + block.id + ":split:" + Date.now())
    }
    function mergeWithPrevious(block) {
        const index = indexOf(block.id)
        if (capabilities.canMerge && index > 0 && block.type === "text" && blocks[index - 1].type === "text")
            blockMergeRequested(blocks[index - 1].id, block.id, notebookId + ":merge:" + Date.now())
    }

    Tk.Flex {
        id: editorContent
        anchors.fill: parent
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm
        Tk.Flex {
            objectName: "blockEditorToolbar"
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Tk.Overline { title: qsTr("PAGE CONTENT"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.ComboBox {
                id: insertType
                objectName: "blockInsertType"
                model: control.blockTypes
                textRole: "label"
                currentIndex: 0
                tooltip: qsTr("Choose a block type to insert")
                enabled: control.capabilities.canInsert
            }
            Tk.Button {
                objectName: "blockInsert"
                text: qsTr("Insert")
                iconName: "plus"
                tooltip: qsTr("Insert a block after the selected block")
                enabled: control.capabilities.canInsert && insertType.currentIndex >= 0
                onClicked: control.insertBlock(control.blockTypes[insertType.currentIndex].id)
            }
        }
        Tk.Caption {
            objectName: "blockEditorError"
            visible: control.errorText.length > 0
            text: control.errorText
            color: Tk.Theme.color.warning
            wrapMode: Text.WordWrap
        }
        Tk.Scroll {
            objectName: "blockEditorList"
            overflowY: Tk.Scroll.Auto
            Tk.Flex {
                id: blockList
                direction: Tk.Flex.Column
                align: Tk.Flex.Stretch
                gap: Tk.Theme.space.sm
                Repeater {
                    model: control.blocks
                    delegate: Tk.Box {
                        id: blockCard
                        required property var modelData
                        required property int index
                        objectName: "notebookBlock_" + modelData.id
                        color: control.selectedBlockId === modelData.id ? Tk.Theme.color.panelAlt : Tk.Theme.color.panel
                        borderColor: control.selectedBlockId === modelData.id ? Tk.Theme.color.accent : Tk.Theme.color.border
                        radius: Tk.Theme.radius.md
                        padding: Tk.Theme.space.sm
                        Accessible.role: Accessible.Grouping
                        Accessible.name: qsTr("%1 block, %2").arg(modelData.type).arg(modelData.data && (modelData.data.title || modelData.data.text) || modelData.id)
                        Accessible.description: modelData.locked ? qsTr("Read only") : modelData.error || ""
                        property var dataSnapshot: modelData.data || ({})

                        Tk.Flex {
                            anchors.fill: parent
                            direction: Tk.Flex.Column
                            gap: Tk.Theme.space.xs
                            Tk.Flex {
                                direction: Tk.Flex.Row
                                align: Tk.Flex.Center
                                gap: Tk.Theme.space.xs
                                Tk.Button {
                                    objectName: "blockSelect_" + modelData.id
                                    text: modelData.data && (modelData.data.title || modelData.data.language) || modelData.type
                                    tooltip: qsTr("Select %1 block").arg(modelData.type)
                                    variant: control.selectedBlockId === modelData.id ? "accent" : "ghost"
                                    onClicked: control.blockSelectionRequested(modelData.id)
                                    Tk.Flex.grow: 1
                                    Tk.Flex.basis: 0
                                }
                                Tk.Badge {
                                    visible: !!modelData.locked
                                    text: qsTr("Read only")
                                    variant: "warning"
                                    tooltip: qsTr("This block is read only")
                                }
                                Tk.IconButton {
                                    objectName: "blockMoveUp_" + modelData.id
                                    iconName: "arrow-up"
                                    tooltip: qsTr("Move block earlier")
                                    enabled: control.capabilities.canMove && !modelData.locked && index > 0
                                    onClicked: control.moveBlock(modelData.id, -1)
                                }
                                Tk.IconButton {
                                    objectName: "blockMoveDown_" + modelData.id
                                    iconName: "arrow-down"
                                    tooltip: qsTr("Move block later")
                                    enabled: control.capabilities.canMove && !modelData.locked && index < control.blocks.length - 1
                                    onClicked: control.moveBlock(modelData.id, 1)
                                }
                                Tk.IconButton {
                                    objectName: "blockDelete_" + modelData.id
                                    iconName: "trash-2"
                                    tooltip: qsTr("Delete block")
                                    enabled: control.capabilities.canDelete && !modelData.locked
                                    onClicked: control.blockDeleteRequested(modelData.id)
                                }
                            }
                            Loader {
                                id: blockContentLoader
                                objectName: "blockContent_" + modelData.id
                                width: parent.width
                                sourceComponent: modelData.type === "text" || modelData.type === "heading" || modelData.type === "code"
                                                 ? textComponent : modelData.type === "image" || modelData.type === "ink"
                                                 ? mediaComponent : modelData.type === "table" ? tableComponent
                                                 : modelData.type === "embed" ? embedComponent : unsupportedComponent
                                onLoaded: {
                                    if (item && item.hasOwnProperty("blockId")) item.blockId = modelData.id
                                    if (item && item.hasOwnProperty("initialData")) item.initialData = blockCard.dataSnapshot
                                    if (item && item.hasOwnProperty("blockType")) item.blockType = modelData.type
                                    if (item && item.hasOwnProperty("locked")) item.locked = !!modelData.locked
                                }
                            }
                            Tk.Flex {
                                direction: Tk.Flex.Row
                                gap: Tk.Theme.space.xs
                                visible: modelData.type === "text"
                                Tk.Button {
                                    objectName: "blockSplit_" + modelData.id
                                    text: qsTr("Split at cursor")
                                    iconName: "scissors"
                                    tooltip: qsTr("Request a text split at the active caret")
                                    enabled: control.capabilities.canSplit && !modelData.locked
                                    onClicked: {
                                        const field = blockContentLoader.item ? blockContentLoader.item.textEditor : null
                                        control.splitBlock(modelData, field ? field.cursorPosition : 0)
                                    }
                                }
                                Tk.Button {
                                    objectName: "blockMerge_" + modelData.id
                                    text: qsTr("Merge with previous")
                                    iconName: "merge"
                                    tooltip: qsTr("Request merging with the previous text block")
                                    enabled: control.capabilities.canMerge && !modelData.locked && index > 0
                                             && control.blocks[index - 1].type === "text"
                                    onClicked: control.mergeWithPrevious(modelData)
                                }
                                Tk.Spacer { size: Tk.Theme.space.xs }
                            }
                            Tk.Caption {
                                objectName: "blockError_" + modelData.id
                                visible: String(modelData.error || "").length > 0
                                text: modelData.error || ""
                                color: Tk.Theme.color.warning
                                wrapMode: Text.WordWrap
                            }
                        }
                        TapHandler { onTapped: control.blockSelectionRequested(modelData.id) }
                    }
                }
                Tk.Caption { visible: control.blocks.length === 0; text: qsTr("This page is empty. Insert a block to begin."); color: Tk.Theme.color.textMuted }
            }
        }
    }

    Component {
        id: textComponent
        Item {
            id: textBlock
            property string blockId: ""
            property string blockType: "text"
            property var initialData: ({})
            property bool locked: false
            readonly property alias textEditor: textField
            implicitHeight: textField.implicitHeight + Tk.Theme.space.xs
            Tk.TextArea {
                id: textField
                objectName: "blockText_" + parent.blockId
                anchors.left: parent.left
                anchors.right: parent.right
                text: parent.initialData.text || ""
                placeholderText: qsTr("Write in this block")
                rows: parent.blockType === "heading" ? 1 : parent.blockType === "code" ? 4 : 3
                mono: parent.blockType === "code"
                readOnly: parent.locked
                tooltip: parent.blockType === "code" ? qsTr("Code block content") : qsTr("Text block content")
                onEditingFinished: if (!parent.locked && text !== parent.initialData.text)
                                       control.submitText({ id: parent.blockId, data: parent.initialData, locked: parent.locked }, text)
                Keys.onPressed: function(event) {
                    if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Return) {
                        control.splitBlock({ id: parent.blockId, type: parent.blockType, locked: parent.locked }, cursorPosition)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Tab && !(event.modifiers & Qt.ShiftModifier)) {
                        control.blockNavigateRequested(parent.blockId, "next")
                    } else if (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier)) {
                        control.blockNavigateRequested(parent.blockId, "previous")
                    }
                }
                onActiveFocusChanged: if (activeFocus) {
                    control.blockFocusRequested(parent.blockId)
                    control.blockSelectionRequested(parent.blockId)
                }
                Component.onCompleted: if (control.focusedBlockId === parent.blockId) forceActiveFocus()
                Connections {
                    target: control
                    function onFocusedBlockIdChanged() {
                        if (control.focusedBlockId === textBlock.blockId) textField.forceActiveFocus()
                    }
                }
            }
        }
    }
    Component {
        id: mediaComponent
        Tk.Flex {
            id: mediaBlock
            property string blockId: ""
            property string blockType: "image"
            property var initialData: ({})
            property bool locked: false
            direction: Tk.Flex.Column
            align: Tk.Flex.Stretch
            gap: Tk.Theme.space.xs
            Tk.AssetThumbnail {
                objectName: "blockMediaPreview_" + parent.blockId
                provider: control.provider
                resourceId: mediaBlock.initialData.resourceId || ""
                document: control.document
                viewId: control.viewId
                requestKind: "thumbnail"
                tooltip: parent.blockType === "ink" ? qsTr("Ink preview") : qsTr("Image block preview")
                active: !!mediaBlock.initialData.resourceId
                implicitWidth: Tk.Theme.size.thumbnail * 2
                implicitHeight: Tk.Theme.size.thumbnailHeight
            }
            Tk.Caption {
                text: mediaBlock.blockType === "ink"
                      ? qsTr("%1 strokes · %2").arg((mediaBlock.initialData.strokes || []).length).arg(mediaBlock.initialData.state || qsTr("No strokes"))
                      : (mediaBlock.initialData.caption || mediaBlock.initialData.resourceId || qsTr("No image linked"))
                color: mediaBlock.initialData.state === "missing" ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
            }
            Tk.Button {
                objectName: "blockMediaRelink_" + parent.blockId
                text: qsTr("Choose media")
                iconName: "image-plus"
                tooltip: qsTr("Request a media link for this block")
                enabled: !mediaBlock.locked
                visible: mediaBlock.blockType === "image"
                onClicked: control.blockActionRequested(mediaBlock.blockId, "choose-media", control.notebookId + ":media:" + Date.now())
            }
        }
    }
    Component {
        id: tableComponent
        Item {
            id: tableBlock
            property string blockId: ""
            property var initialData: ({})
            property bool locked: false
            property var rows: initialData.rows || []
            implicitHeight: Math.min(tableRows.implicitHeight + Tk.Theme.size.control, Tk.Theme.size.panelMinHeight * 2)
            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Column
                gap: Tk.Theme.size.border
                Repeater {
                    model: tableBlock.initialData.rows || []
                    delegate: Tk.Flex {
                        required property var modelData
                        direction: Tk.Flex.Row
                        Repeater {
                            model: modelData
                            delegate: Tk.Box {
                                id: tableCell
                                required property var modelData
                                property string cellText: String(modelData)
                                color: Tk.Theme.color.panelAlt
                                borderColor: Tk.Theme.color.border
                                padding: Tk.Theme.space.xs
                                radius: Tk.Theme.radius.xs
                                Tk.Label { text: tableCell.cellText; wrapMode: Text.WordWrap; elide: Text.ElideRight }
                                Tk.Flex.grow: 1
                                Tk.Flex.basis: 0
                            }
                        }
                    }
                }
                id: tableRows
            }
            Tk.Button {
                objectName: "blockTableEdit_" + parent.blockId
                anchors.right: parent.right
                anchors.top: parent.top
                text: qsTr("Edit table")
                iconName: "table"
                tooltip: qsTr("Open this table in the host table editor")
                onClicked: control.blockActionRequested(tableBlock.blockId, "edit-table", control.notebookId + ":table:" + Date.now())
            }
        }
    }
    Component {
        id: embedComponent
        Tk.Notice {
            id: embedBlock
            property string blockId: ""
            property var initialData: ({})
            property bool locked: false
            title: initialData.title || qsTr("Embedded content")
            text: initialData.description || qsTr("Linked item · %1").arg(initialData.targetId || qsTr("no target"))
            variant: initialData.state === "ready" ? "info" : "warning"
            objectName: "blockEmbed_" + blockId
            Tk.Button {
                text: qsTr("Open")
                iconName: "external-link"
                tooltip: qsTr("Open linked content")
                visible: !!embedBlock.initialData.targetId
                onClicked: control.blockOpenRequested(embedBlock.blockId, embedBlock.initialData.targetId)
            }
        }
    }
    Component {
        id: unsupportedComponent
        Tk.Notice {
            property string blockId: ""
            property string blockType: ""
            property var initialData: ({})
            title: qsTr("Unsupported block type")
            text: qsTr("%1 is preserved by the host but has no editor in this view.").arg(blockType)
            variant: "warning"
            objectName: "blockUnsupported_" + blockId
        }
    }
}
