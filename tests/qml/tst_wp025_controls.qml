// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "PreferenceShortcutFindReplace"
    when: windowShown
    visible: true
    width: 920
    height: 760

    QtObject {
        id: settingsFixture
        property var settings: []
        property var values: ({})
        property var staged: ({})
        property bool staging: false
        property bool needsRestart: false
        property bool valid: true
        property string error: ""
        property string rejectKey: ""
        signal stateChanged()
        signal validationFailed(string key, string message)
        signal applied()
        function reset() {
            values = ({ "app:audio.buffer": 256, "app:ui.theme": "dark",
                        "app:shortcuts.overrides": "{}" })
            staged = Object.assign({}, values)
            staging = false; needsRestart = false; valid = true
            error = ""; rejectKey = ""
            rebuild()
        }
        function rebuild() {
            settings = [
                { scopedKey: "app:audio.buffer", key: "audio.buffer", scope: "app", instanceId: "",
                  defaultValue: 256, valueType: "int", label: "Buffer size", category: "Audio",
                  description: "Frames per buffer", restartRequired: true,
                  currentValue: values["app:audio.buffer"], stagedValue: staged["app:audio.buffer"] },
                { scopedKey: "app:ui.theme", key: "ui.theme", scope: "app", instanceId: "",
                  defaultValue: "dark", valueType: "QString", label: "Theme", category: "Appearance",
                  description: "Application theme", restartRequired: false,
                  currentValue: values["app:ui.theme"], stagedValue: staged["app:ui.theme"] },
                { scopedKey: "app:shortcuts.overrides", key: "shortcuts.overrides", scope: "app", instanceId: "",
                  defaultValue: "{}", valueType: "QString", label: "Shortcut overrides", category: "Input",
                  description: "Persisted command shortcut overrides", restartRequired: false,
                  currentValue: values["app:shortcuts.overrides"], stagedValue: staged["app:shortcuts.overrides"] }
            ]
        }
        function beginStage() {
            if (staging) return false
            staged = Object.assign({}, values); staging = true; error = ""
            rebuild()
            stateChanged()
            return true
        }
        function setValue(key, value) {
            if (!staging || values[key] === undefined || rejectKey === key) {
                error = rejectKey === key ? "Rejected by validator" : "Unknown setting or inactive stage"
                valid = false
                validationFailed(key, error)
                stateChanged()
                return false
            }
            const next = Object.assign({}, staged)
            next[key] = value
            staged = next; valid = true; error = ""
            rebuild()
            stateChanged()
            return true
        }
        function resetSetting(key) {
            const defaults = { "app:audio.buffer": 256, "app:ui.theme": "dark",
                               "app:shortcuts.overrides": "{}" }
            return setValue(key, defaults[key])
        }
        function valueForKey(key) { return values[key] }
        function apply() {
            if (!staging || !valid) return false
            values = Object.assign({}, staged)
            staging = false; needsRestart = values["app:audio.buffer"] !== 256
            rebuild()
            stateChanged()
            applied()
            return true
        }
        function cancel() {
            staged = Object.assign({}, values)
            staging = false; valid = true; error = ""
            rebuild()
            stateChanged()
        }
        Component.onCompleted: reset()
    }

    QtObject {
        id: commandRegistryFixture
        property int generation: 1
        property var commands: [
            { id: "edit.save", label: "Save", section: "File", keywords: "document", shortcut: "Ctrl+S" },
            { id: "transport.play", label: "Play", section: "Transport", keywords: "audio", shortcut: "Ctrl+P" },
            { id: "edit.copy", label: "Copy", section: "Edit", keywords: "clipboard", shortcut: "Ctrl+C" }
        ]
        function paletteCommands() { return commands }
    }
    QtObject {
        id: shortcutMapFixture
        property int chordTimeoutMs: 80
        property var entries: [
            { commandId: "edit.save", scopeToken: "global" },
            { commandId: "transport.play", scopeToken: "global" },
            { commandId: "edit.copy", scopeToken: "global" }
        ]
        property var overrideValues: ({})
        signal changed()
        function overrides() { return overrideValues }
        function displaySequence(id) {
            if (overrideValues[id] !== undefined) return overrideValues[id]
            for (let i = 0; i < commandRegistryFixture.commands.length; ++i)
                if (commandRegistryFixture.commands[i].id === id) return commandRegistryFixture.commands[i].shortcut
            return ""
        }
        function validateOverrides(values) {
            const conflicts = []
            const sequences = {}
            for (let i = 0; i < commandRegistryFixture.commands.length; ++i) {
                const command = commandRegistryFixture.commands[i]
                const value = values[command.id] === undefined ? command.shortcut : values[command.id]
                if (!value) continue
                if (sequences[value]) conflicts.push({ first: sequences[value], second: command.id, sequence: value })
                else sequences[value] = command.id
            }
            return { ok: conflicts.length === 0, conflicts: conflicts,
                     error: conflicts.length ? "Conflicting shortcuts" : "" }
        }
        function loadOverrides(values) {
            if (!validateOverrides(values).ok) return false
            overrideValues = Object.assign({}, values)
            changed()
            return true
        }
        function reset() { overrideValues = ({}); changed() }
    }

    QtObject {
        id: findServiceFixture
        property string text: "cat cat"
        property int revision: 10
        property bool readOnly: false
        property bool composing: false
        property bool busy: false
        property var results: []
        property int matchCount: 0
        property int currentMatch: -1
        property string lastError: ""
        property string lastRequestId: ""
        property var lastOptions: ({})
        property var lastScope: ({})
        property var cancelled: []
        property int requestSerial: 0
        property int undoGroups: 0
        signal stateChanged()
        signal resultsReady(string requestId, int revision, var results, int currentMatch, string error)
        signal operationFinished(string requestId, var result)
        function reset() {
            text = "cat cat"; revision = 10; readOnly = false; composing = false; busy = false
            results = []; matchCount = 0; currentMatch = -1; lastError = ""
            lastRequestId = ""; lastOptions = ({}); lastScope = ({}); cancelled = []
            requestSerial = 0; undoGroups = 0
        }
        function search(query, options, scope) {
            ++requestSerial
            lastRequestId = "find." + requestSerial
            lastOptions = Object.assign({}, options)
            lastScope = Object.assign({}, scope)
            busy = true
            stateChanged()
            return lastRequestId
        }
        function cancelSearch(id) {
            const next = cancelled.slice()
            next.push(id)
            cancelled = next
            if (id === lastRequestId) busy = false
            stateChanged()
            return true
        }
        function complete(id, atRevision, matches, selected) {
            results = matches
            matchCount = matches.length
            currentMatch = selected === undefined ? (matches.length ? 0 : -1) : selected
            busy = false
            stateChanged()
            resultsReady(id, atRevision, matches, currentMatch, "")
        }
        function setCurrentMatch(index) {
            if (index < 0 || index >= matchCount) return false
            currentMatch = index
            stateChanged()
            return true
        }
        function replaceCurrent(expectedRevision, replacement) {
            if (expectedRevision !== revision) return { ok: false, error: "Stale revision" }
            if (readOnly || composing || currentMatch < 0) return { ok: false, error: "Unavailable" }
            const match = results[currentMatch]
            text = text.slice(0, match.start) + replacement + text.slice(match.start + match.length)
            ++revision; ++undoGroups
            stateChanged()
            return { ok: true, replaced: 1 }
        }
        function replaceAll(expectedRevision, replacement) {
            if (expectedRevision !== revision) return { ok: false, error: "Stale revision" }
            if (readOnly || composing) return { ok: false, error: "Unavailable" }
            let next = text
            const ordered = results.slice().sort(function(a, b) { return b.start - a.start })
            for (let i = 0; i < ordered.length; ++i)
                next = next.slice(0, ordered[i].start) + replacement + next.slice(ordered[i].start + ordered[i].length)
            text = next
            const count = ordered.length
            ++revision; ++undoGroups; results = []; matchCount = 0; currentMatch = -1
            stateChanged()
            return { ok: true, replaced: count }
        }
    }

    Component { id: preferencesComponent; Tk.PreferencesDialog { settingsModel: settingsFixture } }
    Component {
        id: shortcutComponent
        Item {
            property var commandRegistry: commandRegistryFixture
            property var shortcutMap: shortcutMapFixture
            property var settingsModel: settingsFixture
            width: 800
            height: 600
            Tk.ShortcutEditor {
                id: editor
                objectName: "shortcutEditor"
                anchors.fill: parent
                commandRegistry: parent.commandRegistry
                shortcutMap: parent.shortcutMap
                settingsModel: parent.settingsModel
            }
        }
    }
    Component {
        id: findComponent
        Item {
            width: 880
            height: 200
            Tk.FindReplaceBar {
                id: bar
                objectName: "findReplaceBar"
                anchors.fill: parent
                findReplaceService: findServiceFixture
                selectionStart: 2
                selectionLength: 3
            }
        }
    }
    Component {
        id: realFindComponent
        Item {
            width: 720
            height: 220
            TextEdit { id: realDocument; objectName: "realTextDocument"; text: "cat cat" }
            Tk.FindReplaceService {
                id: realService
                objectName: "realFindReplaceService"
                textDocument: realDocument.textDocument
            }
            Tk.FindReplaceBar {
                objectName: "realFindReplaceBar"
                findReplaceService: realService
                width: parent.width
            }
        }
    }
    Component {
        id: signalSpyComponent
        SignalSpy {}
    }

    function init() {
        settingsFixture.reset()
        shortcutMapFixture.reset()
        findServiceFixture.reset()
    }

    function test_preferences_search_stage_apply_and_cancel() {
        const dialog = createTemporaryObject(preferencesComponent, root)
        verify(dialog)
        dialog.open()
        tryVerify(function() { return dialog.opened })
        const list = findChild(dialog, "preferencesSettings")
        verify(list)
        compare(list.count, 3)
        dialog.searchText = "buffer frames"
        compare(dialog.filteredSettings.length, 1)
        compare(dialog.filteredSettings[0].scopedKey, "app:audio.buffer")
        verify(dialog.setSetting("app:audio.buffer", 512))
        compare(settingsFixture.settings[0].stagedValue, 512)
        dialog.cancelChanges()
        compare(settingsFixture.settings[0].stagedValue, 256)
        compare(settingsFixture.staging, false)
        dialog.open()
        tryVerify(function() { return dialog.opened })
        verify(dialog.setSetting("app:audio.buffer", 512))
        verify(dialog.applyChanges())
        compare(settingsFixture.values["app:audio.buffer"], 512)
        compare(settingsFixture.needsRestart, true)
    }

    function test_preferences_validation_surfaces_error_and_keeps_stage_open() {
        const dialog = createTemporaryObject(preferencesComponent, root)
        verify(dialog)
        dialog.open()
        settingsFixture.rejectKey = "app:ui.theme"
        verify(!dialog.setSetting("app:ui.theme", "light"))
        compare(dialog.invalidKey, "app:ui.theme")
        verify(dialog.invalidMessage.length > 0)
        verify(!dialog.applyChanges())
        compare(dialog.visible, true)
        dialog.cancelChanges()
    }

    function test_real_settings_and_shortcut_adapters_stage_and_apply() {
        const fixture = preferencesFixtures.create()
        tryVerify(function() { return fixture && fixture.ready }, 3000)
        const dialog = createTemporaryObject(preferencesComponent, root,
                                             { settingsModel: fixture.settingsModel })
        verify(dialog)
        dialog.open()
        tryVerify(function() { return dialog.opened })
        verify(fixture.settingsModel.staging)
        verify(dialog.setSetting("app:audio.buffer", 512))
        compare(fixture.settingsModel.settings.find(function(row) {
            return row.scopedKey === "app:audio.buffer"
        }).stagedValue, 512)
        dialog.cancelChanges()
        compare(fixture.settingsModel.valueForKey("app:audio.buffer"), 256)
        dialog.open()
        tryVerify(function() { return dialog.opened })
        verify(dialog.setSetting("app:audio.buffer", 512))
        verify(dialog.applyChanges())
        compare(fixture.settingsModel.valueForKey("app:audio.buffer"), 512)

        const shortcut = createTemporaryObject(shortcutComponent, root,
            { commandRegistry: fixture.commandRegistry, shortcutMap: fixture.shortcutMap,
              settingsModel: fixture.settingsModel })
        verify(shortcut)
        const shortcutEditor = findChild(shortcut, "shortcutEditor")
        verify(shortcutEditor)
        verify(!shortcutEditor.setOverride("file.save", "Ctrl+F"))
        verify(!shortcutEditor.validatePending())
        compare(shortcutEditor.validation.conflicts.length > 0, true)
        shortcutEditor.restoreDefaults()
        verify(shortcutEditor.setOverride("file.save", "Ctrl+Shift+S"))
        verify(shortcutEditor.applyChanges())
        compare(fixture.shortcutMap.displaySequence("file.save"), "Ctrl+Shift+S")
        compare(JSON.parse(fixture.settingsModel.valueForKey("app:shortcuts.overrides"))["file.save"], "Ctrl+Shift+S")
        const fresh = preferencesFixtures.create()
        tryVerify(function() { return fresh && fresh.ready }, 3000)
        const reloaded = createTemporaryObject(shortcutComponent, root,
            { commandRegistry: fresh.commandRegistry, shortcutMap: fresh.shortcutMap,
              settingsModel: fixture.settingsModel })
        verify(reloaded)
        compare(fresh.shortcutMap.displaySequence("file.save"), "Ctrl+Shift+S")
    }

    function test_shortcut_edits_are_staged_conflicts_explained_and_persisted() {
        const fixture = createTemporaryObject(shortcutComponent, root)
        verify(fixture)
        const editor = findChild(fixture, "shortcutEditor")
        verify(editor)
        compare(editor.filteredCommands.length, 3)
        verify(!editor.setOverride("transport.play", "Ctrl+S"))
        verify(editor.validation.conflicts.length > 0)
        compare(shortcutMapFixture.overrides()["transport.play"], undefined)
        editor.resetCommand("transport.play")
        verify(editor.setOverride("transport.play", "Ctrl+Shift+P"))
        verify(editor.applyChanges())
        compare(shortcutMapFixture.overrides()["transport.play"], "Ctrl+Shift+P")
        compare(JSON.parse(settingsFixture.values["app:shortcuts.overrides"])["transport.play"], "Ctrl+Shift+P")
    }

    function test_shortcut_capture_escape_and_tab_leave_capture() {
        const fixture = createTemporaryObject(shortcutComponent, root)
        const editor = findChild(fixture, "shortcutEditor")
        const focusSpy = createTemporaryObject(signalSpyComponent, editor,
            { target: editor, signalName: "focusReturnRequested" })
        verify(focusSpy)
        findChild(editor, "shortcutRecord_edit.save").click()
        compare(editor.recordingCommandId, "edit.save")
        const capture = findChild(editor, "shortcutCapture_edit.save")
        verify(capture && capture.visible)
        const keyboardSink = findChild(editor, "shortcutCaptureKeyboard")
        tryVerify(function() { return keyboardSink && keyboardSink.activeFocus })
        const chordEvent = { key: Qt.Key_K, modifiers: Qt.ControlModifier, accepted: false }
        editor.handleCaptureKey(chordEvent)
        compare(chordEvent.accepted, true)
        tryVerify(function() { return editor.pendingOverrides["edit.save"] === "Ctrl+K" }, 1000)
        tryVerify(function() { return focusSpy.count === 1 })
        compare(focusSpy.signalArguments[0][0], "edit.save")
        tryVerify(function() { return findChild(editor, "shortcutRecord_edit.save").activeFocus })
        findChild(editor, "shortcutRecord_transport.play").click()
        const escapeCapture = findChild(editor, "shortcutCapture_transport.play")
        verify(escapeCapture && escapeCapture.visible)
        const escapeEvent = { key: Qt.Key_Escape, modifiers: Qt.NoModifier, accepted: false }
        editor.handleCaptureKey(escapeEvent)
        compare(escapeEvent.accepted, true)
        compare(focusSpy.count, 2)
        compare(editor.recordingCommandId, "")
        compare(editor.pendingOverrides["transport.play"], undefined)
        tryVerify(function() { return findChild(editor, "shortcutRecord_transport.play").activeFocus })
        findChild(editor, "shortcutRecord_transport.play").click()
        const tabCapture = findChild(editor, "shortcutCapture_transport.play")
        const tabEvent = { key: Qt.Key_Tab, modifiers: Qt.NoModifier, accepted: true }
        editor.handleCaptureKey(tabEvent)
        compare(tabEvent.accepted, false)
        compare(focusSpy.count, 2)
        compare(editor.recordingCommandId, "")
        verify(!editor.pendingOverrides["transport.play"])
        editor.restoreCaptureFocus("recycled-command")
        tryVerify(function() { return findChild(editor, "shortcutSearch").activeFocus })
    }

    function test_find_results_are_revision_tagged_scoped_and_cancellable() {
        const fixture = createTemporaryObject(findComponent, root)
        const bar = findChild(fixture, "findReplaceBar")
        verify(bar)
        findChild(bar, "findQuery").text = "cat"
        bar.searchScope = "selection"
        bar.caseSensitive = true
        const id = bar.searchNow("cat")
        compare(findServiceFixture.lastScope.start, 2)
        compare(findServiceFixture.lastScope.length, 3)
        compare(findServiceFixture.lastOptions.caseSensitive, true)
        findServiceFixture.complete(id, 10, [{ id: "m1", start: 2, length: 3, preview: "cat" }], 0)
        compare(bar.resultsCurrent, true)
        const staleId = id
        ++findServiceFixture.revision
        findServiceFixture.stateChanged()
        verify(findServiceFixture.lastRequestId !== staleId)
        findServiceFixture.complete(staleId, 10, [{ id: "old", start: 0, length: 3, preview: "cat" }], 0)
        compare(bar.acceptedRevision, -1)
        bar.requestSearch("dog")
        verify(findServiceFixture.cancelled.includes(findServiceFixture.lastRequestId))
    }

    function test_find_replace_all_uses_revision_and_one_undo_group() {
        const fixture = createTemporaryObject(findComponent, root)
        const bar = findChild(fixture, "findReplaceBar")
        bar.searchScope = "document"
        findChild(bar, "findQuery").text = "cat"
        const id = bar.searchNow("cat")
        findServiceFixture.complete(id, 10, [
            { id: "m1", start: 0, length: 3, preview: "cat" },
            { id: "m2", start: 4, length: 3, preview: "cat" }
        ], 0)
        findChild(bar, "replaceText").text = "dog"
        compare(findChild(bar, "replaceAll").enabled, true)
        verify(bar.replaceEveryMatch())
        compare(findServiceFixture.text, "dog dog")
        compare(findServiceFixture.undoGroups, 1)
        compare(findServiceFixture.revision, 11)
    }

    function test_find_replace_disabled_for_read_only_and_composition() {
        const fixture = createTemporaryObject(findComponent, root)
        const bar = findChild(fixture, "findReplaceBar")
        findChild(bar, "findQuery").text = "cat"
        const id = bar.searchNow("cat")
        findServiceFixture.complete(id, 10, [{ id: "m1", start: 0, length: 3, preview: "cat" }], 0)
        findServiceFixture.readOnly = true
        findServiceFixture.stateChanged()
        compare(findChild(bar, "replaceAll").enabled, false)
        verify(!bar.replaceEveryMatch())
        findServiceFixture.readOnly = false
        findServiceFixture.composing = true
        findServiceFixture.stateChanged()
        compare(findChild(bar, "replaceCurrent").enabled, false)
        verify(bar.searchNow("cat") === "")
    }

    function test_real_document_search_replace_all_and_single_undo() {
        const fixture = createTemporaryObject(realFindComponent, root)
        verify(fixture)
        const document = findChild(fixture, "realTextDocument")
        const service = findChild(fixture, "realFindReplaceService")
        const bar = findChild(fixture, "realFindReplaceBar")
        verify(document && service && bar)
        findChild(bar, "findQuery").text = "cat"
        const requestId = bar.searchNow("cat")
        verify(requestId.length > 0)
        tryVerify(function() { return bar.resultsCurrent && service.matchCount === 2 }, 3000)
        findChild(bar, "replaceText").text = "dog"
        verify(bar.replaceEveryMatch())
        compare(document.text, "dog dog")
        document.undo()
        compare(document.text, "cat cat")
    }
}
