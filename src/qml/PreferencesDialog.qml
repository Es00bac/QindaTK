// SPDX-License-Identifier: LGPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
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
    readonly property var filteredSettings: buildFilteredSettings()

    signal settingsApplied()
    signal applyRejected(string message)

    title: qsTr("Preferences")
    subtitle: qsTr("Search settings, review validation, and apply changes together.")
    dialogWidth: Tk.Theme.size.dialogWidth
    primaryText: ""
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
                    && control.settingsModel.valid
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
    function beginChanges() {
        invalidKey = ""
        invalidMessage = ""
        if (settingsModel && !settingsModel.staging) settingsModel.beginStage()
    }
    function cancelChanges() {
        if (settingsModel) settingsModel.cancel()
        close()
    }
    function applyChanges() {
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
        invalidKey = ""
        invalidMessage = ""
        return true
    }
    function resetSetting(scopedKey) {
        if (!settingsModel || !settingsModel.resetSetting(scopedKey)) {
            invalidKey = scopedKey
            invalidMessage = settingsModel ? settingsModel.error : qsTr("Settings are unavailable")
            return false
        }
        invalidKey = ""
        invalidMessage = ""
        return true
    }
    function focusInvalid() {
        if (!invalidKey) return
        const index = filteredSettings.findIndex(function(row) { return row.scopedKey === invalidKey })
        if (index < 0 || !settingsListRef) return
        settingsListRef.positionViewAtIndex(index, ListView.Contain)
        Qt.callLater(function() {
            if (!control || !control.settingsListRef) return
            const row = control.settingsListRef.itemAtIndex(index)
            if (row) row.focusEditor()
        })
    }

    onOpened: beginChanges()
    Connections {
        target: control.settingsModel
        function onValidationFailed(key, message) {
            control.invalidKey = key
            control.invalidMessage = message
            control.focusInvalid()
        }
    }

    Tk.Flex {
        id: preferencesBody
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm
        width: parent ? parent.width : Tk.Theme.size.dialogWidth

        Tk.SearchField {
            id: preferenceSearch
            objectName: "preferencesSearch"
            placeholderText: qsTr("Search settings")
            tooltip: qsTr("Search nested settings by name, key, category, or description")
            onTextEdited: control.searchText = text
            Keys.onEscapePressed: {
                if (text.length > 0) { clearText(); control.searchText = "" }
                else control.cancelChanges()
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            height: Math.min(Tk.Theme.size.dialogWidth, control.filteredSettings.length
                             * Tk.Theme.size.control + Tk.Theme.space.md * 2)
            ListView {
                id: categoryList
                objectName: "preferencesCategories"
                Accessible.role: Accessible.List
                Accessible.name: qsTr("Preference categories")
                model: control.categories
                width: Tk.Theme.size.fieldWidth * 3
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
                            Tk.Flex.shrink: 0
                        }
                        Loader {
                            id: settingEditor
                            objectName: "preferenceEditor"
                            property var setting: settingRow.modelData
                            onLoaded: {
                                item.setting = setting
                                if (control.invalidKey === String(setting.scopedKey)
                                          && item.forceActiveFocus)
                                          item.forceActiveFocus(Qt.TabFocusReason)
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
                            onClicked: control.resetSetting(settingRow.scopedKey)
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
            objectName: "preference_" + String((setting || {}).scopedKey || "").replace(/[^A-Za-z0-9_]/g, "_")
            text: String((setting || {}).stagedValue ?? "")
            tooltip: String((setting || {}).description || (setting || {}).label || "")
            onEditingFinished: if (setting) control.setSetting(String(setting.scopedKey), text)
        }
    }
}
