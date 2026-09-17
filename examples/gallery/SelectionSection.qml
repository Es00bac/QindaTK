// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Selection"
    note: "CheckBox · Switch · RadioButton · TabStrip · ListRow · TreeRow"

    GalleryRow {
        label: "CheckBox"
        Tk.CheckBox { text: "Unchecked" }
        Tk.CheckBox { text: "Checked"; checked: true }
        Tk.CheckBox { text: "Partial"; tristate: true; checkState: Qt.PartiallyChecked }
        Tk.CheckBox { text: "Small"; small: true; checked: true }
        Tk.CheckBox { text: "Disabled"; enabled: false; checked: true }
    }
    GalleryRow {
        label: "Switch"
        Tk.Switch { text: "Off" }
        Tk.Switch { text: "On"; checked: true }
        Tk.Switch { text: "Small"; small: true; checked: true }
        Tk.Switch { text: "Disabled"; enabled: false }
    }
    GalleryRow {
        label: "RadioButton"
        Tk.RadioButton { text: "RGB"; checked: true }
        Tk.RadioButton { text: "CMYK" }
        Tk.RadioButton { text: "Grayscale" }
        Tk.RadioButton { text: "Small"; small: true }
        Tk.RadioButton { text: "Disabled"; enabled: false }
    }
    GalleryRow {
        label: "TabStrip"
        Tk.TabStrip { model: ["Clip", "Effects", "Audio"]; currentIndex: 0; implicitWidth: 200 }
        Tk.TabStrip { model: ["Library", "Design assets"]; style: "pill"; currentIndex: 1; implicitWidth: 180 }
        Tk.TabStrip { model: [{ "text": "page-01.sloom", "closable": true }, { "text": "cover.sloom", "closable": true }]; closable: true; implicitWidth: 220 }
        Tk.TabStrip { model: ["S", "M", "L"]; small: true; implicitWidth: 100 }
    }
    GalleryRow {
        label: "ListRow"
        Tk.Box {
            color: Tk.Theme.color.panelAlt; borderWidth: 1; radius: Tk.Theme.radius.xs
            implicitWidth: 300
            Tk.Flex {
                direction: Tk.Flex.Column; gap: 1
                Tk.ListRow { text: "Plain row"; iconName: "file" }
                Tk.ListRow { text: "Selected row"; iconName: "film"; selected: true }
                Tk.ListRow { text: "Active row"; iconName: "layers"; active: true }
                Tk.ListRow { text: "walk-wide.mp4"; secondaryText: "V1 · 12.8s · 1080p"; iconName: "film"; trailing: [ Tk.Badge { text: "V1"; variant: "accent" } ] }
                Tk.ListRow { text: "Small row"; small: true; indent: 1 }
                Tk.ListRow { text: "Disabled row"; enabled: false }
            }
        }
        Tk.Box {
            color: Tk.Theme.color.panelAlt; borderWidth: 1; radius: Tk.Theme.radius.xs
            implicitWidth: 300
            Tk.Flex {
                direction: Tk.Flex.Column; gap: 1
                Tk.TreeRow { text: "Project"; iconName: "folder"; expandable: true; expanded: true }
                Tk.TreeRow { text: "Pages"; iconName: "folder"; depth: 1; expandable: true; expanded: true }
                Tk.TreeRow { text: "page-01"; iconName: "file-text"; depth: 2; selected: true }
                Tk.TreeRow { text: "page-02"; iconName: "file-text"; depth: 2 }
                Tk.TreeRow { text: "Assets"; iconName: "folder"; depth: 1; expandable: true; expanded: false }
                Tk.TreeRow { text: "Exports"; iconName: "folder"; depth: 1; expandable: true; expanded: false }
            }
        }
    }
}
