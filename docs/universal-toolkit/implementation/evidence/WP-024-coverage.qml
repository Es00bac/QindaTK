// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    width: 720
    height: 500
    color: Tk.Theme.color.bg
    Tk.FontCatalog { id: fonts }
    Tk.LocaleService { id: locale; localeName: "en_US"; timeZoneId: "America/Los_Angeles" }
    readonly property var face: fonts.resolveFamily("DejaVu Sans").installed
        ? fonts.resolveFamily("DejaVu Sans") : fonts.families[0]
    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.lg
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.lg
        Tk.Heading { text: "Typography and date controls" }
        Tk.FontFeaturesEditor {
            objectName: "previewFeatures"
            width: 550
            fontProvider: fonts
            fontFamily: face.family
            fontStyle: face.styles[0]
            sampleText: "Sample A\uDBFF\uDFFF"
            tooltip: "Selected face coverage"
        }
        Tk.DateTimeField {
            objectName: "previewDateTime"
            width: 550
            localeService: locale
            dateOnly: true
            dateFormat: "M/d/yyyy"
            minValue: "2024-02-29"
            maxValue: "2024-03-01"
            draft: "3/2/2024"
            validationError: "Value is outside the allowed range"
            tooltip: "Schedule date"
        }
    }
}
