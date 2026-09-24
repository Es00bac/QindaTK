// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_fixture.h"
#include "media_frame_view.h"
#include "media_playback_engine.h"
#include "media_player.h"
#include "resource_resolver.h"
#include "time_map.h"
#include "timeline_model.h"
#include "transport_model.h"

#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <atomic>
#include <mutex>

using namespace QindaTK;
using namespace MediaFixture;

namespace {
constexpr qint64 F = kFrame25;

// One indexed test signal and its source program (48 kHz transport samples).
struct Source {
    QTemporaryDir dir;
    QString path;
    MediaProgramPtr program;
    bool open(qint64 frames, int channels = 0)
    {
        path = signal(dir.filePath(QStringLiteral("clip.mkv")), frames, 128, 72, channels, 25);
        if (path.isEmpty()) return false;
        MediaDecoder decoder;
        if (decoder.open(path)) return false;
        MediaClipSpan span;
        span.resourceId = QStringLiteral("clip");
        span.uri = path;
        span.revision = QStringLiteral("1");
        program = MediaProgram::forSource(span, path, false, decoder.info(), 48000, 2);
        return program && !program->isEmpty();
    }
    QString sample(qint64 frame) const
    {
        return QString::number(program->frameStartSample(frame).value_or(-1));
    }
};
} // namespace

