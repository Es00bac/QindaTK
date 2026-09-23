// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: root
    width: 320
    height: 120

    property alias capabilityAvailable: exportBinding.available
    property alias capabilityReason: exportBinding.reason
    property alias capabilityBackend: exportBinding.backendId
    property alias capabilityVersion: exportBinding.backendVersion
    property alias exportEnabled: exportButton.enabled
    property alias exportTooltip: exportButton.tooltip
    property string revisionText: snapshotWire.revision

    Tk.CapabilityBinding {
        id: exportBinding
        registry: appCapabilities
        featureId: "document.pdf"
        operationId: "export"
    }

    Tk.Button {
        id: exportButton
        text: "Export PDF"
        enabled: exportBinding.available
        tooltip: exportBinding.available ? text : exportBinding.reason
    }
}
