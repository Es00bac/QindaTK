// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Diagnostic scene for layout foundations. Names are stable for qtk-preview
// dumps; each section deliberately isolates one geometry contract.
Item {
    width: 960
    height: 760
    Tk.Flex {
        objectName: "baselineRow"
        x: 12; y: 12; width: 420; height: 54
        direction: Tk.Flex.Row
        align: Tk.Flex.Baseline
        gap: 8
        Tk.Label { objectName: "baselineSmall"; text: "Small x"; font.pixelSize: 12 }
        Tk.Label { objectName: "baselineLarge"; text: "Large x"; font.pixelSize: 26 }
    }
    Tk.Flex {
        objectName: "wrappingColumn"
        x: 12; y: 82; width: 220; height: 112
        direction: Tk.Flex.Column
        align: Tk.Flex.Stretch
        Tk.Label { objectName: "wrappedText"; text: "A long intrinsic text sample that must wrap at the assigned narrow width."; wrapMode: Text.Wrap }
        Item { objectName: "afterWrappedText"; implicitHeight: 14 }
    }
    Tk.Grid {
        objectName: "invalidTrackGrid"
        x: 252; y: 82; width: 220; height: 96
        columns: "1fr minmax(40px, nope)"
        Item { objectName: "invalidTrackCell"; implicitWidth: 24; implicitHeight: 20 }
    }
    Tk.Flex {
        objectName: "rtlRow"
        x: 492; y: 82; width: 220; height: 44
        LayoutMirroring.enabled: true
        LayoutMirroring.childrenInherit: true
        direction: Tk.Flex.Row
        layoutDirection: Qt.RightToLeft
        justify: Tk.Flex.Start
        Rectangle { objectName: "rtlStartA"; implicitWidth: 48; implicitHeight: 24; color: Tk.Theme.color.accent }
        Rectangle { objectName: "rtlStartB"; implicitWidth: 48; implicitHeight: 24; color: Tk.Theme.color.accentSubtle }
    }
    Tk.Stack {
        objectName: "rtlStack"
        x: 492; y: 146; width: 220; height: 44
        paddingLeft: 5; paddingRight: 15
        layoutDirection: Qt.RightToLeft
        Rectangle {
            objectName: "rtlStackLeft"
            implicitWidth: 40; implicitHeight: 24
            color: Tk.Theme.color.accent
            Tk.Stack.fill: false; Tk.Stack.left: 5
        }
        Rectangle {
            objectName: "rtlStackRight"
            implicitWidth: 40; implicitHeight: 24
            color: Tk.Theme.color.accentSubtle
            Tk.Stack.fill: false; Tk.Stack.right: 5
        }
    }
    Tk.Grid {
        objectName: "rtlGrid"
        x: 736; y: 82; width: 212; height: 96
        columns: "72 1fr"; rows: "44"; gap: 8
        paddingLeft: 5; paddingRight: 15
        layoutDirection: Qt.RightToLeft
        justifyItems: Tk.Grid.Start; alignItems: Tk.Grid.Start
        Rectangle {
            objectName: "rtlGridFirst"
            implicitWidth: 40; implicitHeight: 24
            color: Tk.Theme.color.accent
            Tk.Grid.column: 0
        }
        Rectangle {
            objectName: "rtlGridSecond"
            implicitWidth: 48; implicitHeight: 24
            color: Tk.Theme.color.accentSubtle
            Tk.Grid.column: 1
        }
    }
    Tk.Box {
        objectName: "roundedSideBorder"
        x: 252; y: 202; width: 180; height: 84
        color: Tk.Theme.color.panelAlt
        radius: Tk.Theme.radius.lg
        borderWidth: 0
        borderLeft: 3
        borderTop: 1
        borderColor: Tk.Theme.color.accent
        padding: 8
        Tk.Label { objectName: "borderLabel"; text: "Square per-side edges" }
    }
    Tk.Scroll {
        objectName: "outerScroll"
        x: 12; y: 214; width: 216; height: 184
        overflowY: Tk.Scroll.Auto
        Tk.Flex {
            objectName: "outerScrollContent"
            direction: Tk.Flex.Column
            gap: 4
            Repeater {
                model: 8
                Tk.Label { objectName: "outerRow" + index; text: "Outer row " + index }
            }
            Tk.Scroll {
                objectName: "innerScroll"
                implicitWidth: 184
                implicitHeight: 76
                overflowY: Tk.Scroll.Auto
                Tk.Flex {
                    objectName: "innerScrollContent"
                    direction: Tk.Flex.Column
                    Repeater {
                        model: 8
                        Tk.Label { objectName: "innerRow" + index; text: "Inner row " + index }
                    }
                }
            }
        }
    }
}
