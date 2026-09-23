// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Polar and radar"
    note: "PolarView · PolarDataModel · ordered angular axis"

    Tk.PolarDataModel {
        id: points
        Component.onCompleted: replacePoints([
            { seriesId: "profile", seriesName: "Profile", pointId: "a", category: "A",
              radius: 0.8, kind: Tk.PolarDataModel.Polygon },
            { seriesId: "profile", seriesName: "Profile", pointId: "b", category: "B",
              radius: 0.5, kind: Tk.PolarDataModel.Polygon },
            { seriesId: "profile", seriesName: "Profile", pointId: "c", category: "C",
              radius: 0.7, kind: Tk.PolarDataModel.Polygon },
            { seriesId: "profile", seriesName: "Profile", pointId: "d", category: "D",
              radius: 0.4, kind: Tk.PolarDataModel.Polygon }
        ])
    }
    GalleryRow {
        label: "Radar"
        Tk.PolarView {
            objectName: "galleryPolarView"
            implicitWidth: 320
            implicitHeight: 260
            model: points
            angleMode: Tk.PolarRenderer.Category
            categories: ["A", "B", "C", "D"]
            radialMaximum: 1
            radialUnit: "fraction"
            tooltip: "Four-axis radar profile"
        }
    }
}
