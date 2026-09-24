// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/automation_model.h"
#include "../../src/core/motion_composition.h"
#include "../../src/core/motion_frame_renderer.h"
#include <QColorSpace>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
bool addCurve(AutomationModel &model, const QString &id, double maximum, const QVariantList &keys)
{
    if (!model.addParameterMap({{QStringLiteral("id"), id}, {QStringLiteral("name"), id},
            {QStringLiteral("dimension"), QStringLiteral("length")}, {QStringLiteral("unitId"), QStringLiteral("px")},
            {QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), maximum}, {QStringLiteral("default"), 0.0},
            {QStringLiteral("interpolation"), QStringLiteral("linear")}}).value(QStringLiteral("ok")).toBool())
        return false;
    for (const QVariant &entry : keys) {
        const QVariantList pair = entry.toList();
        if (!model.applyEdit({{QStringLiteral("kind"), QStringLiteral("addPoint")}, {QStringLiteral("parameterId"), id}},
                             {{QStringLiteral("ticks"), pair.at(0)}, {QStringLiteral("value"), pair.at(1)},
                              {QStringLiteral("interpolation"), QStringLiteral("linear")}}).value(QStringLiteral("ok")).toBool())
            return false;
    }
    return true;
}

bool addSources(MotionFrameRenderer &renderer)
{
    QImage art(4, 3, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < art.height(); ++y)
        for (int x = 0; x < art.width(); ++x) art.setPixelColor(x, y, QColor(60 * x, 80 * y, 200));
    QImage block(2, 2, QImage::Format_ARGB32_Premultiplied);
    block.fill(QColor(20, 180, 90));
    return renderer.setSourceImage(QStringLiteral("art"), art) && renderer.setSourceImage(QStringLiteral("block"), block);
}

