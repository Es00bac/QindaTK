// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Shape definitions and factories belong to the document host. This palette
// only requests insertion by stable definition ID and passes default params.
//
// AGENT-CONTRACT (docs/controls.md ShapePalette, D-302): a definition with an
// `imageSource` is drawn as a tile (the host's own thumbnail over a caption),
// one without keeps the icon button. With `dragEnabled`, dragging a tile or
// button reports shapeDragStarted/Moved/Finished in SCENE coordinates (the
// window content item's space); the host maps the point into its canvas,
// draws its own drop preview and inserts on finish — the palette never
// creates, previews or places a shape itself.
Item {
    id: control
    property var definitions: [] // [{id, label, category, iconName, imageSource, parameters, enabled, description}]
    property var recentIds: []
    property string searchText: ""
    property string category: "all"
    property string tooltip: qsTr("Choose a shape to insert into the document")
    property int columns: 3
    // "segmented" (a row of segments) or "menu" (a combo box, for many
    // categories in a narrow panel).
    property string categoryPicker: "segmented"
    // Search every category, not only the chosen one, while text is typed.
    property bool searchAllCategories: false
    property bool dragEnabled: false
    // Edge of a tile's thumbnail box.
    property real tileSize: Tk.Theme.size.thumbnailHeight
    readonly property var categories: collectCategories()
    readonly property var visibleDefinitions: filterDefinitions()
    readonly property string draggingShapeId: dragState.shapeId

    signal shapeRequested(string shapeId, var parameters)
    signal shapeDragStarted(string shapeId)
    signal shapeDragMoved(string shapeId, point scenePoint)
    signal shapeDragFinished(string shapeId, var parameters, point scenePoint)
    signal shapeDragCanceled(string shapeId)

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: content.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    function collectCategories() {
        const result = ["all"]
        for (let i = 0; i < definitions.length; ++i) {
            const value = definitions[i].category || "Shapes"
            if (result.indexOf(value) < 0) result.push(value)
        }
        return result
    }
    function categoryLabel(value) { return value === "all" ? qsTr("All shapes") : value }
    function filterDefinitions() {
        const query = searchText.trim().toLocaleLowerCase()
        const everywhere = control.searchAllCategories && query.length > 0
        return definitions.filter(function(shape) {
            const categoryMatch = everywhere || control.category === "all"
                || (shape.category || "Shapes") === control.category
            const text = (shape.label || shape.id || "").toLocaleLowerCase()
            const description = (shape.description || "").toLocaleLowerCase()
            return categoryMatch && (!query || text.indexOf(query) >= 0 || description.indexOf(query) >= 0)
        })
    }
    function remember(shape) {
        control.recentIds = [shape.id].concat(control.recentIds.filter(function(id) { return id !== shape.id })).slice(0, 8)
    }
    function requestInsert(shape) {
        if (!enabled || !shape || shape.enabled === false) return
        control.shapeRequested(shape.id, Object.assign({}, shape.parameters || {}))
        remember(shape)
    }
    // Drag bookkeeping, called by the tiles' and buttons' drag handlers.
    function beginDrag(shape) {
        dragState.shapeId = String(shape.id)
        control.shapeDragStarted(dragState.shapeId)
    }
    function moveDrag(scenePoint) {
        if (dragState.shapeId.length > 0) control.shapeDragMoved(dragState.shapeId, scenePoint)
    }
    function finishDrag(shape, scenePoint) {
        if (dragState.shapeId.length === 0) return
        const id = dragState.shapeId
        dragState.shapeId = ""
        control.shapeDragFinished(id, Object.assign({}, shape.parameters || {}), scenePoint)
        remember(shape)
    }
    function cancelDrag() {
        if (dragState.shapeId.length === 0) return
        const id = dragState.shapeId
        dragState.shapeId = ""
        control.shapeDragCanceled(id)
    }

    // The one drag in flight (a palette drags one shape at a time).
    QtObject {
        id: dragState
        property string shapeId: ""
    }

    // Drag tracking shared by tiles and buttons: the handler never moves its
    // parent (target: null); it only reports where the pointer is.
    // AGENT-NOTE: inline components cannot see this file's ids, so the
    // palette is passed in (DockFloating.qml has the same note).
    component ShapeDrag: DragHandler {
        id: handler
        required property var paletteRoot
        required property Item owner
        required property var shape
        target: null
        enabled: handler.paletteRoot.dragEnabled && handler.paletteRoot.enabled && shape.enabled !== false
        function scenePoint() { return handler.owner.mapToItem(null, handler.centroid.position) }
        onActiveChanged: {
            if (active) handler.paletteRoot.beginDrag(shape)
            else handler.paletteRoot.finishDrag(shape, scenePoint())
        }
        onCentroidChanged: if (active) handler.paletteRoot.moveDrag(scenePoint())
        onCanceled: handler.paletteRoot.cancelDrag()
    }

    // A host thumbnail over a two-line caption.
    component ShapeTile: T.AbstractButton {
        id: tile
        required property var paletteRoot
        required property var shape
        text: shape.label || shape.id || ""
        enabled: shape.enabled !== false
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        padding: Tk.Theme.space.xs
        implicitWidth: tile.paletteRoot.tileSize + leftPadding + rightPadding
        implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
        Accessible.role: Accessible.Button
        Accessible.name: text
        Accessible.description: shape.description || ""
        Accessible.focusable: enabled
        Accessible.onPressAction: if (enabled) click()
        onClicked: tile.paletteRoot.requestInsert(shape)
        background: Rectangle {
            radius: Tk.Theme.radius.sm
            color: tile.down ? Tk.Theme.color.pressed
                 : tile.hovered || tile.paletteRoot.draggingShapeId === String(tile.shape.id) ? Tk.Theme.color.hover
                 : Tk.Theme.alpha(Tk.Theme.color.panelAlt, Tk.Theme.opacity.hidden)
            border.width: tile.activeFocus ? Tk.Theme.size.focusRing : Tk.Theme.size.border
            border.color: tile.activeFocus ? Tk.Theme.color.focus
                        : tile.hovered ? Tk.Theme.color.controlHoverBorder
                        : Tk.Theme.alpha(Tk.Theme.color.panelAlt, Tk.Theme.opacity.hidden)
        }
        contentItem: Tk.Flex {
            direction: Tk.Flex.Column
            align: Tk.Flex.Center
            gap: Tk.Theme.space.xs
            Item {
                implicitWidth: tile.paletteRoot.tileSize
                implicitHeight: tile.paletteRoot.tileSize
                Tk.Flex.shrink: 0
                Image {
                    objectName: "shapeThumbnail"
                    anchors.fill: parent
                    source: tile.shape.imageSource || ""
                    sourceSize.width: tile.paletteRoot.tileSize * 2
                    sourceSize.height: tile.paletteRoot.tileSize * 2
                    fillMode: Image.PreserveAspectFit
                    opacity: tile.enabled ? Tk.Theme.opacity.full : Tk.Theme.opacity.disabled
                    smooth: true
                    mipmap: true
                }
            }
            Tk.Caption {
                objectName: "shapeCaption"
                text: tile.text
                color: Tk.Theme.color.text
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                Tk.Flex.alignSelf: Tk.Flex.Stretch
                Tk.Flex.shrink: 0
            }
        }
        Tk.ToolTip {
            text: tile.shape.description || tile.text
            visible: text.length > 0 && tile.hovered && tile.paletteRoot.draggingShapeId.length === 0
        }
        ShapeDrag { paletteRoot: tile.paletteRoot; owner: tile; shape: tile.shape }
    }

    component ShapeButton: Tk.Button {
        id: button
        required property var paletteRoot
        required property var shape
        text: shape.label
        iconName: shape.iconName || "shapes"
        tooltip: shape.description || shape.label
        enabled: shape.enabled !== false
        variant: "outline"
        onClicked: button.paletteRoot.requestInsert(shape)
        ShapeDrag { paletteRoot: button.paletteRoot; owner: button; shape: button.shape }
    }

    Tk.Flex {
        id: content
        width: control.width > 0 ? control.width : implicitWidth
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm

        Tk.Overline { title: qsTr("SHAPES") }
        Tk.TextField {
            objectName: "shapeSearch"
            placeholderText: qsTr("Search shapes")
            iconName: "search"
            tooltip: qsTr("Filter shape definitions")
            onTextChanged: if (activeFocus) control.searchText = text
        }
        Tk.Segmented {
            objectName: "shapeCategories"
            visible: control.categoryPicker !== "menu"
            model: control.categories
            currentIndex: Math.max(0, control.categories.indexOf(control.category))
            tooltip: qsTr("Shape category")
            onActivated: function(index) { control.category = control.categories[index] }
        }
        Tk.ComboBox {
            objectName: "shapeCategoryMenu"
            visible: control.categoryPicker === "menu"
            model: control.categories.map(control.categoryLabel)
            currentIndex: Math.max(0, control.categories.indexOf(control.category))
            tooltip: qsTr("Shape category")
            onActivated: function(index) { control.category = control.categories[index] }
        }
        Tk.Caption {
            visible: control.recentIds.length > 0
            text: qsTr("Recent: %1").arg(control.recentIds.map(function(id) {
                for (let i = 0; i < control.definitions.length; ++i)
                    if (control.definitions[i].id === id) return control.definitions[i].label
                return id
            }).join(" · "))
            elide: Text.ElideRight
        }
        Tk.Grid {
            objectName: "shapeGrid"
            columns: "repeat(" + Math.max(1, control.columns) + ", 1fr)"
            gap: Tk.Theme.space.xs
            Repeater {
                model: control.visibleDefinitions
                delegate: Loader {
                    id: slot
                    required property var modelData
                    sourceComponent: modelData.imageSource ? tileComponent : buttonComponent
                    onLoaded: item.objectName = "shape_" + modelData.id
                    Component {
                        id: tileComponent
                        ShapeTile { paletteRoot: control; shape: slot.modelData }
                    }
                    Component {
                        id: buttonComponent
                        ShapeButton { paletteRoot: control; shape: slot.modelData }
                    }
                }
            }
        }
        Tk.Caption {
            objectName: "shapeNoMatch"
            visible: control.visibleDefinitions.length === 0
            text: qsTr("No shapes match this search.")
        }
    }
}