class TstMediaPlayback final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        if (!MediaDecoder::available()) QSKIP("Built without the FFmpeg media backend");
    }

    void rapidSeeksNeverPresentASupersededFrame()
    {
        Source source;
        QVERIFY(source.open(400));
        MediaPlaybackEngine engine;
        engine.setClock(std::make_shared<MediaManualClock>());
        engine.setProgram(source.program);
        TransportModel transport;
        transport.setAdapter(&engine);
        QVERIFY(transport.canSeek());
        QTRY_COMPARE(engine.presentedFrame(), qint64(0));
        QTRY_COMPARE(engine.inFlight(), 0);
        const MediaPlaybackStatistics before = engine.statistics();
        QSignalSpy presented(&engine, &MediaPlaybackEngine::framePresented);
        QSignalSpy finished(&transport, &TransportModel::commandFinished);
        QStringList ids;
        for (const qint64 frame : {100, 7, 350}) ids << transport.requestSeek(source.sample(frame));
        for (const QString &id : std::as_const(ids)) QVERIFY(!id.isEmpty());
        QCOMPARE(engine.targetFrame(), qint64(350));
        QVERIFY(engine.pending());
        QTRY_COMPARE(engine.presentedFrame(), qint64(350));
        QTRY_COMPARE(engine.inFlight(), 0);
        QTRY_COMPARE(finished.count(), 3);
        for (const QList<QVariant> &arguments : std::as_const(finished)) QVERIFY(arguments.at(1).toBool());
        // Frames 100 and 7 were cancelled or arrived stale; neither was shown.
        QCOMPARE(presented.count(), 1);
        QCOMPARE(MediaExport::readTestSignalCode(engine.presented().image), qint64(350));
        QCOMPARE(engine.presented().sources.first().frame, qint64(350));
        const MediaPlaybackStatistics &after = engine.statistics();
        QCOMPARE(after.requested - before.requested, quint64(3));
        QCOMPARE((after.cancelled + after.stale) - (before.cancelled + before.stale), quint64(2));
        QCOMPARE(transport.positionSample(), source.sample(350));
        QVERIFY(!engine.pending());

        const QString refused = transport.requestSeek(QString::number(source.program->endSample() + 1));
        QVERIFY(!refused.isEmpty());
        QTRY_COMPARE(finished.count(), 4);
        QVERIFY(!finished.last().at(1).toBool());
        QCOMPARE(engine.presentedFrame(), qint64(350));
    }

    void playbackFollowsTheClock()
    {
        Source source;
        QVERIFY(source.open(50));
        auto clock = std::make_shared<MediaManualClock>();
        MediaPlaybackEngine engine;
        engine.setClock(clock);
        engine.setProgram(source.program);
        TransportModel transport;
        transport.setAdapter(&engine);
        QTRY_COMPARE(engine.presentedFrame(), qint64(0));
        QVERIFY(!transport.requestPlay().isEmpty());
        QVERIFY(engine.snapshot().playing);
        QTRY_VERIFY(transport.playing());
        clock->advanceNanoseconds(40000000);
        engine.tick();
        QCOMPARE(engine.positionSample(), qint64(1920));
        QCOMPARE(engine.targetFrame(), qint64(1));
        QTRY_COMPARE(engine.presentedFrame(), qint64(1));
        QCOMPARE(MediaExport::readTestSignalCode(engine.presented().image), qint64(1));
        clock->advanceNanoseconds(400000000);
        engine.tick();
        QCOMPARE(engine.targetFrame(), qint64(11));
        QCOMPARE(engine.statistics().dropped, quint64(9)); // frames 2-10 were never due on screen
        QTRY_COMPARE(engine.presentedFrame(), qint64(11));
        clock->advanceNanoseconds(10000000000LL);
        engine.tick();
        QVERIFY(!engine.snapshot().playing);
        QCOMPARE(engine.positionSample(), qint64(96000));
        QCOMPARE(engine.targetFrame(), qint64(49));
        QCOMPARE(engine.statistics().dropped, quint64(9 + 37));
        QTRY_COMPARE(engine.presentedFrame(), qint64(49));
        QTRY_VERIFY(!transport.playing());
        // Play at the end restarts from the first frame; stop keeps the position.
        QVERIFY(!transport.requestPlay().isEmpty());
        QCOMPARE(engine.positionSample(), qint64(0));
        QTRY_COMPARE(engine.presentedFrame(), qint64(0));
        QVERIFY(!transport.requestStop().isEmpty());
        QVERIFY(!engine.snapshot().playing);
        QCOMPARE(engine.positionSample(), qint64(0));
    }

    void loopWrapsInsideTheRange()
    {
        Source source;
        QVERIFY(source.open(50));
        auto clock = std::make_shared<MediaManualClock>();
        MediaPlaybackEngine engine;
        engine.setClock(clock);
        engine.setProgram(source.program);
        TransportModel transport;
        transport.setAdapter(&engine);
        QVERIFY(!transport.requestLoop(true, source.sample(10), source.sample(20)).isEmpty());
        QTRY_VERIFY(transport.loopEnabled());
        QVERIFY(!transport.requestSeek(source.sample(10)).isEmpty());
        QTRY_COMPARE(engine.presentedFrame(), qint64(10));
        QVERIFY(!transport.requestPlay().isEmpty());
        clock->advanceNanoseconds(500000000); // 24000 samples: 19200 + 24000 wraps to 24000
        engine.tick();
        QVERIFY(engine.snapshot().playing);
        QCOMPARE(engine.positionSample(), qint64(24000));
        QCOMPARE(engine.targetFrame(), qint64(12));
        QTRY_COMPARE(engine.presentedFrame(), qint64(12));
    }

    void decodeWorkerSkipsCancelledJobs()
    {
        Source source;
        QVERIFY(source.open(10));
        std::mutex mutex;
        QVector<MediaWorkerResult> results;
        std::atomic_int count{0};
        {
            MediaDecodeWorker worker([&](MediaWorkerResult result) {
                const std::lock_guard<std::mutex> lock(mutex);
                results.append(result);
                ++count;
            });
            MediaWorkerJob skipped;
            skipped.serial = 1;
            skipped.program = source.program;
            skipped.frame = 3;
            skipped.cancel.cancel();
            worker.submit(skipped);
            MediaWorkerJob decoded;
            decoded.serial = 2;
            decoded.program = source.program;
            decoded.frame = 4;
            decoded.outputSize = QSize(128, 72);
            worker.submit(decoded);
            QTRY_COMPARE(count.load(), 2);
        }
        const std::lock_guard<std::mutex> lock(mutex);
        QCOMPARE(results.at(0).serial, quint64(1));
        QVERIFY(results.at(0).cancelled);
        QVERIFY(results.at(0).rendered.image.isNull());
        QCOMPARE(results.at(1).serial, quint64(2));
        QVERIFY(!results.at(1).cancelled);
        QCOMPARE(MediaExport::readTestSignalCode(results.at(1).rendered.image), qint64(4));
    }

    void frameViewShowsThePresentedFrame()
    {
        Source source;
        QVERIFY(source.open(60));
        MediaPlayer player;
        player.setResourceId(source.path); // no resolver: the ID is the path
        QTRY_COMPARE(player.state(), QStringLiteral("ready"));
        QTRY_COMPARE(player.presentedFrame(), QStringLiteral("0"));
        QTRY_VERIFY(player.transport()->canSeek());
        QQuickWindow window;
        window.resize(128, 72);
        MediaFrameView view(window.contentItem());
        view.setSize(QSizeF(128, 72));
        view.setPlayer(&player);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QVERIFY(!player.seekFrame(QStringLiteral("37")).isEmpty());
        QTRY_COMPARE(player.presentedFrame(), QStringLiteral("37"));
        QTRY_COMPARE(view.displayedFrame(), QStringLiteral("37"));
        QTRY_VERIFY(view.textureUploads() >= 1);
        QVERIFY(view.frameTransfer() == QLatin1String("software") || view.frameTransfer() == QLatin1String("gpu"));
        QCOMPARE(view.frameRect(), QRectF(0, 0, 128, 72));
        QCOMPARE(MediaExport::readTestSignalCode(window.grabWindow()), qint64(37));
    }

    void playerSourceModeSeeksRelinksAndGoesOffline()
    {
        QTemporaryDir dir;
        const QString first = signal(dir.filePath(QStringLiteral("first.mkv")), 50);
        const QString second = signal(dir.filePath(QStringLiteral("second.mkv")), 30);
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        ResourceResolver resolver;
        QVERIFY(resolver.registerResource(QStringLiteral("clip"), fileUri(first)));
        MediaPlayer player;
        player.setResolver(&resolver);
        player.setResourceId(QStringLiteral("clip"));
        QTRY_COMPARE(player.state(), QStringLiteral("ready"));
        QCOMPARE(player.frameCount(), QStringLiteral("50"));
        QCOMPARE(player.mediaInfo().value(QStringLiteral("codec")).toString(), QStringLiteral("ffv1"));
        QTRY_COMPARE(player.presentedFrame(), QStringLiteral("0"));
        QTRY_VERIFY(player.transport()->canSeek());
        QVERIFY(!player.seekFrame(QStringLiteral("12")).isEmpty());
        QTRY_COMPARE(player.presentedFrame(), QStringLiteral("12"));
        QCOMPARE(MediaExport::readTestSignalCode(player.presentedImage()), qint64(12));
        QCOMPARE(player.presentedTicks(), QString::number(12 * F));
        const QString revision = resolver.lookup(QStringLiteral("clip")).value(QStringLiteral("revision")).toString();
        QCOMPARE(player.presentedFrameKey(), QStringLiteral("clip@%1#12").arg(revision));
        QVERIFY(!player.stepFrame(1).isEmpty());
        QTRY_COMPARE(player.presentedFrame(), QStringLiteral("13"));

        // Relinking changes the location; the playhead and identity hold.
        QVERIFY(resolver.relink(QStringLiteral("clip"), fileUri(second)));
        QTRY_COMPARE(player.frameCount(), QStringLiteral("30"));
        const QString relinked = resolver.lookup(QStringLiteral("clip")).value(QStringLiteral("revision")).toString();
        QVERIFY(relinked != revision);
        QTRY_COMPARE(player.presentedFrameKey(), QStringLiteral("clip@%1#13").arg(relinked));
        QCOMPARE(MediaExport::readTestSignalCode(player.presentedImage()), qint64(13));

        // Offline media never leaves the previous frame on screen.
        QVERIFY(QFile::remove(second));
        QVERIFY(resolver.refresh(QStringLiteral("clip")));
        QCOMPARE(player.state(), QStringLiteral("missing"));
        QCOMPARE(player.presentedFrame(), QString());
        QVERIFY(player.presentedImage().isNull());
        QTRY_VERIFY(!player.transport()->canPlay());
    }

    void playerProgramModeFollowsTimelineEdits()
    {
        QTemporaryDir dir;
        const QString a = signal(dir.filePath(QStringLiteral("a.mkv")), 50);
        QVERIFY(!a.isEmpty());
        ResourceResolver resolver;
        QVERIFY(resolver.registerResource(QStringLiteral("A"), fileUri(a)));
        TimeMap map;
        QVERIFY(map.setFrameRate(25, 1));
        TimelineModel timeline;
        timeline.setTimeMap(&map);
        const TimelineTrack v1 = track(1, QStringLiteral("video"));
        OperationError error;
        QVERIFY(timeline.setContent(kTimebase, {v1}, {clip(1, v1, 0, 50 * F, 0, QStringLiteral("A"))}, {}, &error));
        MediaPlayer player;
        player.setResolver(&resolver);
        player.setFrameWidth(128);
        player.setFrameHeight(72);
        player.setTimeline(&timeline);
        QCOMPARE(player.state(), QStringLiteral("ready"));
        QCOMPARE(player.frameCount(), QStringLiteral("50"));
        QTRY_COMPARE(player.presentedFrame(), QStringLiteral("0"));
        QTRY_VERIFY(player.transport()->canSeek());
        QVERIFY(!player.seekFrame(QStringLiteral("20")).isEmpty());
        QTRY_COMPARE(player.presentedFrame(), QStringLiteral("20"));
        QCOMPARE(MediaExport::readTestSignalCode(player.presentedImage()), qint64(20));
        // A committed edit re-renders the frame under the playhead.
        QVERIFY(timeline.setContent(kTimebase, {v1}, {clip(1, v1, 0, 50 * F, 10 * F, QStringLiteral("A"))}, {}, &error));
        QTRY_COMPARE(MediaExport::readTestSignalCode(player.presentedImage()), qint64(30));
        QCOMPARE(player.presentedFrame(), QStringLiteral("20"));
        QCOMPARE(player.presentedSources().first().toMap().value(QStringLiteral("frame")).toString(), QStringLiteral("30"));
        QCOMPARE(player.presentedTimecode(), QStringLiteral("00:00:00:20"));
        const QVariantMap job = player.exportJob(dir.filePath(QStringLiteral("out")), QStringLiteral("Program"));
        QCOMPARE(job.value(QStringLiteral("providerId")).toString(), MediaExport::providerId());
        QCOMPARE(job.value(QStringLiteral("settings")).toMap().value(QStringLiteral("frameCount")).toString(),
                 QStringLiteral("50"));
    }

    void audioAndPictureShareTheTransportPosition()
    {
        Source source;
        QVERIFY(source.open(50, 2));
        MediaPlaybackEngine engine;
        engine.setClock(std::make_shared<MediaManualClock>());
        engine.setProgram(source.program);
        TransportModel transport;
        transport.setAdapter(&engine);
        QVERIFY(!transport.requestSeek(source.sample(25)).isEmpty());
        QTRY_COMPARE(engine.presentedFrame(), qint64(25));
        QCOMPARE(engine.presented().image.pixelColor(5, 5), QColor(255, 255, 255)); // the flash
        MediaDecoderPool pool;
        const MediaAudioMix at = source.program->mixAudio(engine.positionSample(), 1920, pool, {});
        QVERIFY2(!at.error, qPrintable(at.error.message));
        QVERIFY(peak(at.samples, 0, at.samples.size()) > 0.3f); // the pop
        const MediaAudioMix before = source.program->mixAudio(engine.positionSample() - 1920, 1920, pool, {});
        QCOMPARE(peak(before.samples, 0, before.samples.size()), 0.0f);
    }
};

QTEST_MAIN(TstMediaPlayback)
#include "tst_media_playback.moc"
