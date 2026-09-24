// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick

// Evidence fixture for WP-086-volume-cursor.png: loads the volume-imaging
// example and moves its shared cursor to world (4.0, -3.2, 3.2) mm through
// the axial view's cursor, as a click there would.
Loader {
    width: 1280
    height: 800
    source: "../../../../examples/volume-imaging/Main.qml"
    function find(item, name) {
        if (item.objectName === name) return item
        for (let i = 0; i < item.children.length; ++i) {
            const hit = find(item.children[i], name)
            if (hit) return hit
        }
        return null
    }
    onLoaded: Qt.callLater(function() {
        find(item, "phantomAxial").cursor.setWorldPoint(4.0, -3.2, 3.2)
    })
}
