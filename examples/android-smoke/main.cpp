// SPDX-License-Identifier: LGPL-3.0-or-later
// QindaTK Android smoke app (docs/android.md). Loads a QML scene that uses
// `import QindaTK as Tk`, in Touch density, under the sloom-dark theme.
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>

#include "density.h"
#include "theme.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    // AGENT-NOTE: the Basic style carries no platform look; QindaTK controls are
    // QtQuick.Templates plus their own visuals, so the style only needs to exist.
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QindaTK::Theme::instance()->applyPreset(QStringLiteral("sloom-dark"));
    QindaTK::Density::instance()->setMode(QindaTK::Density::Touch);

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("QindaTKSmoke"), QStringLiteral("AndroidSmoke"));
    return app.exec();
}
