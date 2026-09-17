// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Flexible space inside a Flex row or column (CSS `margin-left: auto` /
// `flex: 1` on an empty div). `size` gives it a fixed extent instead.
Item {
    id: spacer

    property real size: -1

    Tk.Flex.grow: size < 0 ? 1 : 0
    Tk.Flex.shrink: size < 0 ? 1 : 0
    implicitWidth: size < 0 ? 0 : size
    implicitHeight: size < 0 ? 0 : size
}
