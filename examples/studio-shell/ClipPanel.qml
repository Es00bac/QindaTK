// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// The selected visual clip: one PropertyRow per attribute with the dense
// editors, then the keyframe section.
Tk.Scroll {
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.xs

        Tk.Caption {
            text: "Tune the selected clip, or inspect the currently selected source asset."
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
        Tk.PropertyGroup {
            title: "Selected visual clip"
            Tk.PropertyRow { label: "Asset"; Tk.TextField { text: "turn-cu.mp4"; mono: true } }
            Tk.PropertyRow { label: "Track"; Tk.ComboBox { model: ["Video 1", "Video 2", "Video 3", "Video 4"]; currentIndex: 1 } }
            Tk.PropertyRow { label: "Start"; Tk.NumberField { value: 4.1; decimals: 1; suffix: "s"; stepSize: 0.1 } }
            Tk.PropertyRow { label: "Duration"; Tk.NumberField { value: 8.0; decimals: 1; suffix: "s"; stepSize: 0.1 } }
            Tk.PropertyRow { label: "Fit mode"; Tk.ComboBox { model: ["Contain", "Cover", "Stretch"]; currentIndex: 0 } }
            Tk.PropertyRow { label: "Zoom"; Tk.Slider { value: 100; from: 10; to: 400; showValue: true; suffix: "%" } }
            Tk.PropertyRow { label: "Opacity"; Tk.Slider { value: 100; from: 0; to: 100; showValue: true; suffix: "%" } }
        }
        Tk.SectionHeader {
            title: "Keyframes"
            count: "0"
            Tk.IconButton { iconName: "diamond"; tooltip: "Keyframe options"; small: true }
        }
        Tk.Button { text: "Add key"; iconName: "diamond"; variant: "outline" }
    }
}
