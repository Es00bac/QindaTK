// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Page geometry is canonical millimetres. This dialog edits a local draft and
// submits one complete proposal; the host validates printer/document limits.
Tk.Dialog {
    id: dialog
    required property var pageSetup // {widthMm,heightMm,unit,orientation,scope,marginsMm,gutterMm,columns,bleedMm}
    property string tooltip: qsTr("Page size, margins, columns and section scope")
    property var draft: pageSetup ? copySettings(pageSetup) : ({})
    readonly property string validationMessage: validateDraft()
    readonly property bool validDraft: validationMessage.length === 0

    signal pageSetupRequested(var settings)
    signal pageSetupCancelled()

    title: qsTr("Page setup")
    subtitle: qsTr("Physical dimensions are stored in millimetres.")
    dialogWidth: Tk.Theme.size.dialogWidth
    primaryText: qsTr("Apply")
    secondaryText: qsTr("Cancel")
    primaryEnabled: validDraft

    function copySettings(source) {
        const value = Object.assign({}, source)
        value.marginsMm = Object.assign({}, source.marginsMm || {})
        return value
    }
    function displayValue(mm) {
        if (draft.unit === "in") return mm / 25.4
        if (draft.unit === "pt") return mm * 72 / 25.4
        return mm
    }
    function canonicalValue(value) {
        if (draft.unit === "in") return value * 25.4
        if (draft.unit === "pt") return value * 25.4 / 72
        return value
    }
    function setDimension(key, value) {
        if (!draft || !Object.keys(draft).length) return
        const next = copySettings(draft)
        next[key] = canonicalValue(value)
        draft = next
    }
    function setMargin(side, value) {
        if (!draft || !Object.keys(draft).length) return
        const next = copySettings(draft)
        next.marginsMm[side] = canonicalValue(value)
        draft = next
    }
    function setLayout(key, value) {
        if (!draft || !Object.keys(draft).length) return
        const next = copySettings(draft)
        next[key] = value
        draft = next
    }
    function validateDraft() {
        if (!draft || !Object.keys(draft).length) return qsTr("Page geometry is not available.")
        const margins = draft.marginsMm || {}
        const width = Number(draft.widthMm), height = Number(draft.heightMm)
        if (!isFinite(width) || !isFinite(height) || width <= 0 || height <= 0)
            return qsTr("Page width and height must be positive.")
        if (["top", "right", "bottom", "left"].some(function(side) {
            return !isFinite(Number(margins[side])) || Number(margins[side]) < 0
        })) return qsTr("Margins must be finite and non-negative.")
        if (Number(margins.left) + Number(margins.right) >= width
                || Number(margins.top) + Number(margins.bottom) >= height)
            return qsTr("Margins must leave printable content on the page.")
        if (!isFinite(Number(draft.bleedMm)) || Number(draft.bleedMm) < 0
                || !isFinite(Number(draft.gutterMm)) || Number(draft.gutterMm) < 0)
            return qsTr("Bleed and column gap must be non-negative.")
        if (Math.floor(Number(draft.columns)) !== Number(draft.columns) || Number(draft.columns) < 1 || Number(draft.columns) > 24)
            return qsTr("Choose between 1 and 24 columns.")
        return ""
    }

    onOpened: draft = copySettings(pageSetup)
    onRejected: pageSetupCancelled()
    onAccepted: if (validDraft) pageSetupRequested(copySettings(draft))

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md

        Tk.Grid {
            columns: "1fr 1fr"
            gap: Tk.Theme.space.sm
            columnGap: Tk.Theme.space.md

            Tk.PropertyRow {
                label: qsTr("Width")
                Tk.NumberField {
                    objectName: "pageWidth"
                    value: dialog.displayValue(dialog.draft.widthMm || 0)
                    from: 0.01
                    to: 100000
                    stepSize: 1
                    decimals: dialog.draft.unit === "in" ? 2 : 1
                    suffix: dialog.draft.unit || "mm"
                    tooltip: qsTr("Physical page width")
                    onValueModified: function(value) { dialog.setDimension("widthMm", value) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Height")
                Tk.NumberField {
                    objectName: "pageHeight"
                    value: dialog.displayValue(dialog.draft.heightMm || 0)
                    from: 0.01
                    to: 100000
                    stepSize: 1
                    decimals: dialog.draft.unit === "in" ? 2 : 1
                    suffix: dialog.draft.unit || "mm"
                    tooltip: qsTr("Physical page height")
                    onValueModified: function(value) { dialog.setDimension("heightMm", value) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Units")
                Tk.ComboBox {
                    objectName: "pageUnits"
                    model: ["mm", "in", "pt"]
                    currentIndex: Math.max(0, model.indexOf(dialog.draft.unit || "mm"))
                    tooltip: qsTr("Display units for physical dimensions")
                    onActivated: function(index) { dialog.setLayout("unit", model[index]) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Scope")
                Tk.ComboBox {
                    objectName: "pageScope"
                    model: ["document", "section", "page"]
                    currentIndex: Math.max(0, model.indexOf(dialog.draft.scope || "document"))
                    tooltip: qsTr("Apply page geometry at this document scope")
                    onActivated: function(index) { dialog.setLayout("scope", model[index]) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Columns")
                Tk.NumberField {
                    objectName: "pageColumns"
                    value: dialog.draft.columns || 1
                    from: 1
                    to: 24
                    stepSize: 1
                    decimals: 0
                    tooltip: qsTr("Text columns")
                    onValueModified: function(value) { dialog.setLayout("columns", value) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Column gap")
                Tk.NumberField {
                    objectName: "pageGutter"
                    value: dialog.displayValue(dialog.draft.gutterMm || 0)
                    from: 0
                    to: 100000
                    stepSize: 1
                    decimals: dialog.draft.unit === "in" ? 2 : 1
                    suffix: dialog.draft.unit || "mm"
                    tooltip: qsTr("Space between text columns")
                    onValueModified: function(value) { dialog.setDimension("gutterMm", value) }
                }
            }
            Tk.PropertyRow {
                label: qsTr("Bleed")
                Tk.NumberField {
                    objectName: "pageBleed"
                    value: dialog.displayValue(dialog.draft.bleedMm || 0)
                    from: 0
                    to: 100000
                    stepSize: 0.5
                    decimals: 1
                    suffix: dialog.draft.unit || "mm"
                    tooltip: qsTr("Page bleed allowance")
                    onValueModified: function(value) { dialog.setDimension("bleedMm", value) }
                }
            }
        }
        Tk.PropertyRow {
            label: qsTr("Orientation")
            Tk.Segmented {
                objectName: "pageOrientation"
                model: ["portrait", "landscape"]
                currentIndex: Math.max(0, model.indexOf(dialog.draft.orientation || "portrait"))
                tooltip: qsTr("Page orientation")
                onActivated: function(index) { dialog.setLayout("orientation", model[index]) }
            }
        }
        Tk.Overline { title: qsTr("MARGINS") }
        Tk.Grid {
            columns: "1fr 1fr"
            gap: Tk.Theme.space.sm
            columnGap: Tk.Theme.space.md
            Repeater {
                model: ["top", "right", "bottom", "left"]
                delegate: Tk.PropertyRow {
                    required property string modelData
                    label: modelData.charAt(0).toUpperCase() + modelData.slice(1)
                    Tk.NumberField {
                        objectName: "pageMargin_" + modelData
                        value: dialog.displayValue((dialog.draft.marginsMm || ({}))[modelData] || 0)
                        from: 0
                        to: 100000
                        stepSize: 1
                        decimals: dialog.draft.unit === "in" ? 2 : 1
                        suffix: dialog.draft.unit || "mm"
                        tooltip: qsTr("%1 page margin").arg(modelData)
                        onValueModified: function(value) { dialog.setMargin(modelData, value) }
                    }
                }
            }
        }
        Tk.Caption {
            objectName: "pageSetupValidation"
            visible: !dialog.validDraft
            color: Tk.Theme.color.danger
            text: dialog.validationMessage
            wrapMode: Text.WordWrap
        }
    }
}
