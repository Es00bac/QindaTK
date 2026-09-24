// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// A view over the host's document style graph. Style identity, validation,
// selection targets, overrides, transactions and persistence remain host-owned.
Item {
    id: control
    property var styles: [] // [{id,name,kind,parentId,summary,preview,compatible,validationError}]
    property var targetIds: []
    property var directOverrides: [] // [{key,label,value}]
    property string selectedStyleId: ""
    property string filterText: ""
    property string draftName: ""
    property string draftKind: "paragraph"
    property string draftParentId: ""
    property string validationError: ""
    property string tooltip: qsTr("Manage document styles")
    readonly property var selectedStyle: styleById(selectedStyleId)
    readonly property var visibleStyles: filterStyles()
    // The kinds the host's style graph supports (RichTextDocument.styleKinds
    // is paragraph, character and table).
    property var styleKinds: ["character", "paragraph", "table", "object"]
    readonly property bool canApply: selectedStyle !== null && selectedStyle.compatible !== false && targetIds.length > 0
    readonly property bool canUpdate: selectedStyle !== null && !selectedStyle.builtIn && validationError.length === 0

    signal styleSelectionRequested(string styleId)
    signal createStyleRequested(string name, string kind, string parentId)
    signal updateStyleRequested(string styleId, var patch)
    signal applyStyleRequested(string styleId, var targetIds)
    signal resetOverridesRequested(var targetIds, var overrideKeys)

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    function styleById(id) {
        for (let i = 0; i < styles.length; ++i) if (styles[i].id === id) return styles[i]
        return null
    }
    function filterStyles() {
        const query = filterText.trim().toLocaleLowerCase()
        return styles.filter(function(style) {
            return !query || (style.name || "").toLocaleLowerCase().indexOf(query) >= 0
                    || (style.kind || "").toLocaleLowerCase().indexOf(query) >= 0
        })
    }
    function chooseStyle(styleId) {
        const style = styleById(styleId)
        if (!enabled || !style) return
        selectedStyleId = styleId
        styleSelectionRequested(styleId)
        draftName = style.name
        draftKind = style.kind
        draftParentId = style.parentId || ""
    }
    function applySelected() {
        if (canApply && enabled) applyStyleRequested(selectedStyleId, targetIds.slice())
    }
    function createStyle() {
        const name = draftName.trim()
        if (!enabled || !name || styleNameExists(name)) return
        createStyleRequested(name, draftKind, draftParentId)
        draftName = ""
    }
    function styleNameExists(name) {
        const folded = name.trim().toLocaleLowerCase()
        return styles.some(function(style) { return (style.name || "").toLocaleLowerCase() === folded })
    }
    function updateSelected() {
        if (!canUpdate || !enabled) return
        updateStyleRequested(selectedStyleId, { name: draftName.trim(), kind: draftKind, parentId: draftParentId })
    }
    function resetDirectOverrides() {
        if (enabled && directOverrides.length && targetIds.length)
            resetOverridesRequested(targetIds.slice(), directOverrides.map(function(row) { return row.key }))
    }

    Tk.Flex {
        id: column
        width: control.width > 0 ? control.width : implicitWidth
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Overline { title: qsTr("DOCUMENT STYLES"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Caption { text: qsTr("%1 styles").arg(control.styles.length) }
        }
        Tk.TextField {
            objectName: "styleSearch"
            placeholderText: qsTr("Find a style")
            iconName: "search"
            tooltip: qsTr("Search style names and kinds")
            onTextChanged: if (activeFocus) control.filterText = text
        }
        Tk.Scroll {
            objectName: "styleListScroll"
            height: Math.min(Tk.Theme.size.panelMinHeight, listContent.implicitHeight)
            overflowY: Tk.Scroll.Auto
            accessibleName: qsTr("Document styles")
            Tk.Flex {
                id: listContent
                width: parent.width
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Repeater {
                    model: control.visibleStyles
                    delegate: Tk.Button {
                        required property var modelData
                        objectName: "style_" + modelData.id
                        text: modelData.name
                        iconName: modelData.kind === "paragraph" ? "pilcrow"
                                  : modelData.kind === "character" ? "case-sensitive"
                                  : modelData.kind === "table" ? "table" : "square"
                        trailingIconName: modelData.id === control.selectedStyleId ? "check" : ""
                        variant: modelData.id === control.selectedStyleId ? "outline" : "ghost"
                        tooltip: modelData.summary || qsTr("%1 style").arg(modelData.kind)
                        enabled: modelData.available !== false
                        onClicked: control.chooseStyle(modelData.id)
                    }
                }
                Tk.Caption {
                    visible: control.visibleStyles.length === 0
                    text: qsTr("No styles match the search.")
                }
            }
        }
        Tk.Caption {
            objectName: "styleCompatibility"
            visible: control.selectedStyle !== null && control.selectedStyle.compatible === false
            color: Tk.Theme.color.warning
            text: control.selectedStyle ? (control.selectedStyle.compatibilityReason || qsTr("This style cannot be applied to the current selection.")) : ""
            wrapMode: Text.WordWrap
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.TextField {
                id: nameField
                objectName: "styleName"
                text: control.draftName
                placeholderText: qsTr("Style name")
                tooltip: qsTr("Name for the selected or new style")
                error: control.styleNameExists(text) && (!control.selectedStyle || text !== control.selectedStyle.name)
                onTextChanged: control.draftName = text
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.ComboBox {
                objectName: "styleKind"
                model: control.styleKinds
                currentIndex: Math.max(0, control.styleKinds.indexOf(control.draftKind))
                tooltip: qsTr("Style category")
                onActivated: function(index) { control.draftKind = model[index] }
            }
        }
        Tk.ComboBox {
            objectName: "styleParent"
            model: [{ id: "", label: qsTr("No parent") }].concat(control.styles.filter(function(style) {
                return style.id !== control.selectedStyleId && style.kind === control.draftKind
            }).map(function(style) { return { id: style.id, label: style.name } }))
            textRole: "label"
            currentIndex: {
                const entries = model
                for (let i = 0; i < entries.length; ++i) if (entries[i].id === control.draftParentId) return i
                return 0
            }
            tooltip: qsTr("Inherits from this style")
            onActivated: function(index) { control.draftParentId = model[index].id }
        }
        Tk.Caption {
            visible: control.selectedStyle !== null && (control.selectedStyle.preview || "").length > 0
            text: control.selectedStyle ? control.selectedStyle.preview : ""
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        Tk.Caption {
            objectName: "styleValidation"
            visible: control.validationError.length > 0 || (control.selectedStyle !== null && (control.selectedStyle.validationError || "").length > 0)
            color: Tk.Theme.color.danger
            text: control.validationError || (control.selectedStyle && control.selectedStyle.validationError) || ""
            wrapMode: Text.WordWrap
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.Button {
                objectName: "styleCreate"
                text: qsTr("Create")
                iconName: "plus"
                tooltip: qsTr("Create a style with this name, kind and parent")
                enabled: control.draftName.trim().length > 0 && !control.styleNameExists(control.draftName)
                onClicked: control.createStyle()
            }
            Tk.Button {
                objectName: "styleUpdate"
                text: qsTr("Update")
                iconName: "save"
                tooltip: qsTr("Update the selected named style")
                enabled: control.canUpdate
                onClicked: control.updateSelected()
            }
            Tk.Button {
                objectName: "styleApply"
                text: qsTr("Apply")
                variant: "accent"
                tooltip: qsTr("Apply the selected style to the current text selection")
                enabled: control.canApply
                onClicked: control.applySelected()
            }
        }
        Tk.Divider { visible: control.directOverrides.length > 0 }
        Tk.Flex {
            visible: control.directOverrides.length > 0
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Caption {
                text: qsTr("%1 direct formatting override(s) remain on the selection.").arg(control.directOverrides.length)
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: control.directOverrides
                delegate: Tk.KeyValue {
                    required property var modelData
                    key: modelData.label
                    value: String(modelData.value)
                }
            }
            Tk.Button {
                objectName: "styleResetOverrides"
                text: qsTr("Reset direct formatting")
                iconName: "rotate-ccw"
                tooltip: qsTr("Remove these direct overrides from the selected text")
                enabled: control.targetIds.length > 0
                onClicked: control.resetDirectOverrides()
            }
        }
    }
}
