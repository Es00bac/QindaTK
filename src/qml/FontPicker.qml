// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtQuick.Templates as T
import QindaTK as Tk

// Provider metadata is authoritative. The specimen makes fallback explicit;
// no coverage or feature capability is inferred from the rendered glyphs.
Item {
    id: picker
    property Tk.PropertyBinding binding: null
    property Tk.PropertyBinding styleBinding: null
    property var fontProvider: null
    property string family: binding && binding.value !== undefined ? String(binding.value) : ""
    property string style: ""
    property string searchText: ""
    property string tooltip: ""
    property string specimenText: qsTr("Aa Bb Cc 0123 fi ffi")
    property bool includeMissingSelection: true
    readonly property var familyEntries: fontProvider && fontProvider.families ? fontProvider.families : []
    readonly property var filteredFamilies: filterFamilies(searchText)
    readonly property var selectedEntry: entryFor(family)
    readonly property var styleChoices: selectedEntry && selectedEntry.styles ? selectedEntry.styles : []
    readonly property bool selectedMissing: family.length > 0 && (!selectedEntry || selectedEntry.installed === false || selectedEntry.missing === true)
    readonly property string fallbackFamily: selectedEntry && selectedEntry.fallbackFamily ? selectedEntry.fallbackFamily
        : fontProvider && fontProvider.fallbackFamily ? fontProvider.fallbackFamily : ""
    readonly property bool metadataAvailable: !!fontProvider && familyEntries.length > 0
    readonly property var selectedFaceMetadata: fontProvider && fontProvider.faceMetadata && !selectedMissing
        ? fontProvider.faceMetadata(family, style) : ({})

    signal fontSelectionRequested(string family, string style)

    implicitWidth: column.implicitWidth
    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip || qsTr("Font")
    Accessible.description: selectedMissing ? qsTr("Font unavailable. The selected family name is preserved.") : ""

    function entryFor(name) {
        for (let i = 0; i < familyEntries.length; ++i) {
            const entry = familyEntries[i]
            if (String(entry.family || entry.name || entry) === name) return typeof entry === "string" ? { family: entry, installed: true } : entry
        }
        return null
    }
    function filterFamilies(query) {
        const term = String(query || "").trim().toLocaleLowerCase()
        const result = []
        const candidates = fontProvider && typeof fontProvider.search === "function"
                ? fontProvider.search(term, 500) : familyEntries
        for (let i = 0; i < candidates.length; ++i) {
            const entry = candidates[i]
            const name = String(entry.family || entry.name || entry)
            if (!term || name.toLocaleLowerCase().includes(term)) result.push(name)
        }
        if (includeMissingSelection && selectedMissing && result.indexOf(family) < 0
                && (!term || family.toLocaleLowerCase().includes(term))) result.unshift(family)
        return result
    }
    function requestSelection(nextFamily, nextStyle) {
        if (!fontProvider || !binding || !binding.writable) return false
        const entry = entryFor(nextFamily)
        if (!entry) return false
        const chosenStyle = nextStyle || (entry.styles && entry.styles.length ? String(entry.styles[0]) : "")
        const changeStyle = styleBinding && String(styleBinding.value) !== chosenStyle
        const undo = binding.undoService
        if (changeStyle && (!undo || undo !== styleBinding.undoService || !undo.beginMacro("Choose font"))) return false
        if (!applyBinding(binding, nextFamily)) { if (changeStyle) undo.cancelMacro(); return false }
        if (changeStyle && !applyBinding(styleBinding, chosenStyle)) { undo.cancelMacro(); return false }
        if (changeStyle && !undo.endMacro()) { undo.cancelMacro(); return false }
        style = chosenStyle
        fontSelectionRequested(nextFamily, style)
        return true
    }
    function requestStyle(nextStyle) {
        if (!applyBinding(styleBinding, nextStyle)) return false
        style = nextStyle
        fontSelectionRequested(family, style)
        return true
    }
    function applyBinding(target, value) {
        if (!target || !target.writable || !target.begin()) return false
        if (!target.update(value)) { target.cancel(); return false }
        return target.commit()
    }

    onFamilyChanged: if (!familyEntries.length) style = ""
    Component.onCompleted: if (styleBinding && styleBinding.value !== undefined) style = String(styleBinding.value)
    Connections {
        target: picker.styleBinding
        function onChanged() { if (picker.styleBinding && picker.styleBinding.value !== undefined) picker.style = String(picker.styleBinding.value) }
    }

    Tk.ToolTip { text: picker.tooltip; visible: text.length > 0 && familyBox.hovered }
    Tk.Flex {
        id: column
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs
        Tk.TextField {
            id: search
            objectName: "fontSearch"
            placeholderText: qsTr("Search fonts")
            tooltip: picker.tooltip || qsTr("Search available fonts")
            text: picker.searchText
            onTextEdited: picker.searchText = text
        }
        Tk.ComboBox {
            id: familyBox
            objectName: "fontFamilyPicker"
            model: picker.filteredFamilies
            currentIndex: picker.filteredFamilies.indexOf(picker.family)
            tooltip: picker.tooltip || qsTr("Font family")
            enabled: picker.metadataAvailable && picker.binding && picker.binding.writable
            onActivated: function(index) { picker.requestSelection(picker.filteredFamilies[index], picker.style) }
        }
        Tk.ComboBox {
            id: styleBox
            objectName: "fontStylePicker"
            model: picker.styleChoices
            currentIndex: styleChoices.indexOf(picker.style)
            tooltip: qsTr("Font style")
            enabled: familyBox.enabled && styleChoices.length > 0 && !picker.selectedMissing && picker.styleBinding && picker.styleBinding.writable
            onActivated: function(index) { picker.requestStyle(String(styleChoices[index])) }
        }
        Tk.Box {
            objectName: "fontSpecimen"
            width: familyBox.width
            padding: Tk.Theme.space.sm
            color: Tk.Theme.color.panelAlt
            borderColor: Tk.Theme.color.divider
            Tk.Label {
                objectName: "fontSpecimenText"
                text: picker.specimenText
                font.family: picker.selectedMissing
                    ? (picker.fallbackFamily || Tk.Theme.font.family)
                    : (picker.family || Tk.Theme.font.family)
                font.styleName: picker.selectedMissing ? "" : picker.style
                font.pixelSize: Tk.Theme.font.title
                elide: Text.ElideRight
                Accessible.name: qsTr("Font specimen: %1").arg(text)
            }
        }
        Tk.Caption {
            objectName: "fontAvailabilityState"
            visible: picker.selectedMissing || !picker.metadataAvailable
            text: picker.selectedMissing
                ? picker.fallbackFamily.length ? qsTr("Unavailable · using %1 fallback").arg(picker.fallbackFamily) : qsTr("Font unavailable; family name preserved")
                : qsTr("Font metadata unavailable")
            color: picker.selectedMissing ? Tk.Theme.color.warning : Tk.Theme.color.textMuted
            wrapMode: Text.Wrap
        }
    }
}
