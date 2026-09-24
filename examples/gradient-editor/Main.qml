// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    width: 520
    height: 190
    Tk.GradientEditorModel { id: gradientModel }

    Tk.GradientEditor {
        objectName: "gradientEditor"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Tk.Theme.space.lg
        stops: gradientModel.stops
        previewSamples: gradientModel.previewSamples
        selectedStopId: gradientModel.selectedStopId
        tooltip: qsTr("Color gradient stops")
        onSelectionRequested: function(id) { gradientModel.selectStop(String(id)) }
        onAddStopRequested: function(position) { gradientModel.addStop(position) }
        onMovePreviewRequested: function(id, position) {
            if (!gradientModel.editing) gradientModel.beginGesture("Move gradient stop")
            gradientModel.previewMove(String(id), position)
        }
        onMoveFinished: function() { gradientModel.commitGesture() }
        onMoveCanceled: function() { gradientModel.cancelGesture() }
        onDeleteStopRequested: function(id) { gradientModel.deleteStop(String(id)) }
    }
}
