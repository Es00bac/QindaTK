// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "NavigationModels"
    when: windowShown
    visible: true
    width: 720
    height: 480

    Component {
        id: ribbonComponent
        Tk.Ribbon {
            width: 120
            tabs: [
                { id: "home", label: "Home", groups: [
                    { id: "clipboard", label: "Clipboard", items: [
                        { id: "paste", label: "Paste", iconName: "clipboard", keyTip: "P" },
                        { id: "copy", label: "Copy", iconName: "copy" },
                        { id: "cut", label: "Cut", iconName: "cut" },
                        { id: "undo", label: "Undo", iconName: "undo" },
                        { id: "redo", label: "Redo", iconName: "redo" }
                    ] }
                ] },
                { id: "picture", label: "Picture", contextual: true, contextId: "image", groups: [] }
            ]
        }
    }
    Component {
        id: railComponent
        Tk.NavigationRail {
            width: expanded ? 180 : 44
            height: 180
            groups: [{ id: "work", label: "Workspace", entries: [
                { id: "files", label: "Very long file destination", destination: "route.files", iconName: "files", badge: "3" },
                { id: "jobs", label: "Jobs", destination: "route.jobs", iconName: "clock", visible: false },
                { id: "media", label: "Media", destination: "route.media", iconName: "film" }
            ] }]
        }
    }
    Component {
        id: commandRibbonComponent
        Item {
            property var serviceFixture: preferencesFixtures.create()
            property alias ribbon: ribbon
            property var registry: serviceFixture ? serviceFixture.commandRegistry : null
            Tk.Ribbon {
                id: ribbon
                width: 240
                commandRegistry: parent.registry
                tabs: [{ id: "file", label: "File", groups: [
                    { id: "actions", label: "Actions", items: [
                        { id: "save", label: "Save", commandId: "file.save", keyTip: "S" },
                        { id: "missing", label: "Unavailable", commandId: "file.missing", keyTip: "U" }
                    ] }
                ] }]
            }
        }
    }
    Component {
        id: breadcrumbComponent
        Tk.BreadcrumbBar {
            width: 150
            path: [
                { id: "root", label: "Workspace" },
                { id: "folder", label: "Very long folder name", siblings: [{ id: "folder", label: "Very long folder name" }, { id: "other", label: "Other folder" }] },
                { id: "file", label: "a-very-long-file-name.qtk" }
            ]
        }
    }
    Component {
        id: asyncBreadcrumbComponent
        Tk.BreadcrumbBar {
            width: 180
            path: [
                { id: "root", label: "Workspace" },
                { id: "folder", label: "Folder", siblingProvider: true },
                { id: "file", label: "File" }
            ]
        }
    }
    Component {
        id: tabsComponent
        Item {
            property alias manager: manager
            property alias tabs: tabs
            Tk.DocumentManager { id: manager }
            Tk.DocumentTabs { id: tabs; documentManager: manager; width: 260 }
        }
    }
    Component { id: spyComponent; SignalSpy { } }

    function test_ribbon_context_keyboard_and_reachability() {
        const ribbon = createTemporaryObject(ribbonComponent, root)
        verify(ribbon)
        compare(ribbon.visibleTabs.length, 1)
        compare(ribbon.currentTabId, "home")
        const group = findChild(ribbon, "ribbonGroup_clipboard")
        verify(group)
        verify(findChild(ribbon, "ribbonItem_paste"))
        // D-297: a narrow ribbon squeezes and wraps instead of scrolling.
        tryVerify(function() { return ribbon.squeezeLevel >= 2 })
        const paste = findChild(ribbon, "ribbonItem_paste")
        verify(paste.mapToItem(ribbon, paste.width, 0).x <= ribbon.width + 0.5)
        const requested = createTemporaryObject(spyComponent, root, { target: ribbon, signalName: "itemRequested" })
        ribbon.keyTipsVisible = true
        verify(ribbon.onKey({ key: Qt.Key_P, text: "P" }))
        compare(requested.signalArguments[0][2], "paste")
        compare(ribbon.keyTipsVisible, false)
        ribbon.keyTipsVisible = true
        verify(findChild(ribbon, "ribbonAction_paste"))
        findChild(ribbon, "ribbonAction_paste").click()
        compare(requested.count, 2)
        ribbon.contextIds = ["image"]
        compare(ribbon.visibleTabs.length, 2)
        verify(ribbon.activateTab("picture"))
        ribbon.collapsed = true
        verify(findChild(ribbon, "ribbonGroupViewport").height === 0)
        ribbon.collapsed = false
        ribbon.forceActiveFocus()
        keyClick(Qt.Key_Left)
        compare(ribbon.currentTabId, "home")
    }

    function test_ribbon_key_tips_use_registry_command_authority() {
        const fixture = createTemporaryObject(commandRibbonComponent, root)
        verify(fixture && fixture.serviceFixture.ready)
        const ribbon = fixture.ribbon
        const invoked = createTemporaryObject(spyComponent, root,
            { target: fixture.registry, signalName: "invoked" })
        const requested = createTemporaryObject(spyComponent, root,
            { target: ribbon, signalName: "itemRequested" })
        ribbon.keyTipsVisible = true
        verify(ribbon.onKey({ key: Qt.Key_S, text: "S" }))
        compare(invoked.count, 1)
        compare(requested.count, 1)
        const save = findChild(ribbon, "ribbonItem_save")
        verify(save)
        const button = findChild(save, "commandButtonAction")
        verify(button && button.enabled)
        button.click()
        compare(invoked.count, 2)
        compare(requested.count, 2)
        ribbon.keyTipsVisible = true
        verify(ribbon.onKey({ key: Qt.Key_U, text: "U" }))
        compare(invoked.count, 2)
        compare(requested.count, 2)
    }

    function test_navigation_rail_hidden_routes_compact_and_keyboard() {
        const rail = createTemporaryObject(railComponent, root)
        verify(rail)
        compare(rail.entries.filter(function(item) { return item.kind === "entry" }).length, 2)
        const spy = createTemporaryObject(spyComponent, root, { target: rail, signalName: "destinationRequested" })
        verify(rail.activate(rail.entries.filter(function(item) { return item.id === "files" })[0]))
        compare(spy.signalArguments[0][0], "files")
        compare(spy.signalArguments[0][1], "route.files")
        rail.expanded = false
        compare(rail.implicitWidth, rail.compactWidth)
        const button = findChild(rail, "navigationButton_files")
        const icon = findChild(rail, "navigationIcon_files")
        verify(button && icon)
        tryVerify(function() {
            const iconCenter = icon.mapToItem(button, icon.width / 2, icon.height / 2).x
            return Math.abs(iconCenter - button.width / 2) < 0.5
        })
    }

    function test_navigation_route_confirmation_and_reconciliation() {
        const restored = createTemporaryObject(railComponent, root, { currentId: "jobs" })
        verify(restored)
        compare(restored.currentId, "files") // Hidden persisted destination is reconciled.
        const rail = createTemporaryObject(railComponent, root)
        verify(rail)
        compare(rail.currentId, "files")
        const requested = createTemporaryObject(spyComponent, root,
            { target: rail, signalName: "destinationRequested" })
        const reconciled = createTemporaryObject(spyComponent, root,
            { target: rail, signalName: "routeReconciled" })
        const media = rail.entries.find(function(entry) { return entry.id === "media" })
        verify(rail.activate(media))
        compare(requested.count, 1)
        compare(rail.currentId, "files") // A host refusal leaves the active route intact.
        verify(rail.confirmRoute("media"))
        compare(rail.currentId, "media")
        verify(findChild(rail, "navigationButton_media").Accessible.selected)
        verify(!rail.confirmRoute("jobs")) // Hidden route cannot become active.
        compare(rail.currentId, "media")
        rail.currentId = "missing" // Invalid restored route resolves to a visible entry.
        compare(rail.currentId, "files")
        compare(reconciled.count, 1)
        rail.confirmRoute("media")
        rail.groups = [{ id: "work", label: "Workspace", entries: [
            { id: "files", label: "Files", destination: "route.files", iconName: "files" }
        ] }]
        compare(rail.currentId, "files")
        compare(reconciled.count, 2)
        verify(!rail.activate(media)) // Stale delegate data cannot request removed routes.
        rail.groups = [{ id: "work", label: "Workspace", entries: [] }]
        compare(rail.currentId, "")
    }

    function test_breadcrumb_uses_ids_and_scrolls_deep_paths() {
        const bar = createTemporaryObject(breadcrumbComponent, root)
        verify(bar)
        const viewport = findChild(bar, "breadcrumbViewport")
        tryVerify(function() { return viewport.scrollableX })
        const spy = createTemporaryObject(spyComponent, root, { target: bar, signalName: "navigateRequested" })
        verify(bar.activate("folder"))
        compare(spy.signalArguments[0][0], "folder")
        verify(findChild(bar, "breadcrumbSegment_file"))
        bar.showSiblings(bar.path[1])
        compare(bar.siblingItems.length, 2)
    }

    function test_breadcrumb_async_sibling_request_and_stale_result() {
        const bar = createTemporaryObject(asyncBreadcrumbComponent, root)
        verify(bar)
        const button = findChild(bar, "breadcrumbSiblings_folder")
        verify(button && button.visible)
        const requested = createTemporaryObject(spyComponent, root,
            { target: bar, signalName: "siblingsRequested" })
        button.click()
        compare(requested.count, 1)
        compare(requested.signalArguments[0][0], "folder")
        const firstId = requested.signalArguments[0][1]
        verify(bar.siblingPending)
        button.click()
        compare(requested.count, 2)
        const currentId = requested.signalArguments[1][1]
        verify(firstId !== currentId)
        verify(!bar.supplySiblings("folder", firstId, [{ id: "stale", label: "Stale" }]))
        verify(!bar.supplySiblings("other", currentId, [{ id: "wrong", label: "Wrong" }]))
        compare(bar.siblingItems.length, 0)
        verify(bar.supplySiblings("folder", currentId, [
            { id: "folder", label: "Folder" }, { id: "sibling", label: "Sibling" }
        ]))
        compare(bar.siblingPending, false)
        compare(bar.siblingItems.length, 2)
        tryVerify(function() { return findChild(bar, "breadcrumbSiblingMenu").visible })
        verify(!bar.supplySiblings("folder", currentId, []))
        verify(bar.showSiblings(bar.path[1], button))
        const removedPathRequest = bar.siblingRequestId
        bar.path = [{ id: "root", label: "Workspace" }]
        compare(bar.siblingPending, false)
        verify(!bar.supplySiblings("folder", removedPathRequest,
                                   [{ id: "late", label: "Late" }]))
    }

    function test_document_tabs_wait_for_authoritative_close_result() {
        const fixture = createTemporaryObject(tabsComponent, root)
        verify(fixture)
        fixture.manager.createReferenceDocument()
        fixture.manager.createReferenceDocument()
        tryVerify(function() { return fixture.tabs.entries.length === 2 })
        const first = fixture.tabs.entries[0]
        verify(fixture.tabs.requestReorder(first.viewId, 1))
        compare(fixture.manager.viewOrder[1], first.viewId)
        compare(fixture.tabs.entries[1].viewId, first.viewId)
        const requested = createTemporaryObject(spyComponent, root, { target: fixture.tabs, signalName: "closeRequested" })
        const failed = createTemporaryObject(spyComponent, root, { target: fixture.tabs, signalName: "closeFailed" })
        verify(fixture.tabs.requestClose(first.viewId))
        compare(requested.count, 1)
        compare(fixture.tabs.entries.length, 2)
        verify(!fixture.tabs.completeClose(first.documentId, first.viewId, 0))
        compare(fixture.tabs.entries.length, 2)
        compare(failed.count, 1)
        verify(fixture.tabs.errorText.length > 0)
        verify(fixture.tabs.requestClose(first.viewId))
        verify(fixture.tabs.completeClose(first.documentId, first.viewId, 1))
        tryVerify(function() { return fixture.manager.sessions.length === 1 })
        compare(fixture.tabs.entries.length, 1)
    }
}
