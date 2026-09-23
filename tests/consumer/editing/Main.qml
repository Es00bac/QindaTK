// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: root

    property alias projectedValue: gainBinding.value
    property alias projectedMixed: gainBinding.mixed
    property alias projectedWritable: gainBinding.writable
    property alias selectedIds: selection.selectedIds

    function editGain(value) {
        return gainBinding.begin()
                && gainBinding.update(value)
                && gainBinding.commit()
    }

    function selectEntity(id) {
        return selection.select(id)
    }

    Tk.PropertyBinding {
        id: gainBinding
        document: appDocument
        undoService: appUndo
        propertyId: "gain"
        targetIds: [appEntityId]
    }

    Tk.SelectionModel {
        id: selection
        objectName: "selection"
    }

    Tk.ValidationModel {
        objectName: "validation"
    }
}
