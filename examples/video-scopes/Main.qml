// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    width: 900
    height: 650
    color: Tk.Theme.color.canvas
    property string fixtureStatus: qsTr("Analyzing synthetic bars…")
    Tk.VideoScopesModel {
        id: analyzer
        onChanged: if (snapshot.valid) fixtureStatus = qsTr("CPU analysis complete · synthetic bars · no video decoder attached")
    }
    Component.onCompleted: analyzer.analyzeColorBarsFixture("bars-frame-0001")

    Tk.Flex {
        anchors.fill: parent
        padding: Tk.Theme.space.xl
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.Label { text: qsTr("Video scopes"); font.pixelSize: Tk.Theme.font.title; Accessible.role: Accessible.Heading }
            Tk.Badge { text: qsTr("CPU fixture"); variant: "accent" }
        }
        Tk.Label {
            text: qsTr("A deterministic eight-bar frame is analyzed by the same bounded CPU path used for decoded QImages.")
            muted: true; wrapMode: Text.Wrap
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            gap: Tk.Theme.space.md
            Tk.VideoScopes {
                Tk.Flex.flex: 1
                snapshot: analyzer.snapshot
                scopeMode: "waveform"
                tooltip: qsTr("Waveform: columns are horizontal image position; vertical axis is Rec.709 Y prime")
            }
            Tk.VideoScopes {
                Tk.Flex.flex: 1
                snapshot: analyzer.snapshot
                scopeMode: "vectorscope"
                tooltip: qsTr("Vectorscope: full-range Rec.709 Cb and Cr derived from encoded sRGB")
            }
        }
        Tk.Label { text: fixtureStatus; color: Tk.Theme.color.textMuted; objectName: "videoScopesFixtureStatus" }
        Tk.Label {
            text: qsTr("No legal-range scaling, broadcast decoding, chroma subsampling, or video processing is implied.")
            muted: true; wrapMode: Text.Wrap
        }
    }
}
