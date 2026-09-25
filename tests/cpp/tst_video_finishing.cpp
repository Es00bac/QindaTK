// SPDX-License-Identifier: LGPL-3.0-or-later
#include "caption_frame_renderer.h"
#include "caption_track_model.h"
#include "color_grade_edit_session.h"
#include "media_captions.h"
#include "media_finishing.h"
#include "media_fixture.h"
#include "media_player.h"
#include "media_program.h"
#include "video_finishing.h"
#include "video_scopes_model.h"

#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

using namespace QindaTK;

// WP-072 on real media (docs/media-playback.md "Finishing", D-126/D-127):
// the grade and captions a program shows are the ones its export writes,
// embedded captions round-trip with their language, and scopes measure
// decoded frames under stated conventions and keep each frame's identity.
namespace {
constexpr int kWidth = 448; // seven 64-pixel bars
constexpr int kHeight = 72;

struct Signal {
    QTemporaryDir dir;
    QString path;
    MediaProgramPtr program;
    bool open(qint64 frames)
    {
        path = MediaFixture::signal(dir.filePath(QStringLiteral("bars.mkv")), frames, kWidth, kHeight, 2, 0);
        if (path.isEmpty()) return false;
        MediaDecoder decoder;
        if (decoder.open(path)) return false;
        MediaClipSpan span;
        span.resourceId = QStringLiteral("bars");
        span.uri = path;
        span.revision = QStringLiteral("1");
        program = MediaProgram::forSource(span, path, false, decoder.info(), 48000, 2);
        return program && !program->isEmpty();
    }
    std::shared_ptr<MediaProgram> finished(const MediaFinishing &finishing) const
    {
        auto copy = std::make_shared<MediaProgram>(*program);
        copy->finishing = finishing;
        return copy;
    }
};

QImage decodedFrame(const QString &path, qint64 index)
{
    MediaDecoder decoder;
    if (decoder.open(path)) return {};
    return decoder.decodeFrame(index, QSize()).image;
}

int differingPixels(const QImage &left, const QImage &right)
{
    if (left.isNull() || left.size() != right.size()) return -1;
    int count = 0;
    for (int y = 0; y < left.height(); ++y)
        for (int x = 0; x < left.width(); ++x)
            if ((left.pixel(x, y) & 0xffffff) != (right.pixel(x, y) & 0xffffff)) ++count;
    return count;
}

CaptionCueRecord cue(const QString &id, qint64 in, qint64 out, const QString &text, int lane)
{
    CaptionCueRecord record;
    record.id = id;
    record.inMs = in;
    record.outMs = out;
    record.text = text;
    record.language = QStringLiteral("ja-JP");
    record.lane = lane;
    return record;
}

// Centre waveform column of bar b (seven bars over kWaveColumns columns).
int barColumn(int bar)
{
    return int(std::lround((bar + 0.5) * VideoScopeAnalysis::kWaveColumns / 7.0));
}
} // namespace

class VideoFinishingTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        if (!MediaDecoder::available()) QSKIP("Built without the FFmpeg media backend");
    }

    void gradeMatchesTheSessionPreviewAndTheExport()
    {
        Signal signal;
        QVERIFY(signal.open(10));
        const QImage source = decodedFrame(signal.path, 3);
        QVERIFY(!source.isNull());
        ColorGradeEditSession session;
        session.setFrame(QStringLiteral("bars"), QStringLiteral("bars#3"), source, MediaFinishing::gradeContext());
        QVERIFY(session.setWheelValue(QStringLiteral("lift"), 0.2, -0.1));
        QVERIFY(session.setWheelValue(QStringLiteral("gamma"), 0.1, 0.05));
        QVERIFY(session.setWheelValue(QStringLiteral("gain"), -0.05, 0.1));
        QVERIFY(session.setExposure(0.5));
        QVERIFY(session.canUndo());

        VideoFinishing finishing;
        finishing.setGradeSession(&session);
        const auto graded = signal.finished(finishing.value(false));
        QVERIFY(graded->finishing.changesPictures());
        QVERIFY(graded->cacheKey() != signal.program->cacheKey());
        MediaDecoderPool pool;
        const MediaRenderedFrame rendered = graded->render(3, graded->frameSize, pool, {});
        QVERIFY2(!rendered.error, qPrintable(rendered.error.message));
        QCOMPARE(rendered.image.format(), QImage::Format_RGB32);
        // The program pipeline and the session preview produce the same pixels.
        QCOMPARE(differingPixels(rendered.image, session.processedImage().convertToFormat(QImage::Format_RGB32)), 0);
        QVERIFY(differingPixels(rendered.image, source) > 0);

        QTemporaryDir out;
        const QString file = out.filePath(QStringLiteral("graded.mkv"));
        const MediaExportResult exported = MediaExport::exportProgram(*graded, file, 0, 10);
        QVERIFY2(!exported.error, qPrintable(exported.error.message));
        QCOMPARE(exported.frames, qint64(10));
        QCOMPARE(exported.subtitles, qint64(0));
        // Lossless export: the decoded file frame is the monitor frame.
        QCOMPARE(differingPixels(decodedFrame(file, 3), rendered.image), 0);

        // Undo returns the committed grade the next export uses; a neutral
        // grade is an exact identity.
        QVERIFY(session.undo());
        QVERIFY(session.undo());
        QVERIFY(session.undo());
        QVERIFY(session.undo());
        const auto neutral = signal.finished(finishing.value(false));
        QVERIFY(!neutral->finishing.changesPictures());
        const MediaRenderedFrame plain = neutral->render(3, neutral->frameSize, pool, {});
        QCOMPARE(differingPixels(plain.image, source), 0);
    }

    void burnedAndEmbeddedCaptionsReachTheExport()
    {
        Signal signal;
        QVERIFY(signal.open(12));
        CaptionTrackModel track;
        const QVector<CaptionCueRecord> cues{
            cue(QStringLiteral("a"), 0, 200, QStringLiteral("こんにちは、港。"), 0),
            cue(QStringLiteral("b"), 120, 400, QStringLiteral("مرحبا · Ελληνικά\nsecond line"), 1)};
        QJsonObject json = CaptionTrackCodec::toJson(cues, QStringLiteral("captions-ja"), QStringLiteral("ja-JP"));
        QVERIFY(track.loadJson(QString::fromUtf8(QJsonDocument(json).toJson())));
        QCOMPARE(track.language(), QStringLiteral("ja-JP"));

        VideoFinishing finishing;
        finishing.setCaptionTrack(&track);
        finishing.setBurnInCaptions(true);
        finishing.setEmbedCaptions(true);
        QVERIFY(!finishing.digest().isEmpty());
        const auto program = signal.finished(finishing.value(false));
        QCOMPARE(program->frameStartMilliseconds(3).value_or(-1), qint64(120));
        QCOMPARE(program->frameStartMilliseconds(5).value_or(-1), qint64(200));
        QCOMPARE(program->frameStartMilliseconds(10).value_or(-1), qint64(400));

        // [in, out) at frame starts: a on frames 0-4, b on frames 3-9.
        QImage probe(64, 36, QImage::Format_RGB32);
        probe.fill(Qt::black);
        QCOMPARE(CaptionBurnIn::burn(&probe, cues, 119), QStringList{QStringLiteral("a")});
        QCOMPARE(CaptionBurnIn::burn(&probe, cues, 120), (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
        QCOMPARE(CaptionBurnIn::burn(&probe, cues, 200), QStringList{QStringLiteral("b")});
        QCOMPARE(CaptionBurnIn::burn(&probe, cues, 400), QStringList());

        // Multilingual output is drawn, not boxed: burn() paints the layout
        // glyphRuns() returns, every character has a real glyph, and Arabic
        // is joined and runs right to left (audit: "pixels changed" alone
        // would pass tofu or reversed text).
        for (const CaptionCueRecord &record : cues)
            QCOMPARE(CaptionBurnIn::missingCharacters(record.text), QString());
        const QString unassigned = QString::fromUcs4(U"\U0010FFFD"); // plane-16 private use
        QCOMPARE(CaptionBurnIn::missingCharacters(QStringLiteral("a") + unassigned), unassigned);
        const QString arabic = QStringLiteral("مرحبا");
        const QList<QGlyphRun> joined = CaptionBurnIn::glyphRuns(arabic, 24, false, 1000);
        QCOMPARE(joined.size(), 1);
        QVERIFY(joined.first().isRightToLeft());
        const auto glyphAt = [](const QGlyphRun &run, qsizetype stringIndex) {
            for (qsizetype i = 0; i < run.stringIndexes().size(); ++i)
                if (run.stringIndexes().at(i) == stringIndex) return i;
            return qsizetype(-1);
        };
        const qsizetype meem = glyphAt(joined.first(), 0);
        const qsizetype alef = glyphAt(joined.first(), arabic.size() - 1);
        QVERIFY(meem >= 0 && alef >= 0);
        QVERIFY(joined.first().positions().at(meem).x() > joined.first().positions().at(alef).x()); // RTL order
        const QList<QGlyphRun> isolated = CaptionBurnIn::glyphRuns(arabic.left(1), 24, false, 1000);
        QVERIFY(joined.first().glyphIndexes().at(meem) != isolated.first().glyphIndexes().first()); // initial form
        CaptionTrackModel boxed;
        QVERIFY(boxed.addCue(QStringLiteral("0"), QStringLiteral("2000"), QStringLiteral("ok ") + unassigned));
        bool glyphWarning = false;
        for (const QVariant &warning : boxed.warnings())
            glyphWarning = glyphWarning || warning.toMap().value(QStringLiteral("kind")).toString() == QLatin1String("glyphs");
        QVERIFY(glyphWarning);

        MediaDecoderPool pool;
        for (const qint64 frame : {qint64(0), qint64(4), qint64(9)}) {
            const MediaRenderedFrame burned = program->render(frame, program->frameSize, pool, {});
            QVERIFY2(!burned.error, qPrintable(burned.error.message));
            QVERIFY2(differingPixels(burned.image, decodedFrame(signal.path, frame)) > 0, qPrintable(QString::number(frame)));
        }
        const MediaRenderedFrame clean = program->render(10, program->frameSize, pool, {});
        QCOMPARE(differingPixels(clean.image, decodedFrame(signal.path, 10)), 0);

        QTemporaryDir out;
        const QString file = out.filePath(QStringLiteral("captioned.mkv"));
        const MediaExportResult exported = MediaExport::exportProgram(*program, file, 0, 12);
        QVERIFY2(!exported.error, qPrintable(exported.error.message));
        QCOMPARE(exported.subtitles, qint64(2));
        const MediaRenderedFrame four = program->render(4, program->frameSize, pool, {});
        QCOMPARE(differingPixels(decodedFrame(file, 4), four.image), 0);
        QCOMPARE(differingPixels(decodedFrame(file, 11), decodedFrame(signal.path, 11)), 0);

        const MediaCaptionTrack embedded = MediaCaptions::readEmbedded(file);
        QVERIFY2(!embedded.error, qPrintable(embedded.error.message));
        QCOMPARE(embedded.codec, QStringLiteral("subrip"));
        QCOMPARE(embedded.language, QStringLiteral("ja-JP"));
        QCOMPARE(embedded.cues.size(), 2);
        for (int i = 0; i < 2; ++i) {
            QCOMPARE(embedded.cues.at(i).inMs, cues.at(i).inMs);
            QCOMPARE(embedded.cues.at(i).outMs, cues.at(i).outMs);
            QCOMPARE(embedded.cues.at(i).text, cues.at(i).text);
        }
        CaptionTrackModel reopened;
        QVERIFY(reopened.importEmbedded(file));
        QCOMPARE(reopened.language(), QStringLiteral("ja-JP"));
        QCOMPARE(reopened.cues().size(), 2);
        QCOMPARE(reopened.cues().at(1).text, cues.at(1).text);
        QCOMPARE(reopened.cues().at(1).lane, 1); // overlapping cues take separate lanes again

        // A partial export times its captions from its own first frame.
        const QString tail = out.filePath(QStringLiteral("tail.mkv"));
        QVERIFY(!MediaExport::exportProgram(*program, tail, 2, 8).error);
        const MediaCaptionTrack shifted = MediaCaptions::readEmbedded(tail);
        QCOMPARE(shifted.cues.size(), 2);
        QCOMPARE(shifted.cues.at(0).inMs, qint64(0));
        QCOMPARE(shifted.cues.at(0).outMs, qint64(120));
        QCOMPARE(shifted.cues.at(1).inMs, qint64(40));
        QCOMPARE(shifted.cues.at(1).outMs, qint64(320));

        // The finishing survives the render queue's program JSON.
        QString error;
        const auto finishingJson = MediaFinishing::fromJson(program->finishing.toJson(), &error);
        QVERIFY2(finishingJson, qPrintable(error));
        QCOMPARE(finishingJson->digest(), program->finishing.digest());
        QCOMPARE(finishingJson->embeddedLanguage, QStringLiteral("ja-JP"));
    }

    void encoderDropsSubtitlesAfterTheLastFrame()
    {
        QTemporaryDir dir;
        MediaEncodeSettings settings;
        settings.frameSize = QSize(64, 36);
        settings.channels = 0;
        settings.subtitles = {{0, 80, QStringLiteral("first")}, {100, 5000, QStringLiteral("long")},
                              {200, 240, QStringLiteral("after the end")}};
        settings.subtitleLanguage = QStringLiteral("en-GB");
        MediaEncoder encoder;
        const QString path = dir.filePath(QStringLiteral("subs.mkv"));
        QVERIFY(!encoder.open(path, settings));
        QImage frame(64, 36, QImage::Format_RGB32);
        frame.fill(Qt::black);
        for (int i = 0; i < 5; ++i) QVERIFY(!encoder.writeVideo(frame)); // 200 ms
        QVERIFY(!encoder.finish());
        QCOMPARE(encoder.subtitlesWritten(), qint64(2));
        const MediaCaptionTrack read = MediaCaptions::readEmbedded(path);
        QCOMPARE(read.language, QStringLiteral("en-GB"));
        QCOMPARE(read.cues.size(), 2);
        QCOMPARE(read.cues.at(1).outMs, qint64(5000)); // an end is kept exactly

        MediaEncodeSettings unsorted = settings;
        unsorted.subtitles = {{100, 200, QStringLiteral("b")}, {0, 50, QStringLiteral("a")}};
        MediaEncoder rejected;
        QCOMPARE(rejected.open(dir.filePath(QStringLiteral("bad.mkv")), unsorted).code, ErrorCode::InvalidArgument);
    }

    void scopesMeasureDecodedBarsUnderStatedConventions()
    {
        Signal signal;
        QVERIFY(signal.open(3));
        // The top fifth of a non-pop frame is the seven 75% bars only.
        const QImage bars = decodedFrame(signal.path, 1).copy(0, 0, kWidth, kHeight / 5);
        QVERIFY(!bars.isNull());
        VideoScopesModel scopes;
        scopes.setSignalRange(QStringLiteral("limited"));
        QVariantMap snapshot = scopes.analyzeFrame(bars, QStringLiteral("bars#1"));
        QVERIFY(snapshot.value(QStringLiteral("valid")).toBool());
        QCOMPARE(snapshot.value(QStringLiteral("matrix")).toString(), QStringLiteral("bt709"));
        QCOMPARE(snapshot.value(QStringLiteral("signalRange")).toString(), QStringLiteral("limited"));
        const int rec709[7] = {180, 168, 145, 133, 63, 51, 28};
        const int cb709[7] = {128, 44, 147, 63, 193, 109, 212};
        const int cr709[7] = {128, 136, 44, 52, 204, 212, 120};
        QVariantList low = snapshot.value(QStringLiteral("waveformMin")).toList();
        QVariantList high = snapshot.value(QStringLiteral("waveformMax")).toList();
        for (int bar = 0; bar < 7; ++bar) {
            QCOMPARE(low.at(barColumn(bar)).toInt(), rec709[bar]);
            QCOMPARE(high.at(barColumn(bar)).toInt(), rec709[bar]);
            const QImage single = bars.copy(bar * 64 + 16, 0, 32, bars.height());
            const QVariantMap levels = VideoScopeAnalysis::analyze(single, QStringLiteral("bar"), scopes.convention())
                                           .value(QStringLiteral("levels")).toMap();
            QCOMPARE(levels.value(QStringLiteral("cbMin")).toInt(), cb709[bar]);
            QCOMPARE(levels.value(QStringLiteral("cbMax")).toInt(), cb709[bar]);
            QCOMPARE(levels.value(QStringLiteral("crMin")).toInt(), cr709[bar]);
        }
        // The yellow bar's parade: R' and G' at 180, B' at black (16).
        const QVariantList paradeMax = snapshot.value(QStringLiteral("paradeMax")).toList();
        const int columns = VideoScopeAnalysis::kWaveColumns;
        QCOMPARE(paradeMax.at(0 * columns + barColumn(1)).toInt(), 180);
        QCOMPARE(paradeMax.at(1 * columns + barColumn(1)).toInt(), 180);
        QCOMPARE(paradeMax.at(2 * columns + barColumn(1)).toInt(), 16);

        scopes.setMatrix(QStringLiteral("bt601")); // re-measures the same frame
        snapshot = scopes.snapshot();
        QCOMPARE(snapshot.value(QStringLiteral("frameId")).toString(), QStringLiteral("bars#1"));
        const int rec601[7] = {180, 161, 131, 112, 84, 65, 35};
        low = snapshot.value(QStringLiteral("waveformMin")).toList();
        for (int bar = 0; bar < 7; ++bar) QCOMPARE(low.at(barColumn(bar)).toInt(), rec601[bar]);

        scopes.setMatrix(QStringLiteral("bt709"));
        scopes.setSignalRange(QStringLiteral("full"));
        const int full709[7] = {191, 177, 150, 137, 54, 41, 14};
        high = scopes.snapshot().value(QStringLiteral("waveformMax")).toList();
        for (int bar = 0; bar < 7; ++bar) QCOMPARE(high.at(barColumn(bar)).toInt(), full709[bar]);
        QCOMPARE(scopes.analysisCount(), 4); // one analysis per frame or convention change
    }

    void playerScopesFollowFramesAndShowTheFinishing()
    {
        Signal signal;
        QVERIFY(signal.open(10));
        MediaPlayer player;
        player.setResourceId(signal.path);
        QTRY_COMPARE(player.state(), QStringLiteral("ready"));
        QTRY_COMPARE(player.presentedFrame(), QStringLiteral("0"));
        QTRY_VERIFY(player.transport()->canSeek());
        VideoScopesModel scopes;
        scopes.setPlayer(&player);
        QTRY_COMPARE(scopes.snapshot().value(QStringLiteral("frameId")).toString(), player.presentedFrameKey());
        QVERIFY(!player.seekFrame(QStringLiteral("5")).isEmpty());
        QTRY_COMPARE(player.presentedFrame(), QStringLiteral("5"));
        QTRY_COMPARE(scopes.snapshot().value(QStringLiteral("frameId")).toString(), player.presentedFrameKey());
        QVERIFY(player.presentedFrameKey().endsWith(QStringLiteral("#5")));
        // Repaints and repeated notifications of the same frame never re-analyze.
        const int analyses = scopes.analysisCount();
        emit player.frameChanged();
        emit player.frameChanged();
        QCOMPARE(scopes.analysisCount(), analyses);
        // Inactive scopes skip frames and catch up on the frame now shown.
        scopes.setActive(false);
        QVERIFY(!player.seekFrame(QStringLiteral("7")).isEmpty());
        QTRY_COMPARE(player.presentedFrame(), QStringLiteral("7"));
        QVERIFY(scopes.snapshot().value(QStringLiteral("frameId")).toString().endsWith(QStringLiteral("#5")));
        scopes.setActive(true);
        QCOMPARE(scopes.snapshot().value(QStringLiteral("frameId")).toString(), player.presentedFrameKey());

        // The monitor shows the grade through the same pipeline.
        const QImage source = decodedFrame(signal.path, 7);
        ColorGradeEditSession session;
        session.setFrame(QStringLiteral("bars"), QStringLiteral("bars#7"), source, MediaFinishing::gradeContext());
        QVERIFY(session.setExposure(-1.0));
        VideoFinishing finishing;
        finishing.setGradeSession(&session);
        const int before = scopes.analysisCount();
        player.setFinishing(&finishing);
        const QRgb expected = session.processedImage().convertToFormat(QImage::Format_RGB32).pixel(10, 2);
        QVERIFY(qRed(expected) < 191);
        QTRY_COMPARE(player.presentedImage().pixel(10, 2) & 0xffffff, expected & 0xffffff);
        QCOMPARE(player.presentedFrame(), QStringLiteral("7"));
        // The scopes measure the graded frame under the same identity.
        QTRY_VERIFY(scopes.analysisCount() > before);
        QCOMPARE(scopes.snapshot().value(QStringLiteral("frameId")).toString(), player.presentedFrameKey());
    }
};

QTEST_MAIN(VideoFinishingTest)
#include "tst_video_finishing.moc"
