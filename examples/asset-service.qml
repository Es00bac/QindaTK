// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// This fixture uses a real repository PNG for the thumbnail. The file backend
// deliberately reports that a PNG has no waveform and no video frames; the
// adapters show those failures instead of presenting invented content.
Rectangle {
    id: root
    width: 920
    height: 420
    color: Tk.Theme.color.bg

    property string exampleResourceId: "qindatk.example.asset-service"
    property string setupError: ""

    Tk.ResourceResolver {
        id: resolver
    }
    Tk.AssetProvider {
        id: assets
        resolver: resolver
    }

    Component.onCompleted: {
        if (!resolver.registerResource(root.exampleResourceId,
                                      Qt.resolvedUrl("../docs/screenshots/gallery.png")))
            root.setupError = resolver.lastError
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        gap: Tk.Theme.space.md
        padding: Tk.Theme.space.lg

        Tk.Overline {
            title: qsTr("ASSET SERVICE ADAPTERS")
            wide: true
            color: Tk.Theme.color.accent
        }
        Tk.Caption {
            text: root.setupError.length > 0
                  ? root.setupError
                  : qsTr("Thumbnail uses a real PNG. Waveform and frame requests show the file backend's explicit unsupported results.")
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }

        Tk.Flex {
            direction: Tk.Flex.Row
            align: Tk.Flex.Start
            gap: Tk.Theme.space.lg

            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Caption { text: qsTr("IMAGE PREVIEW") }
                Tk.AssetThumbnail {
                    id: imageAsset
                    provider: assets
                    resourceId: root.exampleResourceId
                    active: resolver.resources.length > 0
                    tooltip: qsTr("Repository smoke screenshot")
                }
                Tk.Caption {
                    text: qsTr("%1 — %2").arg(imageAsset.assetState)
                                             .arg(imageAsset.assetError)
                    elide: Text.ElideRight
                }
            }

            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Caption { text: qsTr("AUDIO PEAKS") }
                Tk.AssetWaveform {
                    id: audioAsset
                    width: Tk.Theme.size.thumbnail * 2
                    provider: assets
                    resourceId: root.exampleResourceId
                    active: resolver.resources.length > 0
                    tooltip: qsTr("Waveform request against the PNG fixture")
                }
                Tk.Caption {
                    text: qsTr("%1 — %2").arg(audioAsset.assetState).arg(audioAsset.assetError)
                    elide: Text.ElideRight
                }
            }

            Tk.Flex {
                direction: Tk.Flex.Column
                gap: Tk.Theme.space.xs
                Tk.Caption { text: qsTr("VIDEO ENDPOINTS") }
                Tk.AssetFilmstrip {
                    id: videoAsset
                    provider: assets
                    resourceId: root.exampleResourceId
                    active: resolver.resources.length > 0
                    tailTimeTicks: "24000"
                    rateNumerator: "24"
                    rateDenominator: "1"
                    tooltip: qsTr("Frame requests against the PNG fixture")
                }
                Tk.Caption {
                    text: qsTr("%1 — %2").arg(videoAsset.assetState).arg(videoAsset.assetError)
                    elide: Text.ElideRight
                }
            }
        }

        Tk.Spacer { Tk.Flex.grow: 1 }
        Tk.Caption {
            text: qsTr("Adapters release subscriptions when their resource, view, document, provider or active state changes, and when the component is destroyed.")
            wrapMode: Text.Wrap
            elide: Text.ElideNone
            color: Tk.Theme.color.textMuted
        }
    }
}
