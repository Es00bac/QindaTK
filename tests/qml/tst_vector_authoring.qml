// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "VectorAuthoring"
    when: windowShown
    visible: true
    width: 440
    height: 320

    Component {
        id: paletteComponent
        Tk.ShapePalette {
            width: 360
            definitions: [
                { id: "ellipse", label: "Ellipse", category: "Basic", iconName: "circle", parameters: { startAngle: 0, sweepAngle: 360 } }
            ]
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    function test_shape_button_requests_one_stable_id_and_parameter_copy() {
        const palette = createTemporaryObject(paletteComponent, this)
        verify(palette !== null)
        const requested = createTemporaryObject(spyComponent, this,
            { target: palette, signalName: "shapeRequested" })
        const button = findChild(palette, "shape_ellipse")
        verify(button !== null)

        mouseClick(button)

        compare(requested.count, 1)
        compare(requested.signalArguments[0][0], "ellipse")
        compare(requested.signalArguments[0][1].sweepAngle, 360)
        compare(palette.recentIds[0], "ellipse")
    }
}
