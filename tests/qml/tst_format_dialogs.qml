// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "FormatDialogs"
    when: windowShown
    visible: true
    width: 800
    height: 600

    Component {
        id: signalSpyComponent
        SignalSpy { }
    }

    Component {
        id: dialogsComponent
        Item {
            property alias registry: registry
            property alias manager: manager
            property alias picker: picker
            Tk.FormatRegistry { id: registry }
            Tk.DocumentManager { id: manager }
            Tk.FileDialogAdapter { id: picker }
            Tk.ImportDialog { id: importer; formatRegistry: registry; documentManager: manager; fileDialogAdapter: picker }
            Tk.ExportDialog { id: exporter; formatRegistry: registry; documentSession: manager.activeSession; fileDialogAdapter: picker }
        }
    }

    function test_operation_scoped_formats_and_safe_preflight() {
        const fixture = createTemporaryObject(dialogsComponent, root)
        verify(fixture)
        const importer = findChild(fixture, "importDialog")
        const exporter = findChild(fixture, "exportDialog")
        verify(importer && exporter)
        compare(importer.operationFormats.length, 2)
        compare(exporter.operationFormats.length, 2)
        const ids = importer.operationFormats.map(function(item) { return item.formatId })
        verify(ids.indexOf("qindatk.reference.v2") >= 0)
        verify(ids.indexOf("text.utf8") >= 0)
        verify(ids.indexOf("adobe") < 0)
        compare(importer.canStart(), false)
        compare(exporter.canStart(), false)
        importer.formatId = "qindatk.reference.v2"
        importer.sourceUrl = "file:///definitely-missing-qindatk-input.qtk"
        verify(importer.plan.ok === false)
        verify(importer.plan.error.message.length > 0)
        compare(importer.canStart(), false)
        exporter.formatId = "qindatk.reference.v2"
        exporter.destinationUrl = "file:///tmp/qindatk-format-dialog.qtk"
        verify(exporter.plan.ok === false)
        compare(exporter.canStart(), false)
    }

    function test_file_picker_unavailable_is_reported_without_job() {
        const fixture = createTemporaryObject(dialogsComponent, root)
        verify(fixture)
        const importer = findChild(fixture, "importDialog")
        verify(importer)
        verify(!fixture.picker.available)
        importer.chooseSource()
        tryVerify(function() { return importer.statusText.length > 0 })
        verify(importer.statusText.indexOf("dialog backend") >= 0)
        compare(importer.jobId, "")
        verify(!importer.busy)
    }

    function test_import_adopts_only_after_job_success() {
        const fixture = createTemporaryObject(dialogsComponent, root)
        verify(fixture)
        const importer = findChild(fixture, "importDialog")
        const adopted = createTemporaryObject(signalSpyComponent, root,
            { target: importer, signalName: "importAdopted" })
        importer.formatId = "text.utf8"
        importer.sourceUrl = Qt.resolvedUrl("fixtures/plain_text.txt")
        tryVerify(function() { return importer.plan.ok === true })
        verify(importer.canStart())
        importer.startOrRetry()
        verify(importer.jobId.length > 0)
        tryVerify(function() { return !importer.busy }, 5000)
        tryCompare(adopted, "count", 1, 1000)
        compare(fixture.manager.sessions.length, 1)
        compare(importer.busy, false)
        verify(adopted.signalArguments[0][0] !== null)
    }

    function test_replace_policy_does_not_bypass_preflight() {
        const fixture = createTemporaryObject(dialogsComponent, root)
        verify(fixture)
        const exporter = findChild(fixture, "exportDialog")
        exporter.formatId = "qindatk.reference.v2"
        exporter.destinationUrl = "file:///tmp/qindatk-format-dialog.qtk"
        exporter.conflictPolicy = "replaceIfHash"
        verify(exporter.plan.ok === false)
        compare(exporter.canStart(), false)
        compare(exporter.jobId, "")
    }
}
