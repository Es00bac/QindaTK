// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtCore
import QindaTK as Tk

// qtk-preview teardown fixture: long test-signal jobs are still painting
// frames when --check exits. The preview must destroy the scene (and with it
// the catalog's JobModel, which cancels and waits) before the application
// goes away; otherwise a worker paints into a torn-down QGuiApplication.
Item {
    id: fixture
    width: 320
    height: 180
    readonly property string folder: StandardPaths.writableLocation(StandardPaths.TempLocation)
                                     + "/qindatk-preview-teardown"
    Tk.MediaCatalog {
        id: catalog
        Component.onCompleted: {
            for (let i = 0; i < 4; ++i)
                catalog.generateTestSignal(fixture.folder + "/busy-" + i + ".mkv", ({frames: 20000, channels: 0}))
        }
    }
}
