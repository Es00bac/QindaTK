// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import "components" as Components

Rectangle {
    width: 320
    height: 200
    Components.Panel { anchors.fill: parent }
    Image { source: "images/logo.svg" }
    Image { source: "images/lost.png" }
    // source: "images/commented-out.png" must not create an edge.
}
