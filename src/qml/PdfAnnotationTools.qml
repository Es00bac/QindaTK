// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Annotation identity, geometry, moderation and persistence are host-owned.
// Redaction here means a proposal only; final redaction is a separate job.
Item {
    id: control
    property string documentId: ""
    property int pageIndex: 0
    property string selectedText: ""
    property var selectedBounds: null // renderer page coordinates {x,y,width,height}
    property var annotations: [] // [{id,pageIndex,kind,text,author,status,bounds,error,canAccept,canReject}]
    property var capabilities: ({ annotationKinds: [], canAccept: false, canReject: false,
                                  canEditForms: false, canApplyRedaction: false })
    property string selectedAnnotationId: ""
    property string activeTool: "highlight"
    property string noteDraft: ""
    property string errorText: ""
    property string tooltip: qsTr("Create and review PDF annotation requests")
    property int requestSerial: 1
    readonly property var selectedAnnotation: annotationById(selectedAnnotationId)
    readonly property var availableTools: capabilities.annotationKinds || []
    readonly property bool canCreate: enabled && documentId.length > 0
                                      && availableTools.indexOf(activeTool) >= 0
                                      && selectedBounds !== null
    signal toolChangeRequested(string kind)
    signal annotationCreateRequested(string requestId, int pageIndex, string kind, var bounds,
                                     string selectedText, string note)
    signal annotationSelectionRequested(string annotationId)
    signal annotationNavigateRequested(string annotationId, int pageIndex)
    signal annotationAcceptRequested(string annotationId)
    signal annotationRejectRequested(string annotationId)
    signal annotationDeleteRequested(string annotationId)

    implicitWidth: Tk.Theme.size.panelMinWidth * 2
    implicitHeight: body.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: tooltip

    function annotationById(id) {
        for (let i = 0; i < annotations.length; ++i)
            if (annotations[i].id === id) return annotations[i]
        return null
    }
    function selectAnnotation(id) {
        if (!enabled || !annotationById(id)) return
        annotationSelectionRequested(id)
    }
    function createAnnotation() {
        if (!canCreate) return
        const requestId = documentId + ":annotation:" + requestSerial++
        annotationCreateRequested(requestId, pageIndex, activeTool,
                                  Object.assign({}, selectedBounds), selectedText, noteDraft.trim())
    }
    function navigateAnnotation() {
        if (enabled && selectedAnnotation)
            annotationNavigateRequested(selectedAnnotation.id, selectedAnnotation.pageIndex)
    }

    Tk.Flex {
        id: body
        width: control.width > 0 ? control.width : implicitWidth
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        gap: Tk.Theme.space.sm

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Center
            Tk.Overline { title: qsTr("ANNOTATIONS"); Tk.Flex.grow: 1; Tk.Flex.basis: 0 }
            Tk.Caption { text: qsTr("Page %1").arg(control.pageIndex + 1); mono: true }
        }
        Tk.Caption {
            objectName: "pdfAnnotationError"
            visible: control.errorText.length > 0
            color: Tk.Theme.color.danger
            text: control.errorText
            wrapMode: Text.WordWrap
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.xs
            Tk.ComboBox {
                objectName: "pdfAnnotationTool"
                model: control.availableTools.map(function(kind) {
                    return { id: kind, label: kind === "redaction-proposal" ? qsTr("Redaction proposal") : kind }
                })
                textRole: "label"
                currentIndex: {
                    for (let i = 0; i < model.length; ++i) if (model[i].id === control.activeTool) return i
                    return -1
                }
                placeholderText: qsTr("No annotation tools")
                tooltip: qsTr("Choose an annotation request type")
                enabled: model.length > 0
                onActivated: function(index) {
                    const kind = model[index].id
                    control.toolChangeRequested(kind)
                }
                Tk.Flex.grow: 1
                Tk.Flex.basis: 0
            }
            Tk.Caption { text: qsTr("%1 selected").arg(control.selectedText.length); mono: true }
        }
        Tk.Caption {
            objectName: "pdfAnnotationSelection"
            visible: control.selectedText.length > 0
            text: qsTr("Selected: %1").arg(control.selectedText)
            wrapMode: Text.WordWrap
        }
        Tk.TextField {
            objectName: "pdfAnnotationNote"
            text: control.noteDraft
            placeholderText: qsTr("Optional note text")
            tooltip: qsTr("Text for a note annotation")
            onTextEdited: control.noteDraft = text
            visible: control.activeTool === "note"
        }
        Tk.Button {
            objectName: "pdfAnnotationCreate"
            text: control.activeTool === "redaction-proposal" ? qsTr("Propose redaction") : qsTr("Add annotation")
            iconName: control.activeTool === "redaction-proposal" ? "eye-off" : "plus"
            variant: "accent"
            tooltip: qsTr("Send selected PDF page geometry to the annotation host")
            enabled: control.canCreate
            onClicked: control.createAnnotation()
        }
        Tk.Notice {
            objectName: "pdfRedactionNotice"
            visible: control.activeTool === "redaction-proposal" || !control.capabilities.canApplyRedaction
            variant: "warning"
            text: control.activeTool === "redaction-proposal"
                  ? qsTr("This creates a reviewable proposal only; it does not remove underlying PDF content.")
                  : qsTr("Final redaction is unavailable in this host. OCR and signature operations use separate workflows.")
        }
        Tk.Notice {
            objectName: "pdfFormCapabilityNotice"
            visible: !control.capabilities.canEditForms
            variant: "info"
            text: qsTr("Interactive PDF form editing is unavailable.")
        }
        Tk.Scroll {
            objectName: "pdfAnnotationListScroll"
            implicitHeight: Math.min(annotationList.implicitHeight, Tk.Theme.size.menuItem * 8)
            overflowY: Tk.Scroll.Auto
            Tk.Flex {
                id: annotationList
                direction: Tk.Flex.Column
                align: Tk.Flex.Stretch
                gap: Tk.Theme.space.xs
                Repeater {
                    model: control.annotations
                    delegate: Tk.Button {
                        required property var modelData
                        objectName: "pdfAnnotation_" + modelData.id
                        text: qsTr("%1 · page %2 · %3").arg(modelData.kind).arg(modelData.pageIndex + 1).arg(modelData.text || modelData.author || "")
                        tooltip: qsTr("Select annotation %1 (%2)").arg(modelData.id).arg(modelData.status)
                        variant: modelData.id === control.selectedAnnotationId ? "outline" : "ghost"
                        onClicked: control.selectAnnotation(modelData.id)
                    }
                }
            }
        }
        Tk.Caption {
            objectName: "pdfAnnotationStatus"
            visible: control.selectedAnnotation !== null
            text: control.selectedAnnotation
                  ? qsTr("%1 · %2 · %3").arg(control.selectedAnnotation.id)
                    .arg(control.selectedAnnotation.status).arg(control.selectedAnnotation.error || "") : ""
            color: control.selectedAnnotation && control.selectedAnnotation.status === "rejected"
                   ? Tk.Theme.color.danger : Tk.Theme.color.textMuted
            wrapMode: Text.WordWrap
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            wrap: Tk.Flex.Wrap
            gap: Tk.Theme.space.xs
            Tk.Button {
                objectName: "pdfAnnotationNavigate"
                text: qsTr("Go to annotation")
                iconName: "navigation"
                tooltip: qsTr("Navigate to this annotation on its PDF page")
                enabled: control.selectedAnnotation !== null
                onClicked: control.navigateAnnotation()
            }
            Tk.Button {
                objectName: "pdfAnnotationAccept"
                text: qsTr("Accept")
                iconName: "check"
                variant: "accent"
                tooltip: qsTr("Request acceptance of this pending annotation")
                enabled: control.selectedAnnotation && control.selectedAnnotation.status === "pending"
                         && control.selectedAnnotation.canAccept !== false && control.capabilities.canAccept
                onClicked: control.annotationAcceptRequested(control.selectedAnnotationId)
            }
            Tk.Button {
                objectName: "pdfAnnotationReject"
                text: qsTr("Reject")
                iconName: "x"
                variant: "danger"
                tooltip: qsTr("Request rejection of this pending annotation")
                enabled: control.selectedAnnotation && control.selectedAnnotation.status === "pending"
                         && control.selectedAnnotation.canReject !== false && control.capabilities.canReject
                onClicked: control.annotationRejectRequested(control.selectedAnnotationId)
            }
            Tk.Button {
                objectName: "pdfAnnotationDelete"
                text: qsTr("Delete")
                iconName: "trash-2"
                variant: "danger"
                tooltip: qsTr("Request removal of this annotation")
                enabled: control.selectedAnnotation !== null
                         && (control.selectedAnnotation.status === "accepted"
                             || control.selectedAnnotation.status === "in-document")
                         && control.capabilities.canDelete !== false
                onClicked: control.annotationDeleteRequested(control.selectedAnnotationId)
            }
        }
        Tk.Caption {
            objectName: "pdfAnnotationUnsupported"
            visible: control.availableTools.length === 0
            color: Tk.Theme.color.warning
            text: qsTr("Annotation editing is unavailable for this PDF provider.")
            wrapMode: Text.WordWrap
        }
    }
}
