// SPDX-License-Identifier: LGPL-3.0-or-later
// QindaTK Android smoke app (docs/android.md). Loads a QML scene that uses
// `import QindaTK as Tk`, in Touch density, under the sloom-dark theme.
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    // AGENT-NOTE: the Basic style carries no platform look; QindaTK controls are
    // QtQuick.Templates plus their own visuals, so the style only needs to exist.
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    // Theme and density are set from QML (Component.onCompleted in
    // AndroidSmoke.qml), so this file needs no QindaTK C++ header and the
    // example builds both in-tree and against an installed QindaTK.

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("QindaTKSmoke"), QStringLiteral("AndroidSmoke"));
    return app.exec();
}
