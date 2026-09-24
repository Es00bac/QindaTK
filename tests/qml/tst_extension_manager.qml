// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "ExtensionManager"
    visible: true
    when: windowShown
    width: 720
    height: 500

    Tk.ExtensionRegistry { id: registry }
    Tk.ExtensionManager {
        id: manager
        width: 700
        height: 480
        registry: registry
    }

    function test_restored_missing_extension_is_visible_and_cannot_load() {
        verify(registry.restoreState([
            {id: "org.example.missing", version: "1.0.0",
             descriptorPath: "/absent/extension.json", state: {note: "kept"}}
        ]))
        tryVerify(function() { return findChild(manager, "extension_org.example.missing") !== null })
        tryVerify(function() { return findChild(manager, "extension_org.example.missing").height > 0 })
        const load = findChild(manager, "extensionLoad_org.example.missing")
        const diagnostic = findChild(manager, "extensionDiagnostic_org.example.missing")
        verify(load !== null)
        verify(diagnostic !== null)
        verify(!load.enabled)
        verify(diagnostic.text.length > 0)
        compare(registry.saveState()[0].state.note, "kept")
    }

    function test_add_requires_local_absolute_path() {
        verify(!manager.addDescriptor("relative.json"))
        verify(findChild(manager, "extensionActionMessage").text.length > 0)
    }
    function test_discovery_controls_and_diagnostic() {
        verify(!manager.addDirectory("relative"))
        registry.searchDirectories = ["/qindatk-extension-fixture-absent"]
        verify(!registry.refreshDiscovery())
        const diagnostic = findChild(manager, "extensionDiscoveryDiagnostic")
        const refresh = findChild(manager, "extensionRefresh")
        verify(diagnostic !== null)
        verify(diagnostic.visible)
        verify(diagnostic.text.indexOf("unavailable") >= 0)
        verify(refresh !== null)
        verify(refresh.enabled)
    }
}
