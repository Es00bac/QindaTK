// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    width: 320
    height: 160

    Tk.JobModel { objectName: "installedJobs" }
    Tk.ResourceResolver { objectName: "installedResolver" }
    Tk.AssetProvider { objectName: "installedAssets" }
    Tk.DocumentResourceLinks { objectName: "installedResourceLinks" }

    Tk.AssetThumbnail { objectName: "installedAssetThumbnail"; active: false }
    Tk.AssetWaveform { objectName: "installedAssetWaveform"; active: false }
    Tk.AssetFilmstrip { objectName: "installedAssetFilmstrip"; active: false }
}
