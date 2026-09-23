// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "PartWhole"
    when: windowShown
    visible: true
    width: 360
    height: 280
    Component {
        id: fixture
        Tk.PartWholeView {
            width: 320
            height: 240
            tooltip: "Budget mix"
            model: Tk.PartWholeModel {
                id: data
                unit: "USD"
                Component.onCompleted: replaceNodes([
                    {id: "root", value: 4},
                    {id: "a", parentId: "root", label: "A", value: 1},
                    {id: "b", parentId: "root", label: "B", value: 3}
                ])
            }
        }
    }
    function test_modes_selection_and_table() {
        const view=createTemporaryObject(fixture,this)
        verify(view !== null)
        compare(view.model.knownTotal,4)
        compare(view.geometryFor("a").angleSpan,90)
        verify(view.selectId("b"))
        compare(view.selectedNode.label,"B")
        view.mode=Tk.PartWholeRenderer.Treemap
        compare(view.geometryFor("a").width,0.25)
        view.showDataTable=true
        verify(findChild(view, "partWholeDataTable").visible)
        compare(view.objectName,"partWholeView")
        compare(view.model.get(1).id,"a")
    }
}
