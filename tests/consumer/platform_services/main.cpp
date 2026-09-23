// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>
#include <qindatk/accessibility_adapter.h>
#include <qindatk/clipboard_service.h>
#include <qindatk/drag_drop_service.h>
#include <qindatk/gesture_router.h>
#include <qindatk/locale_service.h>
#include <qindatk/transfer_payload.h>
#include <qindatk/time_map.h>
#include <qindatk/window_host.h>
#include <qindatk/file_dialog_adapter.h>
#include <qindatk/desktop_integration.h>
#include <qindatk/format_registry.h>
#include <qindatk/font_catalog.h>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QINDATK_QML_IMPORT_ROOT));
    engine.load(QUrl::fromLocalFile(QStringLiteral(CONSUMER_QML_FILE)));
    if (engine.rootObjects().size() != 1) return 1;
    const QStringList names{
        QStringLiteral("installedClipboard"),
        QStringLiteral("installedDragDrop"),
        QStringLiteral("installedLocale"),
        QStringLiteral("installedGesture"),
        QStringLiteral("installedTimeMap"),
        QStringLiteral("installedWindowHost"),
        QStringLiteral("installedFileDialog"),
        QStringLiteral("installedDesktopIntegration"),
        QStringLiteral("installedAccessibility"),
        QStringLiteral("installedDocuments"),
        QStringLiteral("installedFormatRegistry"),
        QStringLiteral("installedFontCatalog"),
        QStringLiteral("installedCommandBar"),
        QStringLiteral("installedOverflowToolBar"),
        QStringLiteral("installedInspector"),
        QStringLiteral("installedFontPicker"),
        QStringLiteral("installedDateTimeField"),
        QStringLiteral("installedCalendarPicker"),
        QStringLiteral("installedImportDialog"),
        QStringLiteral("installedExportDialog")};
    QObject *root = engine.rootObjects().first();
    for (const QString &name : names)
        if (!root->findChild<QObject *>(name)) return 2;
    return 0;
}
