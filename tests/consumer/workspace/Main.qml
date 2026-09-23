// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    width: 320
    height: 200
    Tk.SettingsModel { objectName: "installedSettings" }
    Tk.SettingsPersistence { objectName: "installedSettingsFile" }
    Tk.WorkspaceSession { objectName: "installedWorkspace" }
}
