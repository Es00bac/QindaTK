// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    id: root
    width: 1280
    height: 620
    color: Tk.Theme.color.bg

    Tk.PolarDataModel {
        id: wind
        Component.onCompleted: replacePoints([
            { seriesId: "wind", seriesName: "Wind speed", pointId: "n", angle: 0,
              radius: 4, kind: Tk.PolarDataModel.Polyline },
            { seriesId: "wind", seriesName: "Wind speed", pointId: "ne", angle: Math.PI / 4,
              radius: 6, kind: Tk.PolarDataModel.Polyline },
            { seriesId: "wind", seriesName: "Wind speed", pointId: "e", angle: Math.PI / 2,
              radius: 8, kind: Tk.PolarDataModel.Polyline },
            { seriesId: "wind", seriesName: "Wind speed", pointId: "s", angle: Math.PI,
              radius: 5, kind: Tk.PolarDataModel.Polyline },
            { seriesId: "wind", seriesName: "Wind speed", pointId: "w", angle: 3 * Math.PI / 2,
              radius: 7, kind: Tk.PolarDataModel.Polyline }
        ])
    }
    Tk.PolarDataModel {
        id: radarData
        Component.onCompleted: replacePoints([
            { seriesId: "planned", seriesName: "Planned", pointId: "p-quality", category: "Quality",
              radius: 8, kind: Tk.PolarDataModel.Polygon },
            { seriesId: "planned", seriesName: "Planned", pointId: "p-speed", category: "Speed",
              radius: 7, kind: Tk.PolarDataModel.Polygon },
            { seriesId: "planned", seriesName: "Planned", pointId: "p-cost", category: "Cost",
              radius: 5, kind: Tk.PolarDataModel.Polygon },
            { seriesId: "planned", seriesName: "Planned", pointId: "p-risk", category: "Risk",
              radius: 6, kind: Tk.PolarDataModel.Polygon },
            { seriesId: "actual", seriesName: "Actual", pointId: "a-quality", category: "Quality",
              radius: 7, kind: Tk.PolarDataModel.Polygon },
            { seriesId: "actual", seriesName: "Actual", pointId: "a-speed", category: "Speed",
              radius: 5, kind: Tk.PolarDataModel.Polygon },
            { seriesId: "actual", seriesName: "Actual", pointId: "a-cost", category: "Cost",
              radius: 7, kind: Tk.PolarDataModel.Polygon },
            { seriesId: "actual", seriesName: "Actual", pointId: "a-risk", category: "Risk",
              radius: 4, kind: Tk.PolarDataModel.Polygon }
        ])
    }
    Tk.PolarDataModel {
        id: radialData
        Component.onCompleted: replacePoints([
            { seriesId: "allocation", seriesName: "Allocation", pointId: "north", category: "North",
              radius: 0.6, kind: Tk.PolarDataModel.RadialBar },
            { seriesId: "allocation", seriesName: "Allocation", pointId: "east", category: "East",
              radius: 0.8, kind: Tk.PolarDataModel.RadialBar },
            { seriesId: "allocation", seriesName: "Allocation", pointId: "south", category: "South",
              radius: 0.4, kind: Tk.PolarDataModel.RadialBar },
            { seriesId: "allocation", seriesName: "Allocation", pointId: "west", category: "West",
              radius: 0.7, kind: Tk.PolarDataModel.RadialBar }
        ])
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.sm
            Tk.Label { text: "Polar, radar and radial coordinates"; font.pixelSize: Tk.Theme.font.large }
            Tk.Button {
                text: radar.showDataTable ? "Show radar" : "Show radar data"
                onClicked: radar.showDataTable = !radar.showDataTable
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Flex.grow: 1
            Tk.PolarView {
                objectName: "examplePolar"
                model: wind
                angleMode: Tk.PolarRenderer.Numeric
                angleUnit: Tk.PolarRenderer.Radians
                radialMinimum: 0
                radialMaximum: 10
                radialUnit: "m/s"
                tooltip: "Wind polar line, radians and metres per second"
                Tk.Flex.flex: 1
            }
            Tk.PolarView {
                id: radar
                objectName: "exampleRadar"
                model: radarData
                angleMode: Tk.PolarRenderer.Category
                categories: ["Quality", "Speed", "Cost", "Risk"]
                radialMaximum: 10
                radialUnit: "score"
                tooltip: "Planned and actual radar scores"
                Tk.Flex.flex: 1
            }
            Tk.PolarView {
                objectName: "exampleRadial"
                model: radialData
                angleMode: Tk.PolarRenderer.Category
                categories: ["North", "East", "South", "West"]
                radialMaximum: 1
                radialUnit: "fraction"
                tooltip: "Directional resource allocation"
                Tk.Flex.flex: 1
            }
        }
    }
}
