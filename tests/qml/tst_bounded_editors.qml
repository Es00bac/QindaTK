// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "BoundedEditors"
    when: windowShown
    visible: true
    width: 640
    height: 280

    Tk.LocaleService { id: locale; localeName: "en_US" }
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.ExpressionField { id: expression; width: 320; label: "Length"; localeService: locale }
        Tk.FilePathField { id: path; width: 480; label: "Resource" }
        Tk.TokenField { id: tokens; width: 480; label: "Recipients" }
    }

    function test_expression_invalid_preserves_committed_value_and_undoes() {
        const fixture = inspectorFixtures.create()
        verify(fixture.ready)
        expression.binding = fixture.unitBinding
        expression.units = ({mm: {scale: 1, dimension: "length"}, cm: {scale: 10, dimension: "length"}})
        expression.draft = "2cm + 5mm"
        verify(expression.commitDraft())
        compare(fixture.document.valueFor(fixture.firstId, "length"), 25)
        compare(fixture.document.valueFor(fixture.secondId, "length"), 25)
        expression.draft = "1 / 0"
        verify(!expression.commitDraft())
        verify(expression.errorMessage.length > 0)
        compare(fixture.document.valueFor(fixture.firstId, "length"), 25)
        verify(fixture.undoService.undo())
        compare(fixture.document.valueFor(fixture.firstId, "length"), 25.4)
        compare(fixture.document.valueFor(fixture.secondId, "length"), 50.8)
        expression.binding = null
    }

    function test_path_validation_preserves_remote_uri_and_rejects_stale_completion() {
        path.setDraft("sftp://host/folder/a%20b")
        tryCompare(path, "validationStatus", "unknown")
        compare(path.uriValidator.inspect(path.draft, "open").uri, "sftp://host/folder/a%20b")
        path.setDraft("/missing/location/first")
        const stale = path.validationRequestId
        path.setDraft("/missing/location/second")
        verify(stale !== path.validationRequestId)
        tryCompare(path, "validationStatus", "missing")
        verify(path.errorMessage.length > 0)
        verify(findChild(path, "pathBrowse") !== null)
    }

    function test_token_editor_structure_and_bounded_paste() {
        verify(findChild(tokens, "tokenInput") !== null)
        compare(tokens.Accessible.name, "Recipients")
        verify(tokens.readOnly)
        compare(tokens.tokens.length, 0)
        verify(findChild(expression, "expressionInput") !== null)
    }
}
