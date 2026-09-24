// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    width: 720
    height: 420
    Tk.CurveEditorModel { id: curveModel }
    Tk.CurveEditor {
        id: editor
        anchors.fill: parent
        knots: curveModel.knots
        samples: curveModel.samples
        onMovePreview: function(id, x, y) {
            if (!curveModel.editing) curveModel.beginKnotEdit(id)
            curveModel.previewKnot(id, x, y)
        }
        onMoveFinished: curveModel.commitEdit()
        onMoveCancelled: curveModel.cancelEdit()
        onTangentPreviewRequested: function(id, side, x, y) {
            if (!curveModel.editing) curveModel.beginTangentEdit(id, side)
            curveModel.previewTangent(id, side, x, y)
        }
        onTangentEditFinished: curveModel.commitEdit()
        onTangentEditCanceled: curveModel.cancelEdit()
    }
}
