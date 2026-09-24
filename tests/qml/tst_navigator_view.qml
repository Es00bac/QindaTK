// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

// WP-042 NavigatorView: pointer, keyboard and wheel navigation write only the
// main pose (one atomic change per step), the overview keeps its own fit and a
// lower level of detail, and navigation still maps after a model reset.
TestCase {
    id: root
    name: "NavigatorView"
    when: windowShown
    visible: true
    width: 640
    height: 340

    Component {
        id: sceneComponent
        Item {
            width: 620
            height: 320
            property alias document: doc
            property alias main: mainView
            property alias navigator: nav
            property alias tools: controller
            property alias poseSpy: poseChanges
            Tk.CanvasDocument { id: doc; pageRect: Qt.rect(0, 0, 400, 300) }
            Tk.ToolController { id: controller; document: doc }
            Tk.CanvasViewState { id: pose }
            Tk.CanvasView {
                id: mainView
                width: 400
                height: 300
                document: doc
                toolController: controller
                viewState: pose
                fitMode: "page"
            }
            Tk.NavigatorView {
                id: nav
                x: 410
                width: 200
                height: 150
                mainView: mainView
            }
            SignalSpy { id: poseChanges; target: pose; signalName: "changed" }
        }
    }

    function make() {
        const scene = createTemporaryObject(sceneComponent, root)
        verify(scene !== null)
        waitForRendering(scene)
        return scene
    }

    function test_pointer_navigation_moves_only_the_main_pose() {
        const scene = make()
        const doc = scene.document
        const nav = scene.navigator
        const id = doc.addRect(20, 20, 60, 40, "red")
        scene.tools.selection = [id]
        const revision = doc.revision
        const previewX = nav.preview.centerX
        const previewZoom = nav.preview.zoom
        scene.poseSpy.clear()
        const target = Qt.point(150, 40)
        const expected = nav.preview.mapToDocument(target)
        mousePress(nav, target.x, target.y)
        mouseRelease(nav, target.x, target.y)
        compare(scene.poseSpy.count, 1) // one atomic centre change
        fuzzyCompare(scene.main.centerX, expected.x, 1e-9)
        fuzzyCompare(scene.main.centerY, expected.y, 1e-9)
        compare(doc.revision, revision)             // navigation never edits
        compare(scene.tools.selection.length, 1)    // nor changes the selection
        compare(nav.preview.centerX, previewX)      // the overview keeps its fit
        compare(nav.preview.zoom, previewZoom)
        // The page-fitted main view does not refit over the navigation.
        verify(doc.addRect(300, 200, 10, 10, "blue") > 0)
        fuzzyCompare(scene.main.centerX, expected.x, 1e-9)
        fuzzyCompare(scene.main.centerY, expected.y, 1e-9)
    }

    function test_keyboard_and_wheel_navigation() {
        const scene = make()
        const nav = scene.navigator
        const main = scene.main
        compare(nav.Accessible.role, Accessible.Pane)
        compare(nav.Accessible.name, "Canvas navigator")
        verify(nav.activeFocusOnTab)
        nav.forceActiveFocus()
        verify(findChild(nav, "navigatorFocusRing").visible)
        const width = main.visibleDocumentRect.width
        const cx = main.centerX
        const cy = main.centerY
        keyClick(Qt.Key_Right)
        fuzzyCompare(main.centerX, cx + width * nav.keyboardStep, 1e-9)
        fuzzyCompare(main.centerY, cy, 1e-9)
        const zoom = main.zoom
        keyClick(Qt.Key_Plus)
        fuzzyCompare(main.zoom, zoom * (1 + nav.keyboardStep), 1e-9)
        const zoomed = main.zoom
        mouseWheel(nav, nav.width / 2, nav.height / 2, 0, 120)
        fuzzyCompare(main.zoom, zoomed * main.wheelZoomStep, 1e-9)
    }

    function test_overview_follows_a_model_reset() {
        const scene = make()
        const doc = scene.document
        const nav = scene.navigator
        verify(nav.navigateAt(Qt.point(20, 20))) // the user has navigated
        const generation = doc.resetGeneration
        doc.populateFixture(400, 5, false) // a load: new content and reset generation
        verify(doc.resetGeneration !== generation)
        const content = doc.contentBounds
        const cx = content.x + content.width / 2
        const cy = content.y + content.height / 2
        // The overview refits to the new content; its centre maps to it exactly.
        const centre = nav.preview.mapToDocument(Qt.point(nav.width / 2, nav.height / 2))
        fuzzyCompare(centre.x, cx, 1e-6)
        fuzzyCompare(centre.y, cy, 1e-6)
        verify(nav.navigateAt(Qt.point(nav.width / 2, nav.height / 2)))
        fuzzyCompare(scene.main.centerX, cx, 1e-6)
        fuzzyCompare(scene.main.centerY, cy, 1e-6)
        // The overview renders a coarser level of detail than the zoomed main view.
        scene.main.setZoomCentered(4)
        tryVerify(function() {
            return nav.preview.renderStats().lod < scene.main.renderStats().lod
        })
    }
}
