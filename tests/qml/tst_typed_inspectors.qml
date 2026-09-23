// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    id: root
    name: "TypedInspectors"
    when: windowShown
    visible: true
    width: 720
    height: 520

    Tk.Flex {
        id: fixture
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.md
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.FormGroup {
            id: form
            title: "Geometry"
            width: 500
            Tk.PropertyRow { label: "Origin"; Tk.VectorField { tooltip: "Origin"; componentLabels: ["X", "Y"] } }
            Tk.PropertyRow { label: "Angle"; Tk.UnitField { label: "Angle"; displayUnit: "deg"; unitScale: 1; precision: 1 } }
        }
        Tk.Inspector {
            id: inspector
            width: 500
            properties: [
                { id: "width", label: "Width", editorType: "number", displayUnit: "mm", unitScale: 1 },
                { id: "position", label: "Position", editorType: "vector" },
                { id: "name", label: "Name", editorType: "text" }
            ]
        }
        Tk.TransformEditor { id: transform; width: 500 }
    }

    Component { id: unitComponent; Tk.UnitField { width: 180; displayUnit: "in"; unitScale: 25.4; precision: 3 } }
    Component { id: mixedComponent; Tk.MixedValueField { width: 240; label: "Opacity"; helpText: "Alpha channel"; Tk.NumberField { value: 0.5 } } }
    Component { id: vectorComponent; Tk.VectorField { width: 300; componentLabels: ["X", "Y", "Z"]; componentUnits: ["px", "px", "px"] } }
    Component { id: groupComponent; Tk.FormGroup { width: 260; title: "Advanced"; collapsed: true; Tk.Label { text: "Hidden" } } }

    function test_controls_instantiate_with_accessible_contracts() {
        verify(form && inspector && transform)
        compare(form.Accessible.name, "Geometry")
        compare(inspector.Accessible.name, "Inspector")
        compare(transform.Accessible.name, "Transform")
        compare(inspector.visibleCount, 3)
        verify(findChild(root, "inspectorRow_width") !== null)
        verify(findChild(root, "vectorComponent_0") !== null)
    }

    function test_canonical_unit_conversion_and_validation() {
        const field = createTemporaryObject(unitComponent, root)
        verify(field)
        compare(field.conversionValid, true)
        compare(field.canonical(2), 50.8)
        compare(field.setUnit("unknown"), false)
        verify(field.setUnit("cm") === false)
        field.unitScale = 0
        compare(field.conversionValid, false)
        verify(field.readOnly)
    }

    function test_vector_axes_and_mixed_wrapper_instantiation() {
        const vector = createTemporaryObject(vectorComponent, root)
        const mixed = createTemporaryObject(mixedComponent, root)
        verify(vector && mixed)
        compare(vector.componentCount, 3)
        compare(mixed.mixed, false)
        verify(findChild(vector, "vectorComponent_2") !== null)
        verify(findChild(mixed, "mixedValueEditor") !== null)
        compare(findChild(mixed, "mixedValueReset").visible, false)
    }

    function test_form_group_reports_pending_and_focuses_first_invalid() {
        const invalid = Qt.createQmlObject('import QtQuick; TextInput { objectName: "invalidField" }', root)
        const group = createTemporaryObject(groupComponent, root)
        verify(invalid && group)
        group.validationItems = [{ item: invalid, invalid: true, pending: false },
                                 { item: invalid, invalid: false, pending: true }]
        compare(group.invalidCount, 1)
        compare(group.pendingCount, 1)
        verify(group.focusFirstInvalid())
        tryVerify(function() { return invalid.activeFocus })
    }

    function test_invalid_unit_metadata_is_rejected() {
        const field = createTemporaryObject(unitComponent, root)
        field.units = [{ id: "bad", label: "bad", scale: 0, offset: 0 }]
        compare(field.setUnit("bad"), false)
        field.units = [{ id: "ok", label: "ok", scale: 2, offset: 3 }]
        verify(field.setUnit("ok"))
        compare(field.canonical(4), 11)
        compare(field.displayValue, -1.5)
    }

    function test_unit_field_commits_one_binding_transaction_and_undoes() {
        const fixture = inspectorFixtures.create()
        const field = createTemporaryObject(unitComponent, root)
        field.binding = fixture.unitBinding
        field.unitScale = 25.4
        verify(field.edit(3))
        compare(fixture.document.valueFor(fixture.firstId, "length"), 76.2)
        compare(fixture.document.valueFor(fixture.secondId, "length"), 76.2)
        verify(fixture.undoService.canUndo)
        verify(fixture.undoService.undo())
        compare(fixture.document.valueFor(fixture.firstId, "length"), 25.4)
        compare(fixture.document.valueFor(fixture.secondId, "length"), 50.8)
    }

    function test_vector_component_patch_preserves_mixed_axes_and_undoes_once() {
        const fixture = inspectorFixtures.create()
        const field = createTemporaryObject(vectorComponent, root)
        field.binding = fixture.vectorBinding
        verify(fixture.vectorBinding.mixed)
        verify(field.editComponent(1, 9))
        const first = fixture.document.valueFor(fixture.firstId, "position")
        const second = fixture.document.valueFor(fixture.secondId, "position")
        compare(first[0], 1); compare(first[1], 9); compare(first[2], 3)
        compare(second[0], 4); compare(second[1], 9); compare(second[2], 6)
        verify(fixture.undoService.canUndo)
        verify(fixture.undoService.undo())
        const undoneFirst = fixture.document.valueFor(fixture.firstId, "position")
        const undoneSecond = fixture.document.valueFor(fixture.secondId, "position")
        compare(undoneFirst[0], 1); compare(undoneFirst[1], 2); compare(undoneFirst[2], 3)
        compare(undoneSecond[0], 4); compare(undoneSecond[1], 5); compare(undoneSecond[2], 6)
    }

    function test_mixed_reset_is_explicit_and_readonly_without_binding() {
        const fixture = inspectorFixtures.create()
        const field = createTemporaryObject(mixedComponent, root)
        field.binding = fixture.unitBinding
        verify(field.mixed)
        verify(!field.readOnly)
        verify(field.reset())
        compare(fixture.document.valueFor(fixture.firstId, "length"), 0)
        compare(fixture.document.valueFor(fixture.secondId, "length"), 0)
        verify(fixture.undoService.undo())
        compare(fixture.document.valueFor(fixture.firstId, "length"), 25.4)
        compare(fixture.document.valueFor(fixture.secondId, "length"), 50.8)

        const unbound = createTemporaryObject(mixedComponent, root)
        verify(unbound.readOnly)
        verify(!unbound.reset())
    }
}
