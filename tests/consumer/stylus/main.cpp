// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QUrl>

#include <qindatk/stylus_handler.h>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    QindaTK::StylusHandler nativeHandler;
    if (nativeHandler.pressure() != 0.5) {
        return 1;
    }

    QQmlApplicationEngine engine;
    engine.addImportPath(QINDATK_QML_IMPORT_ROOT);
    engine.load(QUrl::fromLocalFile(QStringLiteral(CONSUMER_QML_FILE)));
    if (engine.rootObjects().size() != 1) {
        return 2;
    }
    if (!engine.rootObjects().first()->findChild<QindaTK::StylusHandler *>(
            QStringLiteral("consumerStylusHandler"))) {
        return 3;
    }
    return 0;
}
