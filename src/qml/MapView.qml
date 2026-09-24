// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Local WGS84 GeoJSON vector map over optional local XYZ raster tiles.
Item {
    id: root
    property alias model: canvas.model
    property alias centerLongitude: canvas.centerLongitude
    property alias centerLatitude: canvas.centerLatitude
    property alias zoomLevel: canvas.zoomLevel
    property alias tileSource: canvas.tileSource
    readonly property string selectedFeatureId: canvas.selectedFeatureId
    readonly property var selectedFeature: model ? model.featureById(selectedFeatureId) : ({})
    readonly property string tileStatus: canvas.tileStatus
    property string tooltip: qsTr("Geographic map")
    signal featureSelected(string id)
    function attributesText() {
        var values = selectedFeature.properties || {}
        return Object.keys(values).map(function(key) { return key + ": " + values[key] }).join("\n")
    }
    function loadGeoJson(path) { return model.loadGeoJson(path) }
    function saveGeoJson(path) { return model.saveGeoJson(path) }
    function loadTileManifest(path) { return tileSource ? tileSource.loadManifest(path) : false }
    function useNetworkTiles(urlTemplate, attribution, minZoom, maxZoom, tileSize) {
        return tileSource ? tileSource.useNetworkTemplate(urlTemplate, attribution,
            minZoom === undefined ? 0 : minZoom,
            maxZoom === undefined ? 18 : maxZoom,
            tileSize === undefined ? 256 : tileSize) : false
    }
    function coordinateAt(x, y) { return canvas.coordinateAt(x, y) }
    function screenPosition(longitude, latitude) { return canvas.screenPosition(longitude, latitude) }
    function featureIdAt(x, y) { return canvas.featureIdAt(x, y) }
    function selectAt(x, y) { return canvas.selectAt(x, y) }
    function selectFeature(id) { return canvas.selectFeature(id) }
    function panPixels(dx, dy) { canvas.panPixels(dx, dy) }
    function zoomAt(factor, x, y) { canvas.zoomAt(factor, x, y) }

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    objectName: "mapView"
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: qsTr("WGS84 local map. %1 features. %2. %3. Selected: %4")
        .arg(model ? model.count : 0).arg(model ? model.attribution : "")
        .arg(tileStatus).arg(selectedFeatureId || qsTr("none"))

    Tk.GeographicFeatureModel { id: localFeatures }
    Tk.GeographicTileSource { id: localTiles }
    Tk.GeographicMapCanvas {
        id: canvas
        objectName: "mapCanvas"
        anchors.fill: parent
        model: localFeatures
        tileSource: localTiles
        backgroundColor: Tk.Theme.color.canvas
        featureColor: Tk.Theme.color.accent
        selectionColor: Tk.Theme.color.focus
        gridColor: Tk.Theme.color.divider
        pointRadius: Tk.Theme.size.iconSm / 3
        lineWidth: Tk.Theme.size.focusRing
        gridWidth: Tk.Theme.size.border
        hitRadius: Tk.Theme.space.xs
        fillOpacity: Tk.Theme.opacity.disabled
        selectedFillOpacity: Tk.Theme.opacity.island
        Accessible.role: Accessible.Graphic
        Accessible.name: root.tooltip
        Accessible.description: root.Accessible.description
        onFeatureSelected: function(id) { root.featureSelected(id) }
    }
    Rectangle {
        objectName: "mapTileStatus"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: Tk.Theme.space.sm
        width: tileLabel.implicitWidth + Tk.Theme.space.sm * 2
        height: tileLabel.implicitHeight + Tk.Theme.space.xs * 2
        color: Tk.Theme.color.panel
        radius: Tk.Theme.radius.sm
        Tk.Label {
            id: tileLabel
            anchors.centerIn: parent
            muted: true
            text: root.tileStatus === "unavailable" ? qsTr("Basemap tiles unavailable · offline vectors")
                                                     : qsTr("Local tiles · %1").arg(root.tileStatus)
            font.pixelSize: Tk.Theme.font.caption
        }
    }
    Rectangle {
        objectName: "mapAttribution"
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: Tk.Theme.space.sm
        width: attributionLabel.implicitWidth + Tk.Theme.space.sm * 2
        height: attributionLabel.implicitHeight + Tk.Theme.space.xs * 2
        color: Tk.Theme.color.panel
        radius: Tk.Theme.radius.sm
        Tk.Label {
            id: attributionLabel
            anchors.centerIn: parent
            muted: true
            text: [model && model.attribution ? model.attribution : "",
                   root.tileSource && root.tileSource.attribution ? root.tileSource.attribution : ""].filter(Boolean).join(" · ")
                  || qsTr("Local source attribution unavailable")
            font.pixelSize: Tk.Theme.font.caption
        }
    }
    Rectangle {
        objectName: "mapInspector"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Tk.Theme.space.sm
        width: Math.min(parent.width / 2, Tk.Theme.size.panelMinWidth + Tk.Theme.size.fieldWidth)
        height: inspectorColumn.implicitHeight + Tk.Theme.space.sm * 2
        color: Tk.Theme.color.panel
        radius: Tk.Theme.radius.sm
        visible: selectedFeatureId.length > 0
        Tk.Flex {
            id: inspectorColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Tk.Theme.space.sm
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Label { text: root.selectedFeatureId; accent: true; font.pixelSize: Tk.Theme.font.body }
            Tk.Label { text: root.selectedFeature.type || ""; muted: true }
            Tk.Label {
                text: root.attributesText()
                wrapMode: Text.WrapAnywhere
                width: inspectorColumn.width
                font.pixelSize: Tk.Theme.font.caption
            }
        }
    }
}
