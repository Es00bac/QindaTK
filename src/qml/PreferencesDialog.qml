// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QindaTK as Tk

// Searchable, staged editor for SettingsModel descriptors. The model owns
// validation, defaults, apply/cancel and restart semantics.
Tk.Dialog {
    id: control
    objectName: "preferencesDialog"
    property var settingsModel: null
    property string searchText: ""
    property string selectedCategory: ""
    property string invalidKey: ""
    property string invalidMessage: ""
    property var settingsListRef: null
    readonly property var categories: buildCategories()
    property var filteredSettings: []
    property string filteredSignature: ""

    signal settingsApplied()
    signal applyRejected(string message)

    title: qsTr("Preferences")
    subtitle: qsTr("Search settings, review validation, and apply changes together.")
    dialogWidth: Tk.Theme.size.dialogWidth
    primaryText: ""
    // AGENT-GUARD: Dialog's default Enter path calls accept() directly.
    // Preferences must validate and apply staged values before closing.
    primaryEnabled: false
    secondaryText: ""
    footer: Tk.Box {
        objectName: "preferencesFooter"
        padding: Tk.Theme.space.md
        borderTop: Tk.Theme.size.border
        borderColor: Tk.Theme.color.divider
        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Badge {
                objectName: "preferencesRestartState"
                visible: !!control.settingsModel && control.settingsModel.needsRestart
                text: qsTr("Restart required")
                variant: "warning"
                tooltip: qsTr("Some applied settings take effect after restarting the application")
                Tk.Flex.grow: 1
            }
            Tk.Spacer { visible: !control.settingsModel || !control.settingsModel.needsRestart }
            Tk.Button {
                objectName: "preferencesCancel"
                text: qsTr("Cancel")
                tooltip: qsTr("Discard staged preference changes")
                onClicked: control.cancelChanges()
            }
            Tk.Button {
                objectName: "preferencesApply"
                text: qsTr("Apply")
                variant: "accent"
                tooltip: qsTr("Validate and apply staged preference changes")
                available: !!control.settingsModel && control.settingsModel.staging
                    && control.settingsModel.valid && !control.invalidKey
                onClicked: control.applyChanges()
            }
        }
    }

    function buildCategories() {
        const result = [{ id: "", label: qsTr("All settings") }]
        if (!settingsModel || !settingsModel.settings) return result
        const seen = {}
        for (let i = 0; i < settingsModel.settings.length; ++i) {
            const row = settingsModel.settings[i]
            const category = String(row.category || String(row.key).split(/[./]/)[0] || qsTr("General"))
            if (seen[category]) continue
            seen[category] = true
            result.push({ id: category, label: category })
        }
        return result
    }
    function buildFilteredSettings() {
        if (!settingsModel || !settingsModel.settings) return []
        const query = searchText.trim().toLocaleLowerCase().split(/\s+/).filter(function(token) {
            return token.length > 0
        })
        const output = []
        for (let i = 0; i < settingsModel.settings.length; ++i) {
            const row = settingsModel.settings[i]
            const category = String(row.category || String(row.key).split(/[./]/)[0] || qsTr("General"))
            if (selectedCategory.length > 0 && category !== selectedCategory) continue
            const searchable = [row.label, row.key, row.scopedKey, row.description, category]
                .join(" ").toLocaleLowerCase()
            let matches = true
            for (let token = 0; token < query.length; ++token)
                if (searchable.indexOf(query[token]) < 0) { matches = false; break }
            if (!matches) continue
            output.push(row)
        }
        return output
    }
    function refreshFilteredSettings() {
        const next = buildFilteredSettings()
        const signature = JSON.stringify(next)
        if (signature === filteredSignature) return
        filteredSignature = signature
        filteredSettings = next
    }
    onSettingsModelChanged: refreshFilteredSettings()
    onSearchTextChanged: refreshFilteredSettings()
    onSelectedCategoryChanged: refreshFilteredSettings()
    function beginChanges() {
        invalidKey = ""
        invalidMessage = ""
        if (settingsModel && !settingsModel.staging) settingsModel.beginStage()
    }
    function discardStage() {
        if (settingsModel && settingsModel.staging) settingsModel.cancel()
        invalidKey = ""
        invalidMessage = ""
        searchText = ""
        selectedCategory = ""
        preferenceSearch.text = ""
    }
    function cancelChanges() {
        discardStage()
        close()
    }
    function focusedEditor() {
        const window = preferencesBody.Window.window
        let item = window ? window.activeFocusItem : null
        while (item && item !== preferencesBody) {
            if (item.setting && String(item.objectName || "").startsWith("preference_")) return item
            item = item.parent
        }
        return null
    }
    function focusInBody() {
        const window = preferencesBody.Window.window
        let item = window ? window.activeFocusItem : null
        while (item) {
            if (item === preferencesBody) return true
            item = item.parent
        }
        return false
    }
    function submitChanges() {
        const editor = focusedEditor()
        if (editor && editor.applyInput) {
            if (invalidKey === String(editor.setting.scopedKey)) {
                invalidKey = ""
                invalidMessage = ""
            }
            editor.applyInput()
            if (editor.inputInvalid) {
                invalidKey = String(editor.setting.scopedKey)
                invalidMessage = qsTr("Enter a valid number")
                focusInvalid()
                return false
            }
            if (invalidKey === String(editor.setting.scopedKey)) return false
        } else if (editor && editor.preferenceTextEditor) {
            if (!setSetting(String(editor.setting.scopedKey), editor.text)) return false
        }
        return applyChanges()
    }
    function applyChanges() {
        if (invalidKey) {
            applyRejected(invalidMessage)
            focusInvalid()
            return false
        }
        if (!settingsModel || !settingsModel.apply()) {
            invalidMessage = settingsModel ? settingsModel.error : qsTr("Settings are unavailable")
            applyRejected(invalidMessage)
            focusInvalid()
            return false
        }
        invalidKey = ""
        invalidMessage = ""
        settingsApplied()
        close()
        return true
    }
    function setSetting(scopedKey, value) {
        if (!settingsModel || !settingsModel.setValue(scopedKey, value)) {
            invalidKey = scopedKey
            invalidMessage = settingsModel ? settingsModel.error : qsTr("Settings are unavailable")
            return false
        }
        if (invalidKey === scopedKey) { invalidKey = ""; invalidMessage = "" }
        return true
    }
    function resetSetting(scopedKey) {
        if (!settingsModel || !settingsModel.resetSetting(scopedKey)) {
            invalidKey = scopedKey
            invalidMessage = settingsModel ? settingsModel.error : qsTr("Settings are unavailable")
            return false
        }
        if (invalidKey === scopedKey) { invalidKey = ""; invalidMessage = "" }
        return true
    }
    function focusInvalid() {
        if (!invalidKey || !settingsModel || !settingsModel.settings) return
        const key = invalidKey
        if (selectedCategory.length > 0 || searchText.length > 0) {
            selectedCategory = ""
            searchText = ""
            preferenceSearch.text = ""
        }
        Qt.callLater(function() {
            if (!control || !control.visible || !control.settingsListRef || control.invalidKey !== key) return
            const index = control.filteredSettings.findIndex(function(row) { return row.scopedKey === key })
            if (index < 0) return
            control.settingsListRef.forceLayout()
            control.settingsListRef.positionViewAtIndex(index, ListView.Contain)
            Qt.callLater(function() {
                if (!control || !control.visible || !control.settingsListRef || control.invalidKey !== key) return
                const row = control.settingsListRef.itemAtIndex(index)
                if (row) row.focusEditor()
            })
        })
    }

    onOpened: {
        beginChanges()
        preferenceSearch.forceActiveFocus(Qt.TabFocusReason)
    }
    onAboutToHide: discardStage()
    Component.onDestruction: if (settingsModel && settingsModel.staging) settingsModel.cancel()
    Connections {
        target: control.settingsModel
        ignoreUnknownSignals: true
        function onStateChanged() {
            // AGENT-GUARD: SettingsModel emits stateChanged while an editor
            // commits. Refresh after its handler returns, and keep delegates
            // when only validation state changed, or focus can reenter commit.
            Qt.callLater(control.refreshFilteredSettings)
        }
        function onValidationFailed(key, message) {
            control.invalidKey = key
            control.invalidMessage = message
            Qt.callLater(function() { control.focusInvalid() })
        }
    }

    Tk.Flex {
        id: preferencesBody
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        width: parent ? parent.width : Tk.Theme.size.dialogWidth

        Shortcut {
            sequence: "Return"
            context: Qt.WindowShortcut
            enabled: control.visible && control.focusInBody()
            onActivated: control.submitChanges()
        }
        Shortcut {
            sequence: "Enter"
            context: Qt.WindowShortcut
            enabled: control.visible && control.focusInBody()
            onActivated: control.submitChanges()
        }

        Tk.SearchField {
            id: preferenceSearch
            objectName: "preferencesSearch"
            placeholderText: qsTr("Search settings")
            tooltip: qsTr("Search nested settings by name, key, category, or description")
            onTextEdited: control.searchText = text
            Keys.onEscapePressed: control.cancelChanges()
        }
        // AGENT-GUARD: Scroll measures its single child's implicit height.
        // Give the two virtual lists a sized wrapper so the dialog body does
        // not collapse to the search field while rows remain clipped below it.
        Item {
            width: parent.width
            implicitHeight: Math.min(Tk.Theme.size.dialogWidth,
                                     Math.max(Tk.Theme.size.fieldWidth, control.filteredSettings.length
                                              * Tk.Theme.size.control + Tk.Theme.space.md * 2))
            height: implicitHeight
            Tk.Flex {
                anchors.fill: parent
                direction: Tk.Flex.Row
                gap: Tk.Theme.space.md
                ListView {
                    id: categoryList
                    objectName: "preferencesCategories"
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Preference categories")
                    model: control.categories
                    Tk.Flex.basis: Tk.Theme.size.control * 5
                    Tk.Flex.shrink: 0
                    clip: true
                    delegate: Tk.Button {
                        required property var modelData
                        objectName: "preferenceCategory_" + (modelData.id || "all")
                        width: categoryList.width
                        text: modelData.label
                        tooltip: text
                        variant: control.selectedCategory === modelData.id ? "outline" : "ghost"
                        onClicked: control.selectedCategory = modelData.id
                    }
                }
                ListView {
                    id: settingsList
                    objectName: "preferencesSettings"
                    Accessible.role: Accessible.List
                    Accessible.name: qsTr("Settings")
                    model: control.filteredSettings
                    Component.onCompleted: control.settingsListRef = settingsList
                    clip: true
                    spacing: Tk.Theme.space.xs
                    Tk.Flex.grow: 1
                    delegate: Tk.Box {
                        id: settingRow
                        required property var modelData
                        readonly property string scopedKey: String(modelData.scopedKey || "")
                        readonly property string settingLabel: String(modelData.label || modelData.key || scopedKey)
                        objectName: "preferenceRow_" + scopedKey.replace(/[^A-Za-z0-9_]/g, "_")
                        width: settingsList.width
                        padding: Tk.Theme.space.sm
                        color: Tk.Theme.color.surface
                        borderColor: control.invalidKey === scopedKey ? Tk.Theme.color.danger : Tk.Theme.color.divider
                        Accessible.role: Accessible.ListItem
                        Accessible.name: settingLabel
                        function focusEditor() {
                            if (settingEditor.item && settingEditor.item.forceActiveFocus)
                                settingEditor.item.forceActiveFocus(Qt.TabFocusReason)
                        }

                        Tk.Flex {
                            direction: Tk.Flex.Row
                            align: Tk.Flex.Center
                            gap: Tk.Theme.space.md
                            width: parent.width
                            Tk.Flex {
                                direction: Tk.Flex.Column
                                gap: Tk.Theme.space.xs
                                Tk.Flex.basis: 0
                                Tk.Flex.grow: 1
                                Tk.Flex.minWidth: Tk.Theme.size.control * 3
                                Tk.Label { text: settingRow.settingLabel; Tk.Flex.grow: 1 }
                                Tk.Caption {
                                    visible: String(settingRow.modelData.description || "").length > 0
                                    text: String(settingRow.modelData.description || "")
                                    wrapMode: Text.Wrap
                                    elide: Text.ElideNone
                                }
                                Tk.Caption {
                                    visible: control.invalidKey === settingRow.scopedKey
                                    text: control.invalidKey === settingRow.scopedKey ? control.invalidMessage : ""
                                    color: Tk.Theme.color.danger
                                    wrapMode: Text.Wrap
                                }
                            }
                            Loader {
                                id: settingEditor
                                objectName: "preferenceEditor"
                                property var setting: settingRow.modelData
                                Tk.Flex.basis: Tk.Theme.size.control * 4
                                Tk.Flex.shrink: 0
                                onLoaded: {
                                    item.setting = setting
                                }
                                sourceComponent: {
                                    const type = String(setting.valueType || "").toLowerCase()
                                    if (type === "bool" || type === "boolean") return booleanEditor
                                    if (setting.options && setting.options.length > 0) return choiceEditor
                                    if (["int", "uint", "longlong", "qlonglong", "qulonglong",
                                         "double", "float", "short"].indexOf(type) >= 0)
                                        return numberEditor
                                    return textEditor
                                }
                            }
                            Tk.Button {
                                objectName: "preferenceReset_" + settingRow.scopedKey.replace(/[^A-Za-z0-9_]/g, "_")
                                text: qsTr("Default")
                                variant: "ghost"
                                tooltip: qsTr("Restore the default value for %1").arg(settingRow.settingLabel)
                                Tk.Flex.shrink: 0
                                onClicked: control.resetSetting(settingRow.scopedKey)
                            }
                        }
                    }
                }
            }
        }
        Tk.EmptyState {
            objectName: "preferencesEmpty"
            visible: control.filteredSettings.length === 0
            title: qsTr("No matching settings")
            text: qsTr("Change the search or category filter.")
        }
        Tk.Label {
            objectName: "preferencesError"
            visible: !!control.settingsModel && !control.settingsModel.valid
            text: control.settingsModel ? control.settingsModel.error : ""
            color: Tk.Theme.color.danger
            wrapMode: Text.Wrap
        }
    }

    Component {
        id: booleanEditor
        Tk.CheckBox {
            property var setting: null
            objectName: "preference_" + String((setting || {}).scopedKey || "").replace(/[^A-Za-z0-9_]/g, "_")
            text: checked ? qsTr("On") : qsTr("Off")
            checked: !!(setting || {}).stagedValue
            tooltip: String((setting || {}).description || (setting || {}).label || "")
            onToggled: if (setting) control.setSetting(String(setting.scopedKey), checked)
        }
    }
    Component {
        id: choiceEditor
        Tk.ComboBox {
            property var setting: null
            objectName: "preference_" + String((setting || {}).scopedKey || "").replace(/[^A-Za-z0-9_]/g, "_")
            model: setting ? setting.options : []
            textRole: "label"
            currentIndex: {
                const options = setting ? setting.options || [] : []
                const value = setting ? setting.stagedValue : undefined
                return options.findIndex(function(option) {
                    return (option && option.value !== undefined ? option.value : option) === value
                })
            }
            tooltip: String((setting || {}).description || (setting || {}).label || "")
            onActivated: function(index) {
                if (!setting) return
                const option = setting.options[index]
                control.setSetting(String(setting.scopedKey),
                                   option && option.value !== undefined ? option.value : option)
            }
        }
    }
    Component {
        id: numberEditor
        Tk.NumberField {
            property var setting: null
            objectName: "preference_" + String((setting || {}).scopedKey || "").replace(/[^A-Za-z0-9_]/g, "_")
            value: Number((setting || {}).stagedValue || 0)
            decimals: ["double", "float"].indexOf(String((setting || {}).valueType || "").toLowerCase()) >= 0 ? 3 : 0
            tooltip: String((setting || {}).description || (setting || {}).label || "")
            onValueModified: function(next) { if (setting) control.setSetting(String(setting.scopedKey), next) }
        }
    }
    Component {
        id: textEditor
        Tk.TextField {
            property var setting: null
            property bool preferenceTextEditor: true
            objectName: "preference_" + String((setting || {}).scopedKey || "").replace(/[^A-Za-z0-9_]/g, "_")
            text: String((setting || {}).stagedValue ?? "")
            tooltip: String((setting || {}).description || (setting || {}).label || "")
            onEditingFinished: if (setting) control.setSetting(String(setting.scopedKey), text)
        }
    }
}
