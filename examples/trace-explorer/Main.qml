// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Tk.AppWindow {
    width: 1000
    height: 420
    title: "Trace explorer"
    Tk.TraceIndex { id: trace }
    Component.onCompleted: {
        trace.setClockDomains([
            {id: "mono", nanosPerTick: "1000", offsetNs: "0"},
            {id: "device", nanosPerTick: "100", offsetNs: "100000"}
        ])
        trace.ingestAsync([
            {id: "frame", kind: "span", name: "Render frame", category: "render", threadId: "UI",
             clockDomain: "mono", startTicks: "100", endTicks: "500"},
            {id: "layout", kind: "span", name: "Layout", category: "render", threadId: "UI",
             parentId: "frame", clockDomain: "mono", startTicks: "125", endTicks: "220"},
            {id: "composite", kind: "span", name: "Composite", category: "gpu", threadId: "GPU",
             parentId: "frame", clockDomain: "device", startTicks: "2000", endTicks: "3000"},
            {id: "upload", kind: "event", name: "Upload done", category: "transfer", threadId: "GPU",
             parentId: "composite", clockDomain: "device", startTicks: "2500"},
            {id: "present", kind: "event", name: "Present", category: "display", threadId: "UI",
             parentId: "frame", clockDomain: "mono", startTicks: "450"},
            {id: "decode", kind: "span", name: "Decode input", category: "media", threadId: "IO",
             parentId: "frame", clockDomain: "mono", startTicks: "110", endTicks: "180"},
            {id: "resample", kind: "span", name: "Resample", category: "audio", threadId: "Audio",
             parentId: "frame", clockDomain: "mono", startTicks: "145", endTicks: "265"},
            {id: "effects", kind: "span", name: "Apply effects", category: "render", threadId: "Worker",
             parentId: "frame", clockDomain: "mono", startTicks: "225", endTicks: "395"},
            {id: "draw", kind: "span", name: "Draw tiles", category: "render", threadId: "Render",
             parentId: "frame", clockDomain: "mono", startTicks: "280", endTicks: "430"},
            {id: "mix", kind: "event", name: "Mix complete", category: "audio", threadId: "Audio",
             parentId: "resample", clockDomain: "mono", startTicks: "265"}
        ])
    }
    Tk.TraceView {
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.lg
        index: trace
        selectedId: "composite"
    }
}
