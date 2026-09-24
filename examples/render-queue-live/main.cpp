// SPDX-License-Identifier: LGPL-3.0-or-later
#include "demo_host.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickItem>
#include <QQuickView>
#include <QQmlContext>
#include <QQmlEngine>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

namespace {
QQuickItem *findItem(QQuickItem *root, const QString &name)
{
    if (!root) return nullptr;
    if (root->objectName() == name) return root;
    for (QQuickItem *child : root->childItems())
        if (QQuickItem *found = findItem(child, name)) return found;
    return nullptr;
}
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments();
    const int rootOption = args.indexOf(QStringLiteral("--output-root"));
    const QString root = rootOption >= 0 && rootOption + 1 < args.size()
        ? QDir(args.at(rootOption + 1)).absolutePath()
        : QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
              .filePath(QStringLiteral("render-queue-live"));
    DemoHost host(root);
    if (!host.ready()) { qCritical().noquote() << host.lastError(); return 2; }
    QQuickView window;
    window.engine()->addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
    window.rootContext()->setContextProperty(QStringLiteral("demoHost"), &host);
    window.setResizeMode(QQuickView::SizeRootObjectToView);
    window.setSource(QUrl::fromLocalFile(QStringLiteral(RENDER_QUEUE_LIVE_QML_FILE)));
    if (window.status() != QQuickView::Ready || !window.rootObject()) return 3;
    window.resize(1080, 680);
    window.show();

    const bool check = args.contains(QStringLiteral("--check"));
    const int grabOption = args.indexOf(QStringLiteral("--grab"));
    const bool grab = grabOption >= 0 && grabOption + 1 < args.size();
    if (!check && !grab && !args.contains(QStringLiteral("--auto"))) return app.exec();
    const bool startedDraft = host.state() == QLatin1String("draft");
    if (startedDraft) {
        QCoreApplication::processEvents();
        auto *open = findItem(window.rootObject(), QStringLiteral("renderOpenOutput_demo-caption"));
        if (!open || open->isVisible() || !QMetaObject::invokeMethod(window.rootObject(), "runDemo")) return 4;
    }
    if (host.state() == QLatin1String("validated")) return 4;
    QEventLoop wait;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &wait, &QEventLoop::quit);
    QObject::connect(&host, &DemoHost::changed, &wait, [&]() {
        if (host.state() == QLatin1String("succeeded") || host.state() == QLatin1String("failed")) wait.quit();
    });
    if (host.state() != QLatin1String("succeeded")) {
        timeout.start(20000);
        wait.exec();
    }
    if (host.state() != QLatin1String("succeeded") || !host.verify()) {
        qCritical().noquote() << "Render failed:" << host.state() << host.lastError(); return 5;
    }
    const QString frame = QDir(host.outputPath()).filePath(QStringLiteral("frame-000000.png"));
    QImageReader reader(frame);
    const QImage image = reader.read();
    QFile manifest(QDir(host.outputPath()).filePath(QStringLiteral("render-manifest.json")));
    if (image.size() != QSize(640, 360) || !manifest.open(QIODevice::ReadOnly)
        || QJsonDocument::fromJson(manifest.readAll()).object().value(QStringLiteral("files")).toArray().size() != 3)
        return 6;
    QCoreApplication::processEvents();
    auto *open = findItem(window.rootObject(), QStringLiteral("renderOpenOutput_demo-caption"));
    if (!open || !open->isVisible()
        || (startedDraft && host.statusTrail() != QStringLiteral("queued → running → succeeded"))) {
        qCritical().noquote() << "UI verification failed" << bool(open)
            << (open ? open->isVisible() : false) << host.statusTrail();
        return 8;
    }
    if (grab) {
        QEventLoop settle;
        QTimer::singleShot(150, &settle, &QEventLoop::quit);
        settle.exec();
        if (!window.grabWindow().save(args.at(grabOption + 1), "PNG")) return 7;
    }
    QTextStream(stdout) << "verified " << host.outputPath() << " (3 PNG frames)\n";
    return 0;
}