QByteArray exported(MotionFrameRenderer &renderer, const QString &path)
{
    if (!renderer.exportPng(QUrl::fromLocalFile(path))) return {};
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

class MotionFrameRendererTest final : public QObject {
    Q_OBJECT
private slots:
    void compositesLayerPixelsAndExportsSameFrame()
    {
        MotionComposition composition;
        const auto layer = [](const QString &id, const QString &source, int z, int x, double opacity) {
            return QVariantMap{{QStringLiteral("layerId"), id},
                               {QStringLiteral("sourceId"), source},
                               {QStringLiteral("zIndex"), z},
                               {QStringLiteral("visible"), true},
                               {QStringLiteral("transform"), QVariantMap{
                                    {QStringLiteral("x"), x}, {QStringLiteral("y"), 1},
                                    {QStringLiteral("opacity"), opacity}}}};
        };
        composition.setLayers({layer(QStringLiteral("red"), QStringLiteral("red-source"), 0, 1, 1.0),
                               layer(QStringLiteral("blue"), QStringLiteral("blue-source"), 1, 2, 0.5)});
        MotionFrameRenderer renderer;
        renderer.setComposition(&composition);
        QImage red(2, 2, QImage::Format_ARGB32_Premultiplied);
        red.fill(Qt::red);
        QImage blue(2, 2, QImage::Format_ARGB32_Premultiplied);
        blue.fill(Qt::blue);
        QVERIFY(renderer.setSourceImage(QStringLiteral("red-source"), red));
        QVERIFY(renderer.setSourceImage(QStringLiteral("blue-source"), blue));
        const QVariantMap result = renderer.render(QStringLiteral("0"), 8, 8);
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("drawnLayers")).toInt(), 2);
        const QImage frame = renderer.frame();
        QCOMPARE(frame.pixelColor(1, 1), QColor(Qt::red));
        const QColor overlap = frame.pixelColor(2, 1);
        QVERIFY(qAbs(overlap.red() - 127) <= 1);
        QCOMPARE(overlap.green(), 0);
        QVERIFY(qAbs(overlap.blue() - 128) <= 1);
        QCOMPARE(overlap.alpha(), 255);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("frame.png"));
        QVERIFY(renderer.exportPng(QUrl::fromLocalFile(path)));
        const QImage reopened(path);
        QVERIFY(!reopened.isNull());
        QCOMPARE(reopened.pixelColor(2, 1), overlap);
    }

    void orderedEffectsSkipDisabledRejectUnsupportedAndUseHostRegistry()
    {
        MotionComposition composition;
        MotionFrameRenderer renderer;
        renderer.setComposition(&composition);
        QImage red(1, 1, QImage::Format_RGBA8888);
        red.setColorSpace(QColorSpace(QColorSpace::SRgb));
        red.fill(QColor(Qt::red));
        QVERIFY(renderer.setSourceImage(QStringLiteral("pixel"), red));
        const auto layer = [](const QVariantList &effects) {
            return QVariantMap{{QStringLiteral("layerId"), QStringLiteral("layer")},
                               {QStringLiteral("sourceId"), QStringLiteral("pixel")},
                               {QStringLiteral("zIndex"), 0}, {QStringLiteral("visible"), true},
                               {QStringLiteral("effects"), effects},
                               {QStringLiteral("transform"), QVariantMap{}}};
        };
        const QVariantMap bright{{QStringLiteral("effectId"), QStringLiteral("brightness")},
                                 {QStringLiteral("parameters"), QVariantMap{{QStringLiteral("amount"), 0.5}}}};
        const QVariantMap gray{{QStringLiteral("effectId"), QStringLiteral("grayscale")}};
        composition.setLayers({layer({bright, gray})});
        QVERIFY(renderer.render(QStringLiteral("0"), 1, 1).value(QStringLiteral("ok")).toBool());
        const QColor brightThenGray = renderer.frame().pixelColor(0, 0);
        QVERIFY(brightThenGray.red() > 150);
        QCOMPARE(brightThenGray.red(), brightThenGray.green());
        QCOMPARE(brightThenGray.green(), brightThenGray.blue());

        composition.setLayers({layer({gray, bright})});
        QVERIFY(renderer.render(QStringLiteral("0"), 1, 1).value(QStringLiteral("ok")).toBool());
        const QColor grayThenBright = renderer.frame().pixelColor(0, 0);
        QVERIFY(grayThenBright.red() > brightThenGray.red() + 15);
        QCOMPARE(grayThenBright.red(), grayThenBright.green());
        QCOMPARE(grayThenBright.green(), grayThenBright.blue());

        composition.setLayers({layer({QVariantMap{{QStringLiteral("effectId"), QStringLiteral("missing")},
                                                    {QStringLiteral("enabled"), false}}})});
        QVERIFY(renderer.render(QStringLiteral("0"), 1, 1).value(QStringLiteral("ok")).toBool());
        QCOMPARE(renderer.frame().pixelColor(0, 0), QColor(Qt::red));

        const QImage previous = renderer.frame();
        composition.setLayers({layer({QVariantMap{{QStringLiteral("effectId"), QStringLiteral("missing")}}})});
        const QVariantMap rejected = renderer.render(QStringLiteral("0"), 1, 1);
        QVERIFY(!rejected.value(QStringLiteral("ok")).toBool());
        QVERIFY(renderer.lastError().contains(QStringLiteral("Unsupported enabled motion effect")));
        QCOMPARE(renderer.frame(), previous);

        QVERIFY(renderer.registerEffectProcessor(QStringLiteral("invert"),
            [](QImage &image, const QVariantMap &parameters, QString *error) {
                if (!parameters.isEmpty()) { *error = QStringLiteral("invert takes no parameters"); return false; }
                for (int y = 0; y < image.height(); ++y) {
                    uchar *row = image.scanLine(y);
                    for (int x = 0; x < image.width(); ++x)
                        for (int c = 0; c < 3; ++c) row[x * 4 + c] = uchar(255 - row[x * 4 + c]);
                }
                return true;
            }));
        composition.setLayers({layer({QVariantMap{{QStringLiteral("effectId"), QStringLiteral("invert")}}})});
        QVERIFY(renderer.render(QStringLiteral("0"), 1, 1).value(QStringLiteral("ok")).toBool());
        QCOMPARE(renderer.frame().pixelColor(0, 0), QColor(0, 255, 255));
    }

    void boundedCacheReusesFramesAndNeverReturnsStaleOnes()
    {
        AutomationModel model;
        QVERIFY(addCurve(model, QStringLiteral("x"), 7, {QVariantList{QStringLiteral("0"), 0.0},
                                                          QVariantList{QStringLiteral("10"), 4.0}}));
        MotionComposition composition;
        composition.setAnimationModel(&model);
        composition.setLayers({QVariantMap{{QStringLiteral("layerId"), QStringLiteral("dot")},
            {QStringLiteral("sourceId"), QStringLiteral("pixel")},
            {QStringLiteral("animatedProperties"), QVariantMap{{QStringLiteral("x"), QStringLiteral("x")}}}}});
        MotionFrameRenderer renderer;
        renderer.setComposition(&composition);
        QImage white(1, 1, QImage::Format_ARGB32_Premultiplied);
        white.fill(Qt::white);
        QVERIFY(renderer.setSourceImage(QStringLiteral("pixel"), white));
        QSignalSpy invalidated(&renderer, &MotionFrameRenderer::contentInvalidated);

        QVERIFY(!renderer.render(QStringLiteral("10"), 8, 1).value(QStringLiteral("cached")).toBool());
        const QImage first = renderer.frame();
        QCOMPARE(first.pixelColor(4, 0), QColor(Qt::white));
        QVERIFY(renderer.render(QStringLiteral("10"), 8, 1).value(QStringLiteral("cached")).toBool());
        QCOMPARE(renderer.frame(), first);
        QCOMPARE(renderer.frameRevision(), renderer.contentRevision());

        renderer.setFrameCacheLimit(2);
        QVERIFY(renderer.render(QStringLiteral("0"), 8, 1).value(QStringLiteral("ok")).toBool());
        QVERIFY(renderer.render(QStringLiteral("5"), 8, 1).value(QStringLiteral("ok")).toBool());
        QCOMPARE(renderer.cachedFrameCount(), 2);
        QVERIFY(!renderer.render(QStringLiteral("10"), 8, 1).value(QStringLiteral("cached")).toBool()); // evicted

        const QString revision = renderer.contentRevision();
        const QString keyId = model.points(QStringLiteral("x")).at(1).toMap().value(QStringLiteral("id")).toString();
        QVERIFY(model.applyEdit({{QStringLiteral("kind"), QStringLiteral("setPoint")}, {QStringLiteral("parameterId"), QStringLiteral("x")},
                                 {QStringLiteral("pointId"), keyId}}, {{QStringLiteral("value"), 6.0}}).value(QStringLiteral("ok")).toBool());
        QVERIFY(invalidated.count() > 0);
        QVERIFY(renderer.contentRevision() != revision);
        QCOMPARE(renderer.cachedFrameCount(), 0);
        QVERIFY(renderer.frameRevision() != renderer.contentRevision()); // displayed frame is now stale
        QVERIFY(!renderer.render(QStringLiteral("10"), 8, 1).value(QStringLiteral("cached")).toBool());
        QCOMPARE(renderer.frame().pixelColor(6, 0), QColor(Qt::white));
        QCOMPARE(renderer.frame().pixelColor(4, 0).alpha(), 0);

        QImage red(1, 1, QImage::Format_ARGB32_Premultiplied);
        red.fill(Qt::red);
        QVERIFY(renderer.setSourceImage(QStringLiteral("pixel"), red));
        QCOMPARE(renderer.cachedFrameCount(), 0);
        QVERIFY(!renderer.render(QStringLiteral("10"), 8, 1).value(QStringLiteral("cached")).toBool());
        QCOMPARE(renderer.frame().pixelColor(6, 0), QColor(Qt::red));
        QVERIFY(renderer.registerEffectProcessor(QStringLiteral("noop"), [](QImage &, const QVariantMap &, QString *) { return true; }));
        QCOMPARE(renderer.cachedFrameCount(), 0);
        renderer.setFrameCacheLimit(0);
        renderer.render(QStringLiteral("10"), 8, 1);
        QVERIFY(!renderer.render(QStringLiteral("10"), 8, 1).value(QStringLiteral("cached")).toBool());
    }

    void sampledFramesExportIdenticallyAfterSaveAndReopen()
    {
        AutomationModel animation;
        QVERIFY(addCurve(animation, QStringLiteral("x"), 40, {QVariantList{QStringLiteral("0"), 2.0},
                                                               QVariantList{QStringLiteral("100"), 30.0}}));
        QVERIFY(addCurve(animation, QStringLiteral("opacity"), 1, {QVariantList{QStringLiteral("0"), 1.0},
                                                                    QVariantList{QStringLiteral("100"), 0.4}}));
        const QString firstKey = animation.points(QStringLiteral("x")).at(0).toMap().value(QStringLiteral("id")).toString();
        QVERIFY(animation.applyEdit({{QStringLiteral("kind"), QStringLiteral("setHandle")}, {QStringLiteral("parameterId"), QStringLiteral("x")},
            {QStringLiteral("pointId"), firstKey}, {QStringLiteral("side"), QStringLiteral("out")}},
            {{QStringLiteral("handle"), QVariantMap{{QStringLiteral("x"), 0.2}, {QStringLiteral("y"), 0.9}}}}).value(QStringLiteral("ok")).toBool());
        const auto pair = [](const char *comp, const char *local) {
            return QVariantMap{{QStringLiteral("compTicks"), QString::fromLatin1(comp)},
                               {QStringLiteral("layerTicks"), QString::fromLatin1(local)}};
        };
        MotionComposition composition;
        composition.setAnimationModel(&animation);
        composition.setLayers({
            QVariantMap{{QStringLiteral("layerId"), QStringLiteral("group")},
                {QStringLiteral("transform"), QVariantMap{{QStringLiteral("x"), 3.0}, {QStringLiteral("y"), 2.0},
                    {QStringLiteral("scaleX"), 1.5}, {QStringLiteral("scaleY"), 1.5}}}},
            QVariantMap{{QStringLiteral("layerId"), QStringLiteral("art")}, {QStringLiteral("parentLayerId"), QStringLiteral("group")},
                {QStringLiteral("sourceId"), QStringLiteral("art")}, {QStringLiteral("zIndex"), 2},
                {QStringLiteral("timeOffsetTicks"), QStringLiteral("20")},
                {QStringLiteral("timeRemap"), QVariantList{pair("0", "0"), pair("60", "100"), pair("80", "100")}},
                {QStringLiteral("animatedProperties"), QVariantMap{{QStringLiteral("x"), QStringLiteral("x")},
                                                                   {QStringLiteral("opacity"), QStringLiteral("opacity")}}},
                {QStringLiteral("effects"), QVariantList{
                    QVariantMap{{QStringLiteral("effectId"), QStringLiteral("brightness")},
                                {QStringLiteral("parameters"), QVariantMap{{QStringLiteral("amount"), 0.25}}}},
                    QVariantMap{{QStringLiteral("effectId"), QStringLiteral("grayscale")}}}}},
            QVariantMap{{QStringLiteral("layerId"), QStringLiteral("block")}, {QStringLiteral("sourceId"), QStringLiteral("block")},
                {QStringLiteral("zIndex"), 1}, {QStringLiteral("transform"), QVariantMap{{QStringLiteral("x"), 10.0}, {QStringLiteral("y"), 12.0}}}}});
        MotionFrameRenderer renderer;
        renderer.setComposition(&composition);
        QVERIFY(addSources(renderer));

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QStringList ticks{QStringLiteral("0"), QStringLiteral("20"), QStringLiteral("35"), QStringLiteral("50"),
                                QStringLiteral("80"), QStringLiteral("100"), QStringLiteral("250")};
        QList<QImage> frames;
        QList<QByteArray> pngs;
        for (const QString &time : ticks) {
            QVERIFY2(renderer.render(time, 48, 32).value(QStringLiteral("ok")).toBool(), qPrintable(renderer.lastError()));
            frames.push_back(renderer.frame());
            pngs.push_back(exported(renderer, directory.filePath(QStringLiteral("a-%1.png").arg(time))));
            QVERIFY(!pngs.last().isEmpty());
        }
        QVERIFY(frames.at(1) != frames.at(3));  // the remapped key animation moves the layer
        QCOMPARE(frames.at(4), frames.at(5));    // remap freeze holds layer tick 100
        renderer.clearFrameCache();
        for (qsizetype i = 0; i < ticks.size(); ++i) {
            QVERIFY(!renderer.render(ticks.at(i), 48, 32).value(QStringLiteral("cached")).toBool());
            QCOMPARE(renderer.frame(), frames.at(i));
        }

        const QUrl manifest = QUrl::fromLocalFile(directory.filePath(QStringLiteral("motion.qtmotion")));
        QVERIFY(composition.saveToFile(manifest));
        AutomationModel reopenedAnimation;
        MotionComposition reopened;
        reopened.setAnimationModel(&reopenedAnimation);
        QVERIFY(reopened.loadFromFile(manifest).value(QStringLiteral("ok")).toBool());
        MotionFrameRenderer reopenedRenderer;
        reopenedRenderer.setComposition(&reopened);
        QVERIFY(addSources(reopenedRenderer));
        for (qsizetype i = 0; i < ticks.size(); ++i) {
            QVERIFY(reopenedRenderer.render(ticks.at(i), 48, 32).value(QStringLiteral("ok")).toBool());
            QCOMPARE(reopenedRenderer.frame(), frames.at(i));
            QCOMPARE(exported(reopenedRenderer, directory.filePath(QStringLiteral("b-%1.png").arg(ticks.at(i)))), pngs.at(i));
        }
    }
};

QTEST_GUILESS_MAIN(MotionFrameRendererTest)
#include "tst_motion_frame_renderer.moc"
