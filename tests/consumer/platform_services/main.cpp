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
        QStringLiteral("installedAccessibility")};
    QObject *root = engine.rootObjects().first();
    for (const QString &name : names)
        if (!root->findChild<QObject *>(name)) return 2;
    return 0;
}
