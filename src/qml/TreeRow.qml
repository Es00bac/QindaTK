// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): a ListRow with a depth indent and a
// disclosure chevron. Clicking the chevron (or double-clicking the row)
// toggles `expanded` and emits `expansionToggled`; a nonexpandable,
// noneditable leaf uses the inherited `clicked` action for pointer,
// keyboard and accessible activation. The row never owns disclosed children.
// AGENT-NOTE: the signal is not `toggled(bool)` as docs/controls.md first
// said: AbstractButton already owns a `toggled()` signal and QML refuses
// the override.
Tk.ListRow {
    id: tree

    property int depth: 0
    property bool expandable: false
    property bool expanded: false
    // Inline rename: startEditing() (or a double-click when `editable`)
    // overlays a field on the label; Enter emits renamed(text), Escape
    // cancels. The row never changes its own `text`: the owner does after
    // it has renamed the real thing.
    property bool editable: false
    readonly property bool editing: editor.visible
    // Drag and drop (D-326), both opt-in: a `dragEnabled` row can be dragged
    // (a ghost follows the pointer); a `dropEnabled` row accepts rows with
    // the same `dragKey` and shows where one would land — "before" or
    // "after" it (outer quarters), "inside" (the middle, only when
    // `acceptsChildren`) — then emits dropRequested(sourceId, position).
    // The row moves nothing: its owner reorders the model.
    property bool dragEnabled: false
    property bool dropEnabled: false
    property string dragKey: "qindatk-tree-row"
    property string itemId: ""
    property bool acceptsChildren: expandable
    readonly property bool dragging: rowDrag.active
    readonly property string dropPosition: _dropPosition
    property string _dropPosition: ""
    signal expansionToggled(bool expanded)
    signal renamed(string text)
    signal dragStarted()
    signal dragFinished(bool dropped)
    signal dropRequested(string sourceId, string position)

    function dropPositionAt(y) {
        const quarter = height / 4
        if (acceptsChildren && y > quarter && y < height - quarter) return "inside"
        return y < height / 2 ? "before" : "after"
    }

    indent: tree.depth * Tk.Theme.space.lg
    focusPolicy: Qt.StrongFocus
    activeFocusOnTab: enabled

    Accessible.role: Accessible.TreeItem
    Accessible.name: tree.text
    Accessible.description: tree.expandable
                              ? tree.expanded ? qsTr("Expanded") : qsTr("Collapsed")
                              : ""
    Accessible.focusable: enabled
    Accessible.focused: activeFocus

    Rectangle {
        objectName: "treeFocusRing"
        z: 1
        anchors.fill: parent
        anchors.margins: -Tk.Theme.size.focusRing
        color: "transparent"
        border.width: Tk.Theme.size.focusRing
        border.color: Tk.Theme.color.focus
        visible: tree.activeFocus && tree.enabled
    }

    function toggle() {
        if (!tree.enabled || !tree.expandable) {
            return
        }
        tree.expanded = !tree.expanded
        tree.expansionToggled(tree.expanded)
    }

    function startEditing() {
        if (!tree.enabled || !tree.editable) {
            return
        }
        editor.text = tree.text
        editor.visible = true
        editor.forceActiveFocus(Qt.TabFocusReason)
        editor.selectAll()
    }
    function cancelEditing() {
        editor.visible = false
    }

    function activate() {
        if (!tree.enabled) {
            return
        }
        if (tree.expandable) {
            tree.toggle()
        } else if (tree.editable) {
            tree.startEditing()
        } else {
            tree.click()
        }
    }

    Keys.onSpacePressed: function(event) {
        if (!tree.editing && tree.enabled && (tree.expandable || !tree.editable)) {
            tree.activate()
            event.accepted = true
        }
    }
    Keys.onReturnPressed: function(event) {
        if (!tree.editing && tree.enabled && (tree.expandable || !tree.editable)) {
            tree.activate()
            event.accepted = true
        }
    }
    Keys.onEnterPressed: function(event) {
        if (!tree.editing && tree.enabled && (tree.expandable || !tree.editable)) {
            tree.activate()
            event.accepted = true
        }
    }
    Keys.onPressed: function(event) {
        if (!tree.editing && event.key === Qt.Key_F2 && tree.enabled && tree.editable) {
            tree.startEditing()
            event.accepted = true
        }
    }
    // AGENT-GUARD: ListRow already maps its accessible press to click(). Let
    // nonexpandable, noneditable leaves inherit that single guarded action;
    // invoking activate() here as well would emit clicked() twice.
    Accessible.onPressAction: if (tree.expandable || tree.editable) tree.activate()

    onDoubleClicked: tree.editable ? tree.startEditing() : tree.toggle()

    Tk.TextField {
        id: editor
        objectName: "treeEditor"
        visible: false
        small: true
        z: 2
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: tree.leftPadding + Tk.Theme.size.iconSm + tree.spacing
        anchors.rightMargin: tree.rightPadding
        onAccepted: {
            const value = editor.text.trim()
            editor.visible = false
            if (value.length > 0 && value !== tree.text) {
                tree.renamed(value)
            }
        }
        Keys.onEscapePressed: function(event) {
            editor.visible = false
            event.accepted = true
        }
        onActiveFocusChanged: if (!activeFocus && visible) editor.visible = false
    }

    DragHandler {
        id: rowDrag
        enabled: tree.dragEnabled && tree.enabled && !tree.editing
        target: null
        // AGENT-GUARD: the ghost's state (shown, parent, position) and
        // Drag.active are set here, never bound to `active`: a binding could
        // run before this handler on release, move the ghost back over the
        // source row and end the drag, so drop() would deliver nothing (it
        // passed alone and failed in the full QML run).
        onActiveChanged: {
            if (active) {
                ghost.Drag.hotSpot = centroid.pressPosition
                tree.placeGhost()
                tree._ghostShown = true
                ghost.Drag.active = true
                tree.dragStarted()
            } else {
                const action = ghost.Drag.drop()
                ghost.Drag.active = false
                tree._ghostShown = false
                tree.dragFinished(action !== Qt.IgnoreAction)
            }
        }
        onCentroidChanged: if (active) tree.placeGhost()
    }
    property bool _ghostShown: false
    readonly property string idKeyPrefix: "qindatk-tree-row-id:"
    function placeGhost() {
        ghost.x = rowDrag.centroid.scenePosition.x - rowDrag.centroid.pressPosition.x
        ghost.y = rowDrag.centroid.scenePosition.y - rowDrag.centroid.pressPosition.y
    }
    // The drag ghost: the row's label on the overlay, under the pointer.
    Rectangle {
        id: ghost
        objectName: "treeDragGhost"
        readonly property Item overlay: T.Overlay.overlay
        parent: tree._ghostShown && overlay ? overlay : tree
        visible: tree._ghostShown
        width: tree.width
        height: tree.height
        radius: Tk.Theme.radius.sm
        color: Tk.Theme.color.accentSubtle
        border.width: Tk.Theme.size.border
        border.color: Tk.Theme.color.accent
        opacity: Tk.Theme.opacity.ghost
        // AGENT-NOTE: the row's id travels as a second drag key. An internal
        // drag carries no mime data, and a cast of `source` to this row's own
        // type does not resolve inside its own file.
        Drag.keys: [tree.dragKey, tree.idKeyPrefix + tree.itemId]
        Drag.source: tree
        Tk.Label {
            x: tree.leftPadding + tree.indent + Tk.Theme.size.iconSm + tree.spacing
            width: Math.max(0, parent.width - x)
            height: parent.height
            text: tree.text
        }
    }
    DropArea {
        objectName: "treeDropArea"
        anchors.fill: parent
        enabled: tree.dropEnabled && tree.enabled
        keys: [tree.dragKey]
        onEntered: function(drag) {
            if (drag.source === tree) { drag.accepted = false; return }
            tree._dropPosition = tree.dropPositionAt(drag.y)
        }
        onPositionChanged: function(drag) { if (drag.source !== tree) tree._dropPosition = tree.dropPositionAt(drag.y) }
        onExited: tree._dropPosition = ""
        onDropped: function(drop) {
            const position = tree._dropPosition.length ? tree._dropPosition : tree.dropPositionAt(drop.y)
            tree._dropPosition = ""
            if (!drop.source || drop.source === tree) return
            drop.acceptProposedAction()
            let sourceId = ""
            for (let i = 0; i < drop.keys.length; ++i) {
                const key = String(drop.keys[i])
                if (key.startsWith(tree.idKeyPrefix)) sourceId = key.slice(tree.idKeyPrefix.length)
            }
            tree.dropRequested(sourceId, position)
        }
    }
    // Where a hovering drag would land: a line above or below, or a frame.
    Rectangle {
        objectName: "treeDropLine"
        visible: tree._dropPosition === "before" || tree._dropPosition === "after"
        z: 3
        x: tree.indent
        y: tree._dropPosition === "after" ? parent.height - height : 0
        width: Math.max(0, parent.width - x)
        height: Tk.Theme.size.focusRing
        color: Tk.Theme.color.accent
    }
    Rectangle {
        objectName: "treeDropFrame"
        visible: tree._dropPosition === "inside"
        z: 3
        anchors.fill: parent
        color: "transparent"
        radius: Tk.Theme.radius.sm
        border.width: Tk.Theme.size.focusRing
        border.color: Tk.Theme.color.accent
    }

    // AGENT-NOTE: parented into the ListRow's leading slot so callers that
    // add their own leading items keep the chevron first.
    Item {
        parent: tree.leadingItem
        objectName: "treeChevron"
        implicitWidth: Tk.Theme.size.iconSm
        implicitHeight: Tk.Theme.size.iconSm
        Tk.Flex.shrink: 0

        Tk.Icon {
            anchors.centerIn: parent
            visible: tree.expandable
            name: tree.expanded ? "chevron-down" : "chevron-right"
            size: Tk.Theme.size.iconSm
            color: Tk.Theme.color.textMuted
        }
        TapHandler {
            enabled: tree.enabled && tree.expandable
            onTapped: tree.toggle()
        }
    }
}
