// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QFile>
#include <QDebug>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QSet>
#include <QTemporaryDir>
#include <QUrl>

namespace {
bool check(bool condition, int code, const char *message)
{
    if (condition)
        return true;
    qCritical("QindaTK installed chart consumer failed at %d: %s", code, message);
    return false;
}
} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(QINDATK_QML_IMPORT_ROOT));
    engine.load(QUrl::fromLocalFile(QStringLiteral(CONSUMER_QML_FILE)));
    if (!check(engine.rootObjects().size() == 1, 1, "installed QML module import"))
        return 1;

    QObject *root = engine.rootObjects().first();
    QObject *view = root->findChild<QObject *>(QStringLiteral("installedChartView"));
    QObject *dataset = root->findChild<QObject *>(QStringLiteral("installedChartDataset"));
    if (!check(view && dataset && root->property("datasetReady").toBool(),
               2, "ChartView and three stable-ID data rows"))
        return 2;

    QVariant selected;
    if (!check(QMetaObject::invokeMethod(root, "selectFirstPoint",
                                        Q_RETURN_ARG(QVariant, selected))
                   && selected.toBool()
                   && root->property("selectedPointId").toString()
                          == QLatin1String("point-a"),
               3, "installed keyboard selection API"))
        return 3;
    QVariant selectedPoint;
    if (!check(QMetaObject::invokeMethod(root, "selectedPoint",
                                        Q_RETURN_ARG(QVariant, selectedPoint))
                   && selectedPoint.toMap().value(QStringLiteral("pointId")).toString()
                          == QLatin1String("point-a"),
               4, "selected full-resolution point inspection"))
        return 4;
    QVariant exportedCsv;
    if (!check(QMetaObject::invokeMethod(root, "csvData",
                                        Q_RETURN_ARG(QVariant, exportedCsv))
                   && exportedCsv.toString().contains(QStringLiteral("point-b")),
               5, "full-resolution CSV API"))
        return 5;

    QTemporaryDir output;
    if (!check(output.isValid(), 6, "temporary export directory"))
        return 6;
    const QSize imageSize(320, 180);
    QVariant pngResult;
    if (!check(QMetaObject::invokeMethod(root, "exportPng",
                                        Q_RETURN_ARG(QVariant, pngResult),
                                        Q_ARG(QVariant, QVariant(output.filePath(QStringLiteral("chart.png")))),
                                        Q_ARG(QVariant, QVariant(imageSize)))
                   && pngResult.toBool(),
               7, "QML PNG export API"))
        return 7;
    const QImage png(output.filePath(QStringLiteral("chart.png")));
    QSet<QRgb> pixels;
    for (int y = 0; y < png.height(); ++y)
        for (int x = 0; x < png.width(); ++x)
            pixels.insert(png.pixel(x, y));
    if (!check(png.size() == imageSize && pixels.size() > 8,
               8, "exported raster dimensions and varied chart pixels"))
        return 8;

    QVariant svgResult;
    if (!check(QMetaObject::invokeMethod(root, "exportSvg",
                                        Q_RETURN_ARG(QVariant, svgResult),
                                        Q_ARG(QVariant, QVariant(output.filePath(QStringLiteral("chart.svg")))),
                                        Q_ARG(QVariant, QVariant(imageSize)))
                   && svgResult.toBool(),
               9, "QML SVG export API"))
        return 9;
    QFile svg(output.filePath(QStringLiteral("chart.svg")));
    if (!check(svg.open(QIODevice::ReadOnly), 10, "read exported SVG"))
        return 10;
    const QByteArray markup = svg.readAll();
    if (!check(markup.contains("<svg")
                   && (markup.contains("<path") || markup.contains("<line")),
               11, "exported vector chart marks"))
        return 11;
    return 0;
}
