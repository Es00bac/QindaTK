// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Version browser"
    note: "Named checkpoints, provenance, compare and safe restore"
    property var exampleHistory: ({
        count: 2,
        client: { document: { revision: "8" } },
        lastError: "",
        versions: [
            {id: "v2", name: "Final layout", actor: "editor-b", revision: "7",
             createdAt: "2026-09-24 09:42Z"},
            {id: "v1", name: "Initial draft", actor: "editor-a", revision: "3",
             createdAt: "2026-09-24 09:10Z"}
        ],
        inspect: function(id) {
            return {id: id, name: id === "v1" ? "Initial draft" : "Final layout",
                    actor: id === "v1" ? "editor-a" : "editor-b",
                    revision: id === "v1" ? "3" : "7", createdAt: "2026-09-24"}
        },
        diffCurrent: function(id) {
            return id === "v1"
                ? [{entity: "scene", property: "opacity", from: 0.6, to: 1.0}]
                : []
        },
        checkpoint: function(_name) { return "" },
        restore: function(_id) { return true }
    })
    Tk.VersionBrowser {
        width: 400
        history: exampleHistory
        selectedId: "v1"
        Tk.Flex.shrink: 0
    }
}
