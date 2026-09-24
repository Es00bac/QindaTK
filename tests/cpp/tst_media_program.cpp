// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_fixture.h"
#include "media_program.h"
#include "render_queue_model.h"
#include "time_map.h"
#include "timeline_model.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;
using namespace MediaFixture;

namespace {
constexpr qint64 F = kFrame25;

// A timeline with its own 25 fps, 48 kHz TimeMap.
struct Sequence {
    TimeMap map;
    TimelineModel timeline;
    Sequence()
    {
        map.setSampleRate(48000);
        map.setFrameRate(25, 1);
        timeline.setTimeMap(&map);
    }
    bool load(const QVector<TimelineTrack> &tracks, const QVector<TimelineClip> &clips)
    {
        OperationError error;
        return timeline.setContent(kTimebase, tracks, clips, {}, &error);
    }
    std::shared_ptr<MediaProgram> program(const QHash<QString, QString> &paths, int channels = 2)
    {
        QString error;
        auto built = MediaProgram::fromTimeline(timeline, [paths](const QString &id) {
            return std::pair<QString, QString>{paths.value(id), QStringLiteral("1")};
        }, QSize(128, 72), channels, &error);
        if (!built) qWarning("%s", qPrintable(error));
        return built;
    }
};

qint64 codeAt(const MediaProgram &program, qint64 frame, MediaDecoderPool &pool)
{
    const MediaRenderedFrame rendered = program.render(frame, program.frameSize, pool, {});
    return rendered.error ? -2 : MediaExport::readTestSignalCode(rendered.image);
}
} // namespace

