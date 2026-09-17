// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// One gallery block: a header row with the section title and a note, then
// the section's content in a column. Hidden when it does not match the
// gallery's filter text (an invisible child takes no space in a Flex).
Tk.Box {
    id: section

    property string title: ""
    property string note: ""
    property string filter: ""
    default property alias content: body.data

    visible: filter.length === 0 || title.toLowerCase().indexOf(filter.toLowerCase()) >= 0
    color: Tk.Theme.color.panel
    borderWidth: 1
    borderColor: Tk.Theme.color.divider
    radius: Tk.Theme.radius.md
    padding: Tk.Theme.space.md

    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md

        Tk.SectionHeader {
            title: section.title
            count: section.note
        }
        Tk.Flex {
            id: body
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.md
        }
    }
}
