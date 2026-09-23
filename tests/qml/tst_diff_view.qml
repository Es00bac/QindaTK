// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "DiffView"
    visible: true
    width: 800
    height: 400

    Tk.DiffModel { id: model }
    Tk.DiffView {
        id: view
        anchors.fill: parent
        diffModel: model
        tooltip: "Test differences"
    }

    function test_navigation_and_layout() {
        verify(model.setDocuments(["a", "old", "b", "old 2", "c"],
                                  ["a", "new", "b", "new 2", "c"], "v1", "v2"))
        compare(view.hunkCount, 2)
        compare(view.currentHunkId, model.hunks[0].id)
        verify(view.navigate(1))
        compare(view.currentHunkId, model.hunks[1].id)
        const unaffected = view.currentHunkId
        verify(model.setDocuments(["a", "new", "b", "old 2", "c"],
                                  ["a", "new", "b", "new 2", "c"], "v3", "v2"))
        compare(view.currentHunkId, unaffected)
        compare(view.hunkCount, 1)
        view.sideBySide = false
        compare(view.sideBySide, false)
        compare(view.request("accept"), "") // no host adapter installed
        compare(model.get(3).leftText, "old 2")
    }
}
