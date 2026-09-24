// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Asset dependencies"
    note: "Stable inputs, missing links, cycles and impact"
    Tk.AssetDependencyIndex { id: graph }
    Component.onCompleted: {
        graph.upsertAsset("qml/Main.qml", "Main.qml", "project/qml/Main.qml", true, "qml")
        graph.upsertAsset("qml/Panel.qml", "Panel.qml", "project/qml/Panel.qml", true, "qml")
        graph.upsertAsset("media/logo.svg", "logo.svg", "project/media/logo.svg", true, "image")
        graph.upsertAsset("media/intro.wav", "intro.wav", "project/media/intro.wav", false, "audio")
        graph.upsertAsset("qml/CycleA.qml", "CycleA.qml", "project/qml/CycleA.qml", true, "qml")
        graph.upsertAsset("qml/CycleB.qml", "CycleB.qml", "project/qml/CycleB.qml", true, "qml")
        graph.commitScan("qml/Main.qml", graph.beginScan("qml/Main.qml"),
                         ["qml/Panel.qml", "media/logo.svg", "media/intro.wav"])
        graph.commitScan("qml/Panel.qml", graph.beginScan("qml/Panel.qml"), ["media/logo.svg"])
        graph.commitScan("qml/CycleA.qml", graph.beginScan("qml/CycleA.qml"), ["qml/CycleB.qml"])
        graph.commitScan("qml/CycleB.qml", graph.beginScan("qml/CycleB.qml"), ["qml/CycleA.qml"])
    }
    Tk.AssetDependencyView {
        implicitWidth: 780
        implicitHeight: 360
        index: graph
        selectedId: "qml/Main.qml"
    }
}
