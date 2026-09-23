// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: scene
    width: 920
    height: 620
    objectName: "platformServicesScene"

    Tk.LocaleService {
        id: locale
        localeName: "ar_EG"
        chromeDirection: "rtl"
    }
    Component.onCompleted: Tk.Density.textScale = 1.5

    Rectangle {
        anchors.fill: parent
        color: Tk.Theme.color.canvas
    }

    Tk.Flex {
        id: chrome
        objectName: "rtlChrome"
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        layoutDirection: locale.chromeDirection === "rtl" ? Qt.RightToLeft : Qt.LeftToRight
        gap: Tk.Theme.space.md
        LayoutMirroring.enabled: locale.chromeDirection === "rtl"
        LayoutMirroring.childrenInherit: true
        Accessible.role: Accessible.Pane
        Accessible.name: qsTr("Locale and input qualification")

        Tk.Flex {
            objectName: "headingRow"
            direction: Tk.Flex.Row
            layoutDirection: locale.chromeDirection === "rtl" ? Qt.RightToLeft : Qt.LeftToRight
            align: Tk.Flex.Center
            gap: Tk.Theme.space.sm
            Tk.Icon { name: "globe"; size: Tk.Theme.size.icon; color: Tk.Theme.color.accent }
            Tk.Label {
                objectName: "localeHeading"
                text: qsTr("Locale, direction and editing")
                font.pixelSize: Tk.Theme.font.title
                color: Tk.Theme.color.text
            }
        }

        Tk.Label {
            objectName: "longLocalizedLabel"
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignRight
            LayoutMirroring.enabled: false
            color: Tk.Theme.color.textMuted
            text: qsTr("A deliberately long translated-style description checks whether dense application chrome can grow and wrap without truncating instructions or pushing the active editor outside the visible workspace.")
        }

        Tk.Box {
            objectName: "localeReadout"
            color: Tk.Theme.color.panelAlt
            borderColor: Tk.Theme.color.divider
            borderWidth: Tk.Theme.size.border
            radius: Tk.Theme.radius.md
            padding: Tk.Theme.space.md
            implicitHeight: Tk.Theme.size.control * 2
            Tk.Flex {
                direction: Tk.Flex.Row
                layoutDirection: locale.chromeDirection === "rtl" ? Qt.RightToLeft : Qt.LeftToRight
                justify: Tk.Flex.SpaceBetween
                align: Tk.Flex.Center
                Tk.Label {
                    objectName: "formattedNumber"
                    text: locale.formatNumber(12345.67, 2)
                    color: Tk.Theme.color.text
                }
                Tk.Label {
                    objectName: "localeAndDirection"
                    text: locale.localeName + " · chrome " + locale.chromeDirection
                    color: Tk.Theme.color.accentText
                }
            }
        }

        Tk.Label {
            objectName: "bidiCaption"
            text: qsTr("Document text remains independent of chrome direction")
            horizontalAlignment: Text.AlignRight
            LayoutMirroring.enabled: false
            color: Tk.Theme.color.text
        }
        Text {
            id: bidiSample
            objectName: "mixedBidiText"
            text: "فاتورة Invoice رقم ١٢٣٤ — project QindaTK — total €45.00"
            horizontalAlignment: locale.contentDirection(text) === "rtl"
                                 ? Text.AlignRight : Text.AlignLeft
            LayoutMirroring.enabled: false
            wrapMode: Text.Wrap
            color: Tk.Theme.color.text
            font.family: Tk.Theme.font.family
            font.pixelSize: Tk.Theme.font.body
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Tk.Label {
            objectName: "dirtyFieldCaption"
            text: qsTr("Dirty editable value (preserved while locale changes)")
            color: Tk.Theme.color.text
        }
        Tk.TextField {
            id: dirtyField
            objectName: "dirtyLocaleField"
            tooltip: qsTr("Uncommitted amount")
            placeholderText: qsTr("Enter an amount")
            text: "1,234.50 — draft"
            Accessible.name: qsTr("Uncommitted amount")
        }
        Item { Tk.Flex.grow: 1 }
        Tk.Label {
            objectName: "qualificationNote"
            text: qsTr("This scene scales typography independently from density and keeps document bidi text separate from mirrored application chrome.")
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignRight
            LayoutMirroring.enabled: false
            color: Tk.Theme.color.textMuted
        }
    }
}
