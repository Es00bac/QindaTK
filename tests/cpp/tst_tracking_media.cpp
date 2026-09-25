// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-075 footage path: tracking decoded video frames (WP-070 MediaDecoder)
// and rendering the tracked mask through MediaFrameFeed.
#include "media_encoder.h"
#include "media_fixture.h"
#include "media_frame_feed.h"
#include "motion_composition.h"
#include "motion_frame_renderer.h"
#include "resource_resolver.h"
#include "tracking_roto_model.h"

#include <QColorSpace>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;
using namespace MediaFixture;

namespace {
constexpr int kWidth = 96;
constexpr int kHeight = 64;

// Feature centre in frame k: 2 px right and 1 px down per frame.
QPoint featureAt(int k) { return {20 + 2 * k, 16 + k}; }

// A textured 11x11 feature on a flat background; `occluded` frames omit it.
QImage footageFrame(int k, bool occluded)
{
    QImage image(kWidth, kHeight, QImage::Format_RGB32);
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    image.fill(QColor(16, 16, 16));
    if (occluded) return image;
    const QPoint centre = featureAt(k);
    for (int y = -5; y <= 5; ++y)
        for (int x = -5; x <= 5; ++x) {
            const int v = 55 + ((x + 5) * 17 + (y + 5) * 29) % 175;
            image.setPixel(centre.x() + x, centre.y() + y, qRgb(v, v, v));
        }
    return image;
}

// Lossless FFV1 clip of `frames` frames; frames 6 and 7 are occluded.
QString writeClip(const QString &path, int frames)
{
    MediaEncoder encoder;
    MediaEncodeSettings settings;
    settings.frameSize = QSize(kWidth, kHeight);
    settings.frameRate = {25, 1};
    settings.channels = 0;
    if (encoder.open(path, settings)) return {};
    for (int k = 0; k < frames; ++k)
        if (encoder.writeVideo(footageFrame(k, k == 6 || k == 7))) return {};
    return encoder.finish() ? QString() : path;
}

QVariantList square(double left, double top, double size)
{
    QVariantList points;
    const double xs[] = {left, left + size, left + size, left};
    const double ys[] = {top, top, top + size, top + size};
    for (int i = 0; i < 4; ++i)
        points.append(QVariantMap{{QStringLiteral("pointId"), QStringLiteral("p%1").arg(i)},
                                  {QStringLiteral("x"), xs[i] / kWidth}, {QStringLiteral("y"), ys[i] / kHeight}});
    return points;
}
} // namespace

