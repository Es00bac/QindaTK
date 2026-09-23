// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "PreferencesDialogLifecycle"
    when: windowShown
    visible: true
    width: 900
    height: 700

    Component { id: dialogComponent; Tk.PreferencesDialog {} }

    function makeDialog() {
        const fixture = preferencesFixtures.create()
        verify(fixture && fixture.ready)
        const dialog = createTemporaryObject(dialogComponent, root,
                                             { settingsModel: fixture.settingsModel })
        verify(dialog)
        return { fixture: fixture, model: fixture.settingsModel, dialog: dialog }
    }

    function openDialog(dialog) {
        dialog.open()
        tryVerify(function() { return dialog.opened })
        tryVerify(function() { return findChild(dialog, "preferencesSearch").activeFocus })
    }

    function test_escape_and_close_discard_then_reopen_cleanly() {
        const scene = makeDialog()
        const dialog = scene.dialog
        openDialog(dialog)
        verify(dialog.setSetting("app:audio.buffer", 512))
        dialog.selectedCategory = "Audio"
        dialog.searchText = "buffer"
        const search = findChild(dialog, "preferencesSearch")
        search.text = "buffer"
        keyClick(Qt.Key_Escape)
        tryVerify(function() { return !dialog.opened })
        compare(scene.model.staging, false)
        compare(scene.model.valueForKey("app:audio.buffer"), 256)
        openDialog(dialog)
        compare(dialog.searchText, "")
        compare(dialog.selectedCategory, "")
        compare(search.text, "")
        compare(findChild(dialog, "preferencesSettings").count, 3)
        verify(dialog.setSetting("app:audio.buffer", 1024))
        dialog.close()
        tryVerify(function() { return !dialog.opened })
        compare(scene.model.staging, false)
        compare(scene.model.valueForKey("app:audio.buffer"), 256)
    }

    function test_enter_applies_and_rejects_invalid_focused_text() {
        const scene = makeDialog()
        const dialog = scene.dialog
        openDialog(dialog)
        verify(dialog.setSetting("app:audio.buffer", 512))
        keyClick(Qt.Key_Return)
        tryVerify(function() { return !dialog.opened })
        compare(scene.model.valueForKey("app:audio.buffer"), 512)
        openDialog(dialog)
        dialog.searchText = "shortcuts"
        compare(dialog.filteredSettings.length, 1)
        const list = findChild(dialog, "preferencesSettings")
        tryCompare(list, "count", 1)
        let row = null
        tryVerify(function() {
            list.forceLayout()
            row = list.itemAtIndex(0)
            return row && row.objectName === "preferenceRow_app_shortcuts_overrides"
        })
        const editor = findChild(row, "preference_app_shortcuts_overrides")
        verify(editor)
        editor.forceActiveFocus()
        editor.text = "not json"
        keyClick(Qt.Key_Return)
        compare(dialog.opened, true)
        compare(scene.model.staging, true)
        compare(scene.model.valueForKey("app:shortcuts.overrides"), "{}")
        compare(dialog.invalidKey, "app:shortcuts.overrides")
        dialog.cancelChanges()
    }

    function test_invalid_hidden_setting_reveals_and_focuses_editor() {
        const scene = makeDialog()
        const dialog = scene.dialog
        openDialog(dialog)
        dialog.selectedCategory = "Audio"
        dialog.searchText = "buffer"
        findChild(dialog, "preferencesSearch").text = "buffer"
        compare(dialog.filteredSettings.length, 1)
        verify(!dialog.setSetting("app:shortcuts.overrides", "not json"))
        tryCompare(dialog, "selectedCategory", "")
        tryCompare(dialog, "searchText", "")
        tryCompare(findChild(dialog, "preferencesSearch"), "text", "")
        const list = findChild(dialog, "preferencesSettings")
        tryCompare(list, "count", 3)
        tryVerify(function() {
            list.forceLayout()
            const row = list.itemAtIndex(2)
            const editor = row ? findChild(row, "preference_app_shortcuts_overrides") : null
            return editor && editor.activeFocus
        })
        compare(dialog.invalidKey, "app:shortcuts.overrides")
        dialog.cancelChanges()
    }
}
