// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// AGENT-CONTRACT (docs/controls.md): the 24px (small 20) single-line field
// on inputBg with a 1px inputBorder; focus paints inputFocusBorder plus
// the focus ring; `error` paints the danger colour. Optional leading
// `iconName`, a `clearable` × button (objectName "fieldClear") and a
// `trailing` slot for buttons or units. `cleared()` fires when the user
// clears the field through the button or Escape.
// AGENT-CONTRACT (D-314): `suggestions` is a host-fed completion list
// (strings or {label, detail, iconName, ...}). While it is non-empty and
// the field has focus a popup (objectName "fieldSuggestions") lists it;
// Up/Down/PageUp/PageDown move `suggestionIndex`, Return/Enter/Tab or a
// click choose, and suggestionChosen(entry) carries the whole entry. The
// field never edits its own text for a choice — the host inserts what the
// entry means (a formula bar replaces the token being typed). Escape closes
// the popup until the suggestions or the text change.
T.TextField {
    id: control

    property string iconName: ""
    property bool clearable: false
    property bool small: false
    property bool error: false
    property bool mono: false
    property string tooltip: ""
    property alias trailing: trailingHost.data
    property var suggestions: []
    property int suggestionIndex: -1
    property int maxVisibleSuggestions: 8
    readonly property int suggestionCount: suggestions ? suggestions.length || 0 : 0
    readonly property bool suggestionsOpen: suggestionCount > 0 && activeFocus && !_suggestionsDismissed
    property bool _suggestionsDismissed: false

    signal cleared()
    signal suggestionChosen(var value)

    readonly property real controlHeight: small ? Tk.Theme.size.controlSm : Tk.Theme.size.control
    readonly property real iconSize: small ? Tk.Theme.size.iconSm : Tk.Theme.size.icon
    readonly property real edgePadding: Tk.Theme.space.sm + 2
    readonly property bool showClear: clearable && control.length > 0 && control.enabled && !control.readOnly

    function clearText() {
        if (control.length === 0 || !control.enabled || control.readOnly) {
            return
        }
        control.clear()
        control.cleared()
    }
    function suggestionAt(index) {
        const entry = index >= 0 && index < suggestionCount ? suggestions[index] : undefined
        return typeof entry === "object" && entry !== null ? entry : ({ label: String(entry ?? "") })
    }
    function chooseSuggestion(index) {
        if (index < 0 || index >= suggestionCount) return false
        const entry = suggestions[index]
        _suggestionsDismissed = true
        suggestionChosen(entry)
        return true
    }
    function closeSuggestions() { _suggestionsDismissed = true }
    // Whether a popup of `height` has more room above the field than below.
    function _suggestionsAbove(height) {
        const overlay = T.Overlay.overlay
        if (overlay === null) return false
        const origin = control.mapToItem(overlay, 0, 0)
        const below = overlay.height - origin.y - control.height - Tk.Theme.space.xs
        const above = origin.y - Tk.Theme.space.xs
        return below < height && above > below
    }
    function moveSuggestion(delta) {
        suggestionIndex = Math.max(0, Math.min(suggestionCount - 1,
                                               (suggestionIndex < 0 ? (delta > 0 ? -1 : suggestionCount) : suggestionIndex) + delta))
    }

    // AGENT-GUARD: read `suggestions` here, not `suggestionCount` — the
    // handler can run before that binding has seen the new list.
    onSuggestionsChanged: {
        _suggestionsDismissed = false
        suggestionIndex = suggestions && suggestions.length > 0 ? 0 : -1
    }
    onTextEdited: _suggestionsDismissed = false

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)
    leftPadding: edgePadding + (iconName.length > 0 ? iconSize + Tk.Theme.space.xs + 2 : 0)
    rightPadding: edgePadding + trailingHost.implicitWidth
                  + (showClear ? clearButton.width + Tk.Theme.space.xs : 0)
    topPadding: 0
    bottomPadding: 0
    verticalAlignment: TextInput.AlignVCenter
    color: control.enabled ? Tk.Theme.color.text : Tk.Theme.color.textDisabled
    selectionColor: Tk.Theme.color.selection
    selectedTextColor: Tk.Theme.color.text
    placeholderTextColor: Tk.Theme.color.textMuted
    font.family: control.mono ? Tk.Theme.font.monoFamily : Tk.Theme.font.family
    font.pixelSize: control.small ? Tk.Theme.font.small : Tk.Theme.font.body
    renderType: Text.NativeRendering
    hoverEnabled: true
    selectByMouse: true
    opacity: enabled ? 1.0 : Tk.Theme.opacity.disabled

    Accessible.role: Accessible.EditableText
    Accessible.name: control.tooltip.length > 0 ? control.tooltip : control.placeholderText
    Accessible.description: control.error && control.readOnly
                            ? qsTr("Error. Read only")
                            : control.error ? qsTr("Error")
                            : control.readOnly ? qsTr("Read only") : ""

    Tk.ToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered && !control.suggestionsOpen
        delay: 600
    }

    Keys.onEscapePressed: function(event) {
        if (control.suggestionsOpen) {
            control.closeSuggestions()
            event.accepted = true
        } else if (control.clearable && control.length > 0 && control.enabled && !control.readOnly) {
            control.clearText()
            event.accepted = true
        } else {
            event.accepted = false
        }
    }
    // AGENT-GUARD: the generic `pressed` handler runs only for keys no
    // specific Keys.on<Key>Pressed handler took, and leaves every key alone
    // while the popup is closed, so existing consumers' key handling (and
    // `accepted` on Return) is unchanged.
    Keys.onPressed: function(event) {
        if (!control.suggestionsOpen) return
        const page = Math.max(1, control.maxVisibleSuggestions - 1)
        if (event.key === Qt.Key_Down) control.moveSuggestion(1)
        else if (event.key === Qt.Key_Up) control.moveSuggestion(-1)
        else if (event.key === Qt.Key_PageDown) control.moveSuggestion(page)
        else if (event.key === Qt.Key_PageUp) control.moveSuggestion(-page)
        else if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Tab)
                 && control.suggestionIndex >= 0) control.chooseSuggestion(control.suggestionIndex)
        else return
        event.accepted = true
    }

    // AGENT-GUARD: interactive parts live as direct children, not inside
    // `background`: T.TextField stacks its background at z -1, so the text
    // input takes every press before a button parented there could.
    Tk.Icon {
        visible: control.iconName.length > 0
        name: control.iconName
        size: control.iconSize
        color: control.activeFocus ? Tk.Theme.color.accentText : Tk.Theme.color.textMuted
        x: control.edgePadding
        y: (control.height - height) / 2
        z: 1
    }
    Tk.IconButton {
        id: clearButton
        objectName: "fieldClear"
        visible: control.showClear
        small: true
        iconName: "x"
        tooltip: qsTr("Clear")
        focusPolicy: Qt.NoFocus
        anchors.right: trailingHost.left
        anchors.rightMargin: Tk.Theme.space.xs
        anchors.verticalCenter: parent.verticalCenter
        z: 1
        onClicked: control.clearText()
    }
    Tk.Flex {
        id: trailingHost
        direction: Tk.Flex.Row
        align: Tk.Flex.Center
        gap: Tk.Theme.space.xs
        anchors.right: parent.right
        anchors.rightMargin: control.edgePadding - Tk.Theme.space.xs
        anchors.verticalCenter: parent.verticalCenter
        width: implicitWidth
        height: implicitHeight
        z: 1
    }

    // AGENT-NOTE: the popup is created only while there are suggestions
    // (most fields never have any) and never takes focus (focus: false;
    // entries are TapHandlers, not focusable delegates): the field keeps
    // typing and the keys above, and losing focus closes it through
    // `suggestionsOpen`. A Loader's non-Item object sits at the field's
    // origin, so x/y below are field coordinates.
    Loader {
        id: suggestionLoader
        active: control.suggestionCount > 0
        sourceComponent: T.Popup {
            id: suggestionPopup
            objectName: "fieldSuggestions"
            readonly property real entryHeight: Tk.Theme.size.menuItem
            readonly property bool above: {
                void control.suggestionsOpen
                return control._suggestionsAbove(implicitHeight)
            }
            visible: control.suggestionsOpen
            focus: false
            modal: false
            dim: false
            closePolicy: T.Popup.NoAutoClose
            x: 0
            y: above ? -height - Tk.Theme.space.xs : control.height + Tk.Theme.space.xs
            width: Math.max(control.width, Tk.Theme.size.menuMinWidth)
            implicitHeight: Math.min(control.suggestionCount, Math.max(1, control.maxVisibleSuggestions)) * entryHeight
                            + topPadding + bottomPadding
            padding: Tk.Theme.space.xs
            background: Rectangle {
                color: Tk.Theme.color.popoverBg
                border.width: Tk.Theme.size.border
                border.color: Tk.Theme.color.popoverBorder
                radius: Tk.Theme.radius.md
            }
            contentItem: ListView {
                id: suggestionList
                objectName: "fieldSuggestionList"
                clip: true
                model: control.suggestionCount
                currentIndex: control.suggestionIndex
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: 0
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Suggestions")
                onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
                T.ScrollBar.vertical: Tk.ScrollBar { }
                delegate: Rectangle {
                    id: entryRow
                    required property int index
                    readonly property var entry: control.suggestionAt(index)
                    readonly property bool current: index === control.suggestionIndex
                    objectName: "fieldSuggestion_" + index
                    width: suggestionList.width
                    height: suggestionPopup.entryHeight
                    radius: Tk.Theme.radius.sm
                    color: current ? Tk.Theme.color.selection : hover.hovered ? Tk.Theme.color.hover : "transparent"
                    Accessible.role: Accessible.ListItem
                    Accessible.name: String(entry.label ?? "")
                    Accessible.description: String(entry.detail ?? "")
                    Accessible.selected: current
                    HoverHandler { id: hover; onHoveredChanged: if (hovered) control.suggestionIndex = entryRow.index }
                    TapHandler { onTapped: control.chooseSuggestion(entryRow.index) }
                    Tk.Flex {
                        anchors.fill: parent
                        anchors.leftMargin: Tk.Theme.space.sm
                        anchors.rightMargin: Tk.Theme.space.sm
                        direction: Tk.Flex.Row
                        align: Tk.Flex.Center
                        gap: Tk.Theme.space.sm
                        Tk.Icon {
                            visible: String(entryRow.entry.iconName ?? "").length > 0
                            name: String(entryRow.entry.iconName ?? "")
                            size: Tk.Theme.size.iconSm
                            color: Tk.Theme.color.textMuted
                        }
                        Tk.Label {
                            objectName: "fieldSuggestionLabel"
                            text: String(entryRow.entry.label ?? "")
                            font.family: control.font.family
                            font.pixelSize: Tk.Theme.font.small
                            elide: Text.ElideRight
                            Tk.Flex.grow: 1
                            Tk.Flex.shrink: 1
                        }
                        Tk.Caption {
                            objectName: "fieldSuggestionDetail"
                            visible: text.length > 0
                            text: String(entryRow.entry.detail ?? "")
                            elide: Text.ElideRight
                            Tk.Flex.shrink: 2
                        }
                    }
                }
            }
        }
    }

    background: Rectangle {
        implicitWidth: Tk.Theme.size.fieldWidth
        implicitHeight: control.controlHeight
        radius: Tk.Theme.radius.sm
        color: Tk.Theme.color.inputBg
        border.width: Tk.Theme.size.border
        border.color: control.error ? Tk.Theme.color.danger
                    : control.activeFocus ? Tk.Theme.color.inputFocusBorder
                    : control.hovered ? Tk.Theme.color.controlHoverBorder : Tk.Theme.color.inputBorder
        Behavior on border.color { ColorAnimation { duration: Tk.Theme.motion.fast } }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: parent.radius + 1
            color: "transparent"
            border.width: Tk.Theme.size.focusRing
            border.color: control.error ? Tk.Theme.alpha(Tk.Theme.color.danger, 0.4) : Tk.Theme.color.focus
            visible: control.activeFocus
        }
        // AGENT-NOTE: placeholder at the small size (11px), one step under
        // body, so it reads as a hint inside a 24px field and still lines up
        // with the typed text.
        Tk.Label {
            visible: !control.length && !control.preeditText
            x: control.leftPadding
            width: Math.max(0, parent.width - control.leftPadding - control.rightPadding)
            height: parent.height
            text: control.placeholderText
            color: control.placeholderTextColor
            font.family: control.font.family
            font.pixelSize: control.small ? Tk.Theme.font.caption : Tk.Theme.font.small
            verticalAlignment: Text.AlignVCenter
        }
    }
}
