// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: root
    width: 1040
    height: 720
    property var settingsModel: settingsFixture
    property var commandRegistry: commandFixture
    property var shortcutMap: shortcutsFixture

    QtObject {
        id: settingsFixture
        property var settings: [
            { scopedKey: "app:appearance.theme", key: "appearance.theme", scope: "app", instanceId: "",
              defaultValue: "Sloom Dark", valueType: "QString", label: "Theme", category: "Appearance",
              description: "Choose the application theme.", restartRequired: false,
              stagedValue: "Sloom Dark", currentValue: "Sloom Dark", options: ["Sloom Dark", "Sloom Light"] },
            { scopedKey: "app:audio.buffer", key: "audio.buffer", scope: "app", instanceId: "",
              defaultValue: 256, valueType: "int", label: "Audio buffer", category: "Audio",
              description: "Frames per audio buffer.", restartRequired: true,
              stagedValue: 256, currentValue: 256 }
        ]
        property bool staging: false
        property bool needsRestart: false
        property bool valid: true
        property string error: ""
        signal validationFailed(string key, string message)
        function beginStage() { staging = true; return true }
        function setValue(key, value) {
            for (let i = 0; i < settings.length; ++i) {
                if (settings[i].scopedKey !== key) continue
                let rows = settings.slice()
                rows[i] = Object.assign({}, rows[i], { stagedValue: value })
                settings = rows
                return true
            }
            return false
        }
        function resetSetting(key) {
            for (let i = 0; i < settings.length; ++i)
                if (settings[i].scopedKey === key) return setValue(key, settings[i].defaultValue)
            return false
        }
        function apply() { staging = false; needsRestart = true; return true }
        function cancel() { staging = false }
    }
    QtObject {
        id: commandFixture
        property int generation: 1
        function paletteCommands() {
            return [
                { id: "file.save", label: "Save", section: "File", keywords: "document", shortcut: "Ctrl+S" },
                { id: "edit.find", label: "Find", section: "Edit", keywords: "search", shortcut: "Ctrl+F" }
            ]
        }
    }
    QtObject {
        id: shortcutsFixture
        property int chordTimeoutMs: Tk.Theme.motion.tooltipTimeout
        signal changed()
        function overrides() { return ({}) }
        function validateOverrides(values) { return { ok: true, conflicts: [], error: "" } }
        function loadOverrides(values) { changed(); return true }
    }

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Heading { text: "Settings and text workflows"; level: 2 }
        Tk.Caption { text: "Staged preferences, keyboard bindings, and revision-aware find/replace" }
        Tk.Flex {
            direction: Tk.Flex.Row
            width: parent.width
            gap: Tk.Theme.space.md
            Tk.Box {
                objectName: "shortcutPreview"
                width: root.width * 0.43
                Tk.ShortcutEditor {
                    anchors.fill: parent
                    commandRegistry: root.commandRegistry
                    shortcutMap: root.shortcutMap
                    settingsModel: null
                }
            }
            Tk.Box {
                objectName: "textWorkflowPreview"
                width: root.width * 0.53
                Tk.Flex {
                    direction: Tk.Flex.Column
                    gap: Tk.Theme.space.sm
                    width: parent.width
                    TextEdit {
                        id: sampleDocument
                        objectName: "sampleDocument"
                        text: "A compact editor can search and replace this sample text."
                        wrapMode: TextEdit.Wrap
                        height: Tk.Theme.size.fieldHeight * 3
                    }
                    Tk.FindReplaceService {
                        id: findService
                        textDocument: sampleDocument.textDocument
                    }
                    Tk.FindReplaceBar {
                        objectName: "sampleFindReplace"
                        findReplaceService: findService
                        width: parent.width
                    }
                }
            }
        }
        Tk.Button {
            text: "Open preferences"
            tooltip: "Open the staged preferences dialog"
            onClicked: preferences.open()
        }
    }
    Tk.PreferencesDialog {
        id: preferences
        settingsModel: root.settingsModel
    }
}