class TstTrackingMedia final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        if (!MediaDecoder::available() || !MediaEncoder::available())
            QSKIP("Built without the FFmpeg media backend");
    }

    void decodedFootageTracksAndMasksTheFedFrame()
    {
        QTemporaryDir dir;
        const QString clip = writeClip(dir.filePath(QStringLiteral("feature.mkv")), 12);
        QVERIFY(!clip.isEmpty());

        TrackingRotoModel model;
        QSignalSpy loaded(&model, &TrackingRotoModel::frameLoadFinished);
        QVERIFY(model.loadMediaFrames(fileUri(clip), 0, 12));
        QVERIFY(model.loadingFrames());
        QTRY_COMPARE(loaded.count(), 1);
        QVERIFY2(loaded.first().at(0).toBool(), qPrintable(loaded.first().at(1).toString()));
        QCOMPARE(model.loadedFrames().size(), 12);
        QCOMPARE(model.frameLoadProgress(), 1.0);

        const QString id = model.addTrack(QStringLiteral("feature"), 0, 20, 16, 5, 4);
        QVERIFY(model.startAnalysis(id, 1, 11, 3));
        QTRY_VERIFY(!model.analysisRunning());
        const QVariantMap track = model.tracks().first().toMap();
        QCOMPARE(track.value(QStringLiteral("state")).toString(), QStringLiteral("ready"));
        const QVariantList points = track.value(QStringLiteral("previewPoints")).toList();
        QCOMPARE(points.size(), 11);
        for (int k = 1; k <= 11; ++k) {
            const QVariantMap point = points.at(k - 1).toMap();
            const bool occluded = k == 6 || k == 7;
            // Lossless decode: every visible frame matches exactly; occluded
            // frames are lost and hold frame 5's trusted position.
            const QPoint expected = featureAt(occluded ? 5 : k);
            QCOMPARE(point.value(QStringLiteral("frame")).toInt(), k);
            QCOMPARE(point.value(QStringLiteral("state")).toString(),
                     occluded ? QStringLiteral("lost") : QStringLiteral("tracked"));
            QCOMPARE(point.value(QStringLiteral("x")).toDouble(), double(expected.x()));
            QCOMPARE(point.value(QStringLiteral("y")).toDouble(), double(expected.y()));
        }
        QVERIFY(model.acceptAnalysis(id));

        const QString maskId = model.addMask(QStringLiteral("feature matte"));
        QVERIFY(model.setMaskKeyframe(maskId, 0, square(15, 11, 10)));
        const QVariantMap applied = model.applyTrackToMask(maskId, 0, id, 0, 11);
        QVERIFY2(applied.value(QStringLiteral("ok")).toBool(), qPrintable(model.lastError()));
        QCOMPARE(applied.value(QStringLiteral("skippedFrames")).toList(), (QVariantList{6, 7}));
        QCOMPARE(applied.value(QStringLiteral("keyedFrames")).toList().size(), 9);

        // The layer starts 2 frames into the composition, so composition
        // frame 11 shows (and masks) source frame 9.
        ResourceResolver resolver;
        QVERIFY(resolver.registerResource(QStringLiteral("clip"), fileUri(clip)));
        MotionComposition composition;
        composition.setLayers({QVariantMap{{QStringLiteral("layerId"), QStringLiteral("shot")},
            {QStringLiteral("sourceId"), QStringLiteral("clip")}, {QStringLiteral("maskId"), maskId},
            {QStringLiteral("zIndex"), 0}, {QStringLiteral("visible"), true},
            {QStringLiteral("timeOffsetTicks"), QString::number(2 * kFrame25)},
            {QStringLiteral("transform"), QVariantMap{{QStringLiteral("x"), 0}, {QStringLiteral("y"), 0},
                                                       {QStringLiteral("opacity"), 1.0}}}}});
        MotionFrameRenderer renderer;
        renderer.setComposition(&composition);
        renderer.setRotoModel(&model);
        MediaFrameFeed feed;
        feed.setResolver(&resolver);
        feed.setComposition(&composition);
        feed.setRenderer(&renderer);
        QSignalSpy prepared(&feed, &MediaFrameFeed::prepared);
        const QString ticks = QString::number(11 * kFrame25);
        QVERIFY(!feed.prepare(ticks).isEmpty());
        QTRY_COMPARE(prepared.count(), 1);
        QCOMPARE(renderer.sourceFrame(QStringLiteral("clip")), QStringLiteral("9"));
        const QVariantMap frame = renderer.render(ticks, kWidth, kHeight);
        QVERIFY2(frame.value(QStringLiteral("ok")).toBool(), qPrintable(renderer.lastError()));
        QCOMPARE(frame.value(QStringLiteral("drawnLayers")).toInt(), 1);
        // Frame 9 mask: [33,43]x[20,30]. Frame 11's would be [37,47]x[22,32].
        const QImage out = renderer.frame();
        QCOMPARE(out.pixelColor(featureAt(9)).alpha(), 255);
        QCOMPARE(out.pixelColor(34, 21).alpha(), 255);
        QCOMPARE(out.pixelColor(featureAt(0)).alpha(), 0);
        QCOMPARE(out.pixelColor(60, 50).alpha(), 0);
        QCOMPARE(qRed(out.pixel(featureAt(9))), qRed(footageFrame(9, false).pixel(featureAt(9))));
    }

    void frameLoadingFailsExplicitlyAndCancels()
    {
        QTemporaryDir dir;
        const QString clip = writeClip(dir.filePath(QStringLiteral("short.mkv")), 4);
        QVERIFY(!clip.isEmpty());
        TrackingRotoModel model;
        QSignalSpy finished(&model, &TrackingRotoModel::frameLoadFinished);

        QVERIFY(model.loadMediaFrames(fileUri(clip), 2, 5)); // past the last frame
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(!finished.last().at(0).toBool());
        QVERIFY(model.loadedFrames().isEmpty());
        QVERIFY(!model.lastError().isEmpty());

        QVERIFY(model.loadMediaFrames(dir.filePath(QStringLiteral("missing.mkv")), 0, 1));
        QTRY_COMPARE(finished.count(), 2);
        QVERIFY(!finished.last().at(0).toBool());

        QVERIFY(!model.loadMediaFrames(fileUri(clip), 0, 301));
        QVERIFY(model.loadMediaFrames(fileUri(clip), 0, 4));
        const QString id = model.addTrack(QStringLiteral("feature"), 0, 20, 16);
        QVERIFY(!model.startAnalysis(id, 1, 3)); // one worker job at a time
        QVERIFY(!model.loadMediaFrames(fileUri(clip), 0, 4));
        model.cancelFrameLoad();
        QVERIFY(!model.loadingFrames());
        QCOMPARE(finished.count(), 3);
        QVERIFY(!finished.last().at(0).toBool());
        QTest::qWait(100);
        QVERIFY(model.loadedFrames().isEmpty()); // late frames of a cancelled load never apply
        QCOMPARE(finished.count(), 3);

        QVERIFY(model.loadMediaFrames(fileUri(clip), 0, 4));
        QTRY_COMPARE(finished.count(), 4);
        QVERIFY(finished.last().at(0).toBool());
        QCOMPARE(model.loadedFrames(), (QVariantList{0, 1, 2, 3}));
    }
};

QTEST_MAIN(TstTrackingMedia)
#include "tst_tracking_media.moc"
