// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    width: 400
    height: 240

    Tk.ClipboardService { objectName: "installedClipboard" }
    Tk.DragDropService { objectName: "installedDragDrop" }
    Tk.LocaleService { id: locale; objectName: "installedLocale" }
    Tk.GestureRouter { objectName: "installedGesture" }
    Tk.TimeMap { objectName: "installedTimeMap" }
    Tk.WindowHost { objectName: "installedWindowHost" }
    Tk.FileDialogAdapter { id: fileDialog; objectName: "installedFileDialog" }
    Tk.DesktopIntegration { objectName: "installedDesktopIntegration" }
    Tk.DocumentManager { id: documents; objectName: "installedDocuments" }
    Tk.FormatRegistry { id: formats; objectName: "installedFormatRegistry" }
    Tk.FontCatalog { id: fonts; objectName: "installedFontCatalog" }
    Tk.CommandBar { objectName: "installedCommandBar"; visible: false }
    Tk.OverflowToolBar { objectName: "installedOverflowToolBar"; visible: false }
    Tk.Inspector { objectName: "installedInspector"; visible: false }
    Tk.FontPicker { objectName: "installedFontPicker"; fontProvider: fonts; visible: false }
    Tk.DateTimeField { objectName: "installedDateTimeField"; localeService: locale; visible: false }
    Tk.CalendarPicker { objectName: "installedCalendarPicker"; localeService: locale; visible: false }
    Tk.ImportDialog {
        objectName: "installedImportDialog"
        formatRegistry: formats
        fileDialogAdapter: fileDialog
        documentManager: documents
    }
    Tk.ExportDialog {
        objectName: "installedExportDialog"
        formatRegistry: formats
        fileDialogAdapter: fileDialog
    }
    Tk.AccessibilityAdapter {
        objectName: "installedAccessibility"
        width: parent.width
        height: parent.height
        accessibleName: "Consumer virtual editor"
        Accessible.role: Accessible.Pane
    }
}
