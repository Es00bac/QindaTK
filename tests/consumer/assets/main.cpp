// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <qindatk/asset_provider.h>
#include <qindatk/resource_resolver.h>

using namespace QindaTK;

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;

    const QString imagePath = directory.filePath(QStringLiteral("fixture.png"));
    QImage image(16, 12, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(20, 180, 220));
    if (!image.save(imagePath, "PNG")) return 2;

    ResourceResolver resolver;
    if (!resolver.registerResource(QStringLiteral("consumer-fixture"), imagePath))
        return 3;
    AssetProvider assets;
    assets.setResolver(&resolver);
    const QString subscription = assets.requestAsset(
        QStringLiteral("consumer-fixture"), QStringLiteral("thumbnail"),
        {{QStringLiteral("size"), QVariantMap{{QStringLiteral("width"), 8},
                                               {QStringLiteral("height"), 8}}}});
    if (subscription.isEmpty()) return 4;

    bool assetReady = false;
    QObject::connect(&assets, &AssetProvider::assetReady, &app,
        [&](const QString &id) {
            if (id != subscription) return;
            const QVariantMap asset = assets.asset(id);
            const QVariantMap data = asset.value(QStringLiteral("data")).toMap();
            assetReady = asset.value(QStringLiteral("state")).toString()
                             == QLatin1String("ready")
                && data.value(QStringLiteral("mimeType")).toString()
                       == QLatin1String("image/png")
                && data.value(QStringLiteral("previewUrl")).toString()
                       .startsWith(QLatin1String("data:image/png;base64,"));
            app.quit();
        });
    QTimer::singleShot(10000, &app, &QCoreApplication::quit);
    app.exec();
    if (!assetReady) return 5;

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QINDATK_QML_IMPORT_ROOT));
    engine.load(QUrl::fromLocalFile(QStringLiteral(CONSUMER_QML_FILE)));
    if (engine.rootObjects().size() != 1) return 6;
    QObject *root = engine.rootObjects().first();
    const QStringList required{
        QStringLiteral("installedJobs"), QStringLiteral("installedResolver"),
        QStringLiteral("installedAssets"), QStringLiteral("installedResourceLinks"),
        QStringLiteral("installedAssetThumbnail"), QStringLiteral("installedAssetWaveform"),
        QStringLiteral("installedAssetFilmstrip")};
    for (const QString &name : required)
        if (!root->findChild<QObject *>(name)) return 7;
    return 0;
}