class TstMediaProgram final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        if (!MediaDecoder::available()) QSKIP("Built without the FFmpeg media backend");
    }

    void timelineProgramCutsAtExactFrames()
    {
        QTemporaryDir dir;
        const QString a = signal(dir.filePath(QStringLiteral("a.mkv")), 75);
        const QString b = signal(dir.filePath(QStringLiteral("b.mkv")), 100, 128, 72, 0, 50);
        QVERIFY(!a.isEmpty() && !b.isEmpty());
        Sequence sequence;
        const TimelineTrack v1 = track(1, QStringLiteral("video"));
        QVERIFY(sequence.load({v1}, {clip(1, v1, 0, 50 * F, 0, QStringLiteral("A")),
                                     clip(2, v1, 50 * F, 50 * F, 25 * F, QStringLiteral("B"))}));
        const auto program = sequence.program({{QStringLiteral("A"), a}, {QStringLiteral("B"), b}});
        QVERIFY(program);
        QCOMPARE(program->frameCount, qint64(100));
        QCOMPARE(program->video.size(), 2);
        QVERIFY(program->unresolved.isEmpty());
        QCOMPARE(program->frameStartTicks(50).value_or(-1), 50 * F);
        QCOMPARE(program->frameStartSample(50).value_or(-1), qint64(96000));
        QCOMPARE(program->frameAtSample(96000).value_or(-1), qint64(50));
        QCOMPARE(program->frameAtSample(95999).value_or(-1), qint64(49));
        QCOMPARE(program->endSample(), qint64(192000));
        MediaDecoderPool pool;
        const QList<std::pair<qint64, qint64>> expected{{0, 0}, {49, 49}, {50, 25}, {99, 74}, {1, 1}};
        for (const auto &[frame, code] : expected) {
            const MediaRenderedFrame rendered = program->render(frame, program->frameSize, pool, {});
            QVERIFY2(!rendered.error, qPrintable(rendered.error.message));
            QCOMPARE(rendered.sources.size(), 1);
            QCOMPARE(rendered.sources.first().frame, code);
            QCOMPARE(MediaExport::readTestSignalCode(rendered.image), code);
        }
        QCOMPARE(program->render(100, {}, pool, {}).error.code, ErrorCode::InvalidArgument);
    }

    void conformsFractionalRateSources()
    {
        QTemporaryDir dir;
        const QString film = signal(dir.filePath(QStringLiteral("film.mkv")), 48, 128, 72, 0, 0, {24000, 1001});
        QVERIFY(!film.isEmpty());
        Sequence sequence;
        const TimelineTrack v1 = track(1, QStringLiteral("video"));
        QVERIFY(sequence.load({v1}, {clip(1, v1, 0, 25 * F, 0, QStringLiteral("F"))}));
        const auto program = sequence.program({{QStringLiteral("F"), film}});
        QVERIFY(program);
        QCOMPARE(program->frameCount, qint64(25));
        MediaDecoderPool pool;
        // Program frame f at 25 fps holds the 23.976 fps frame that started last.
        const QList<std::pair<qint64, qint64>> expected{{0, 0}, {1, 0}, {2, 1}, {12, 11}, {24, 23}};
        for (const auto &[frame, code] : expected) QCOMPARE(codeAt(*program, frame, pool), code);
    }

    void layeringHiddenAndMissingClips()
    {
        QTemporaryDir dir;
        const QString a = signal(dir.filePath(QStringLiteral("a.mkv")), 75);
        const QString b = signal(dir.filePath(QStringLiteral("b.mkv")), 50, 128, 72, 0, 0);
        QVERIFY(!a.isEmpty() && !b.isEmpty());
        const QHash<QString, QString> paths{{QStringLiteral("A"), a}, {QStringLiteral("B"), b}};
        Sequence sequence;
        TimelineTrack front = track(1, QStringLiteral("video"));
        const TimelineTrack back = track(2, QStringLiteral("video"));
        const TimelineClip base = clip(1, back, 0, 75 * F, 0, QStringLiteral("A"));
        const TimelineClip over = clip(2, front, 25 * F, 25 * F, 0, QStringLiteral("B"));
        QVERIFY(sequence.load({front, back}, {base, over}));
        auto program = sequence.program(paths);
        QVERIFY(program);
        MediaDecoderPool pool;
        const MediaRenderedFrame layered = program->render(30, program->frameSize, pool, {});
        QVERIFY(!layered.error);
        QCOMPARE(layered.sources.size(), 2);
        QCOMPARE(layered.sources.at(0).resourceId, QStringLiteral("A"));
        QCOMPARE(layered.sources.at(1).resourceId, QStringLiteral("B"));
        QCOMPARE(MediaExport::readTestSignalCode(layered.image), qint64(5)); // the front track wins
        QCOMPARE(codeAt(*program, 10, pool), qint64(10));

        front.hidden = true;
        QVERIFY(sequence.load({front, back}, {base, over}));
        program = sequence.program(paths);
        QCOMPARE(codeAt(*program, 30, pool), qint64(30));

        front.hidden = false;
        QVERIFY(sequence.load({front, back}, {base, over}));
        program = sequence.program({{QStringLiteral("A"), a}});
        QCOMPARE(program->unresolved, QStringList{QStringLiteral("B")});
        const MediaRenderedFrame missing = program->render(30, program->frameSize, pool, {});
        QVERIFY(!missing.error);
        QCOMPARE(missing.missingResourceIds, QStringList{QStringLiteral("B")});
        QCOMPARE(MediaExport::readTestSignalCode(missing.image), qint64(30));

        const TimelineClip late = clip(2, front, 25 * F, 25 * F, 60 * F, QStringLiteral("B"));
        QVERIFY(sequence.load({front, back}, {base, late}));
        program = sequence.program(paths);
        const MediaRenderedFrame outside = program->render(30, program->frameSize, pool, {});
        QCOMPARE(outside.outOfRangeClipIds, QStringList{late.id.toString(QUuid::WithoutBraces)});
        QCOMPARE(MediaExport::readTestSignalCode(outside.image), qint64(30));
    }

    void audioMixFollowsTrackState()
    {
        QTemporaryDir dir;
        const QString a = signal(dir.filePath(QStringLiteral("a.mkv")), 75, 128, 72, 2, 25);
        QVERIFY(!a.isEmpty());
        const QHash<QString, QString> paths{{QStringLiteral("A"), a}};
        Sequence sequence;
        TimelineTrack first = track(3, QStringLiteral("audio"));
        TimelineTrack second = track(4, QStringLiteral("audio"));
        const TimelineClip atZero = clip(3, first, 0, 75 * F, 0, QStringLiteral("A"));
        const TimelineClip delayed = clip(4, second, 25 * F, 50 * F, 0, QStringLiteral("A"));
        QVERIFY(sequence.load({first}, {atZero}));
        auto program = sequence.program(paths);
        QCOMPARE(program->audio.size(), 1);
        MediaDecoderPool pool;
        MediaAudioMix mix = program->mixAudio(48000, 1920, pool, {});
        QVERIFY2(!mix.error, qPrintable(mix.error.message));
        QCOMPARE(mix.samples.size(), 3840);
        QVERIFY(peak(mix.samples, 0, mix.samples.size()) > 0.3f); // the pop of frame 25
        QCOMPARE(peak(program->mixAudio(46080, 1920, pool, {}).samples, 0, 3840), 0.0f);

        QVERIFY(sequence.load({second}, {delayed}));
        program = sequence.program(paths);
        QVERIFY(peak(program->mixAudio(48000, 1920, pool, {}).samples, 0, 3840) > 0.3f); // media frame 0
        QCOMPARE(peak(program->mixAudio(0, 1920, pool, {}).samples, 0, 3840), 0.0f); // before the clip

        first.muted = true;
        QVERIFY(sequence.load({first}, {atZero}));
        program = sequence.program(paths);
        QVERIFY(program->audio.isEmpty());
        QCOMPARE(peak(program->mixAudio(48000, 1920, pool, {}).samples, 0, 3840), 0.0f);

        first.muted = false;
        second.solo = true;
        QVERIFY(sequence.load({first, second}, {atZero, delayed}));
        program = sequence.program(paths);
        QCOMPARE(program->audio.size(), 1);
        QCOMPARE(program->audio.first().clipId, delayed.id.toString(QUuid::WithoutBraces));
    }

    void exportWritesVerifiedSequence()
    {
        if (!MediaEncoder::available()) QSKIP("The FFV1/PCM encoder is unavailable");
        QTemporaryDir dir;
        const QString a = signal(dir.filePath(QStringLiteral("a.mkv")), 75, 128, 72, 2, 25);
        const QString b = signal(dir.filePath(QStringLiteral("b.mkv")), 100, 128, 72, 2, 50);
        QVERIFY(!a.isEmpty() && !b.isEmpty());
        Sequence sequence;
        const TimelineTrack v1 = track(1, QStringLiteral("video"));
        const TimelineTrack a1 = track(3, QStringLiteral("audio"));
        QVERIFY(sequence.load({v1, a1}, {clip(1, v1, 0, 50 * F, 0, QStringLiteral("A")),
                                         clip(2, v1, 50 * F, 50 * F, 25 * F, QStringLiteral("B")),
                                         clip(3, a1, 0, 50 * F, 0, QStringLiteral("A")),
                                         clip(4, a1, 50 * F, 50 * F, 25 * F, QStringLiteral("B"))}));
        const auto program = sequence.program({{QStringLiteral("A"), a}, {QStringLiteral("B"), b}});
        QVERIFY(program);
        const QString out = dir.filePath(QStringLiteral("program.mkv"));
        const MediaExportResult result = MediaExport::exportProgram(*program, out, 0, program->frameCount);
        QVERIFY2(!result.error, qPrintable(result.error.message));
        QCOMPARE(result.frames, qint64(100));
        QCOMPARE(result.samples, qint64(192000));

        MediaDecoder decoder;
        const OperationError opened = decoder.open(out);
        QVERIFY2(!opened, qPrintable(opened.message));
        const MediaSourceInfoPtr info = decoder.info();
        QCOMPARE(info->frameCount(), qint64(100));
        QVERIFY(info->constantRate);
        QCOMPARE(info->nominalRate.numerator, qint64(25));
        QCOMPARE(info->audioSampleCount, qint64(192000));
        QCOMPARE(info->audioChannels, 2);
        const QList<std::pair<qint64, qint64>> codes{{0, 0}, {25, 25}, {49, 49}, {50, 25}, {74, 49},
                                                     {75, 50}, {99, 74}};
        for (const auto &[frame, code] : codes)
            QCOMPARE(MediaExport::readTestSignalCode(decoder.decodeFrame(frame, {}).image), code);
        // Pops (white picture + tone) land on program frames 0, 25 and 75.
        for (const qint64 frame : {0, 25, 75})
            QCOMPARE(decoder.decodeFrame(frame, {}).image.pixelColor(5, 5), QColor(255, 255, 255));
        for (const qint64 frame : {24, 50, 74})
            QCOMPARE(decoder.decodeFrame(frame, {}).image.pixelColor(5, 5), QColor(191, 191, 191));
        for (const qint64 frame : {25, 75}) {
            const MediaAudioBlock edge = decoder.readAudio(frame * 1920 - 10, 20, 48000, 2);
            QVERIFY2(!edge.error, qPrintable(edge.error.message));
            QCOMPARE(peak(edge.samples, 0, 20), 0.0f);
            QVERIFY(peak(edge.samples, 20, 20) > 0.1f);
        }
        QCOMPARE(peak(decoder.readAudio(50 * 1920, 1920, 48000, 2).samples, 0, 3840), 0.0f);

        // Missing media, cancellation and a bad range leave no output file.
        const auto partial = sequence.program({{QStringLiteral("A"), a}});
        const QString missing = dir.filePath(QStringLiteral("missing.mkv"));
        const MediaExportResult offline = MediaExport::exportProgram(*partial, missing, 0, 10);
        QCOMPARE(offline.error.code, ErrorCode::Unavailable);
        QCOMPARE(offline.missingResourceIds, QStringList{QStringLiteral("B")});
        QVERIFY(!QFile::exists(missing));
        CancellationSource stop;
        const QString cancelled = dir.filePath(QStringLiteral("cancelled.mkv"));
        const MediaExportResult stopped = MediaExport::exportProgram(*program, cancelled, 0, 100, stop.token(),
                                                                     [&stop](double) { stop.cancel(); });
        QCOMPARE(stopped.error.code, ErrorCode::Cancelled);
        QVERIFY(!QFile::exists(cancelled));
        QCOMPARE(MediaExport::exportProgram(*program, dir.filePath(QStringLiteral("range.mkv")), 90, 20).error.code,
                 ErrorCode::InvalidArgument);
    }

    void renderQueueExportsTimelineProgram()
    {
        if (!MediaEncoder::available()) QSKIP("The FFV1/PCM encoder is unavailable");
        QTemporaryDir dir;
        const QString a = signal(dir.filePath(QStringLiteral("a.mkv")), 30, 128, 72, 2, 25);
        QVERIFY(!a.isEmpty());
        Sequence sequence;
        const TimelineTrack v1 = track(1, QStringLiteral("video"));
        const TimelineTrack a1 = track(3, QStringLiteral("audio"));
        QVERIFY(sequence.load({v1, a1}, {clip(1, v1, 0, 30 * F, 0, QStringLiteral("A")),
                                         clip(3, a1, 0, 30 * F, 0, QStringLiteral("A"))}));
        const auto program = sequence.program({{QStringLiteral("A"), a}});
        QVERIFY(program);
        RenderQueueModel queue;
        QVERIFY(queue.open(dir.filePath(QStringLiteral("queue.json"))));
        QString error;
        QVariantMap snapshot = MediaExport::queueSnapshot(*program, dir.filePath(QStringLiteral("export-1")),
                                                          QStringLiteral("Program"), {}, &error);
        QVERIFY2(!snapshot.isEmpty(), qPrintable(error));
        snapshot.insert(QStringLiteral("id"), QStringLiteral("media-job"));
        QCOMPARE(queue.enqueueJob(snapshot), QStringLiteral("media-job"));
        QTRY_COMPARE_WITH_TIMEOUT(queue.job(QStringLiteral("media-job")).value(QStringLiteral("state")).toString(),
                                  QStringLiteral("succeeded"), 60000);
        QVERIFY(queue.verifyOutput(QStringLiteral("media-job")));
        MediaDecoder decoder;
        QVERIFY(!decoder.open(QDir(dir.filePath(QStringLiteral("export-1"))).filePath(QStringLiteral("program.mkv"))));
        QCOMPARE(decoder.info()->frameCount(), qint64(30));
        QCOMPARE(MediaExport::readTestSignalCode(decoder.decodeFrame(10, {}).image), qint64(10));

        // A source that changes after the job was captured fails the job.
        QVariantMap stale = MediaExport::queueSnapshot(*program, dir.filePath(QStringLiteral("export-2")),
                                                       QStringLiteral("Stale"), {}, &error);
        QVERIFY(!stale.isEmpty());
        stale.insert(QStringLiteral("id"), QStringLiteral("stale-job"));
        QFile source(a);
        QVERIFY(source.open(QIODevice::Append));
        QCOMPARE(source.write("x"), qint64(1));
        source.close();
        QCOMPARE(queue.enqueueJob(stale), QStringLiteral("stale-job"));
        QTRY_COMPARE_WITH_TIMEOUT(queue.job(QStringLiteral("stale-job")).value(QStringLiteral("state")).toString(),
                                  QStringLiteral("failed"), 60000);
        QVERIFY(queue.job(QStringLiteral("stale-job")).value(QStringLiteral("error")).toString()
                    .contains(QStringLiteral("changed or went offline")));
    }

    void programJsonRoundTrip()
    {
        QTemporaryDir dir;
        const QString a = signal(dir.filePath(QStringLiteral("a.mkv")), 25);
        QVERIFY(!a.isEmpty());
        Sequence sequence;
        const TimelineTrack v1 = track(1, QStringLiteral("video"));
        QVERIFY(sequence.load({v1}, {clip(1, v1, 5 * F, 20 * F, 3 * F, QStringLiteral("A"))}));
        const auto program = sequence.program({{QStringLiteral("A"), a}});
        QString error;
        const auto copy = MediaProgram::fromJson(program->toJson(), &error);
        QVERIFY2(copy, qPrintable(error));
        QCOMPARE(copy->frameCount, program->frameCount);
        QCOMPARE(copy->video.size(), 1);
        QCOMPARE(copy->video.first().start, 5 * F);
        QCOMPARE(copy->video.first().sourceOffset, 3 * F);
        QCOMPARE(copy->video.first().uri, a);
        QCOMPARE(copy->frameRate.numerator, qint64(25));
        QJsonObject broken = program->toJson();
        broken.insert(QStringLiteral("frameCount"), QStringLiteral("12x"));
        QVERIFY(!MediaProgram::fromJson(broken, &error));
    }
};

QTEST_MAIN(TstMediaProgram)
#include "tst_media_program.moc"
