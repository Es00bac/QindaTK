// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Numerical dimensions are diagnostic sample data, independent of density.
Rectangle {
    width: 360; height: 180
    color: Tk.Theme.color.canvas
    Tk.Label { x: 20; y: 8; text: "auto-fill: retain empty slot" }
    Tk.Grid {
        objectName: "fillGrid"
        x: 20; y: 30; width: 320; height: 40
        columns: "repeat(auto-fill, minmax(100, 1fr))"
        rows: "40"; columnGap: 10
        Rectangle { objectName: "fillA"; color: Tk.Theme.color.accent }
        Rectangle { objectName: "fillB"; color: Tk.Theme.color.panelAlt }
    }
    Tk.Label { x: 20; y: 88; text: "auto-fit: stretch occupied slots" }
    Tk.Grid {
        objectName: "fitGrid"
        x: 20; y: 110; width: 320; height: 40
        columns: "repeat(auto-fit, minmax(100, 1fr))"
        rows: "40"; columnGap: 10
        Rectangle { objectName: "fitA"; color: Tk.Theme.color.accent }
        Rectangle { objectName: "fitB"; color: Tk.Theme.color.panelAlt }
    }
}
