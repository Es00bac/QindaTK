// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "DocumentReview"
    when: windowShown
    visible: true
    width: 760
    height: 520

    Component {
        id: managerComponent
        Tk.ReferenceManager {
            width: 500
            references: [{ id: "ref-1", label: "Prior study", type: "citation", targetId: "bib-2", status: "resolved" }]
            targets: [{ id: "bib-2", label: "Author · 2025", type: "bibliography" }]
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    function test_add_reference_emits_stable_target_request() {
        const manager = createTemporaryObject(managerComponent, this)
        verify(manager !== null)
        const requested = createTemporaryObject(spyComponent, this,
            { target: manager, signalName: "addReferenceRequested" })
        manager.draftType = "citation"
        manager.draftTargetId = "bib-2"
        const add = findChild(manager, "referenceAdd")
        verify(add !== null && add.enabled)

        add.click()

        compare(requested.count, 1)
        compare(requested.signalArguments[0][0], "citation")
        compare(requested.signalArguments[0][1], "bib-2")
        compare(manager.references.length, 1)
    }
}
