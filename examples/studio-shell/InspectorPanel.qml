// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Sloom's Inspector: sequence facts as key/value readouts, the render
// settings, and the two actions that end a session.
Tk.Scroll {
    Tk.Flex {
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.sm

        Tk.PropertyGroup {
            title: "Sequence info"
            Tk.KeyValue { key: "Canvas"; value: "16:9 · 1080p" }
            Tk.KeyValue { key: "Timebase"; value: "30 fps" }
            Tk.KeyValue { key: "Length"; value: "12.8s" }
            Tk.KeyValue { key: "Tracks"; value: "V:2 · A:1" }
        }
        Tk.PropertyGroup {
            title: "Settings"
            Tk.KeyValue { key: "Size"; value: "1080p" }
            Tk.KeyValue { key: "Codec"; value: "H.264" }
            Tk.PropertyRow { label: "Quality"; Tk.ComboBox { model: ["Review", "Master"]; currentIndex: 0 } }
            Tk.PropertyRow { label: "Hardware"; Tk.Switch { text: "VA-API"; checked: true } }
        }
        Tk.Flex {
            direction: Tk.Flex.Column
            gap: Tk.Theme.space.xs
            Tk.Button { text: "Save video"; variant: "outline"; iconName: "save" }
            Tk.Button { text: "Render"; variant: "accent"; iconName: "play" }
        }
    }
}
