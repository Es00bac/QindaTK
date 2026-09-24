// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    width: 960
    height: 640
    color: Tk.Theme.color.bg
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.md
        gap: Tk.Theme.space.sm
        Tk.Label { text: qsTr("Front Range · local GeoJSON"); font.pixelSize: Tk.Theme.font.title }
        Tk.MapView {
            id: map
            objectName: "fixtureMap"
            centerLongitude: -105
            centerLatitude: 39.8
            zoomLevel: 9
            Tk.Flex.flex: 1
            Component.onCompleted: {
                loadGeoJson(Qt.resolvedUrl("fixtures.geojson"))
                selectFeature("denver")
            }
        }
        Tk.Label { text: qsTr("EPSG:4326 coordinates · Web Mercator display · local vectors only"); muted: true }
    }
}
