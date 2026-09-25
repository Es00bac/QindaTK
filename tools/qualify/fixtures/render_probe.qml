// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-088 render probe (tools/qualify/qualify_checks.py check_render).
// AGENT-CONTRACT: the fill colour is PROBE_RGB in qualify_checks.py, and the
// log line "QTK_QUALIFY api=<name> platform=<name> active=<bool>" is parsed
// there. The probe requests activation itself, so active=false means the
// display never activates windows. The literal colour is demo data: it must
// not follow the theme.
import QtQuick
import QindaTK as Tk

Rectangle {
    id: root
    objectName: "renderProbe"
    width: 320
    height: 200
    color: "#12a4d6"

    function apiName(api) {
        switch (api) {
        case GraphicsInfo.Software: return "software"
        case GraphicsInfo.OpenGL: return "opengl"
        case GraphicsInfo.Vulkan: return "vulkan"
        case GraphicsInfo.Direct3D11: return "d3d11"
        case GraphicsInfo.Direct3D12: return "d3d12"
        case GraphicsInfo.Metal: return "metal"
        case GraphicsInfo.OpenVG: return "openvg"
        case GraphicsInfo.Null: return "null"
        default: return "unknown"
        }
    }

    Tk.Label {
        objectName: "renderProbeLabel"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: Tk.Theme.space.sm
        text: "QindaTK render probe"
    }

    Timer {
        interval: 300
        running: true
        onTriggered: if (root.Window.window) root.Window.window.requestActivate()
    }

    Timer {
        interval: 1500
        running: true
        onTriggered: console.log("QTK_QUALIFY api=" + root.apiName(root.GraphicsInfo.api)
                                 + " platform=" + Qt.platform.pluginName
                                 + " active=" + root.Window.active)
    }
}
