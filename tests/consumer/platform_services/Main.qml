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
    Tk.TransportModel { id: transport; objectName: "installedTransportModel" }
    Tk.CommandBar { objectName: "installedCommandBar"; visible: false }
    Tk.OverflowToolBar { objectName: "installedOverflowToolBar"; visible: false }
    Tk.Ribbon { objectName: "installedRibbon"; visible: false }
    Tk.NavigationRail { objectName: "installedNavigationRail"; visible: false }
    Tk.DocumentTabs { objectName: "installedDocumentTabs"; documentManager: documents; visible: false }
    Tk.BreadcrumbBar { objectName: "installedBreadcrumbBar"; visible: false }
    Tk.Inspector { objectName: "installedInspector"; visible: false }
    Tk.ExpressionField { objectName: "installedExpressionField"; localeService: locale; visible: false }
    Tk.FilePathField { objectName: "installedFilePathField"; dialogAdapter: fileDialog; visible: false }
    Tk.TokenField { objectName: "installedTokenField"; visible: false }
    Tk.FontPicker { objectName: "installedFontPicker"; fontProvider: fonts; visible: false }
    Tk.DateTimeField { objectName: "installedDateTimeField"; localeService: locale; visible: false }
    Tk.CalendarPicker { objectName: "installedCalendarPicker"; localeService: locale; visible: false }
    Tk.ShortcutEditor { objectName: "installedShortcutEditor"; visible: false }
    Tk.FindReplaceBar { objectName: "installedFindReplaceBar"; visible: false }
    Tk.HistoryView { objectName: "installedHistoryView"; visible: false }
    Tk.NotificationCenter { objectName: "installedNotificationCenter"; visible: false }
    Tk.TaskCenter { objectName: "installedTaskCenter"; visible: false }
    Tk.TransportBar { objectName: "installedTransportBar"; transport: transport; visible: false }
    Tk.PreferencesDialog { objectName: "installedPreferencesDialog" }
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
