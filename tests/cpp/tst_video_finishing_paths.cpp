// SPDX-License-Identifier: LGPL-3.0-or-later
#include "caption_track_model.h"
#include "color_grade_edit_session.h"
#include "media_captions.h"
#include "media_finishing.h"
#include "media_fixture.h"
#include "media_player.h"
#include "media_program.h"
#include "resource_resolver.h"
#include "time_map.h"
#include "video_finishing.h"
#include "video_scopes_model.h"

#include <QColorSpace>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

using namespace QindaTK;
using namespace MediaFixture;

// WP-072 audit gaps (docs/universal-toolkit/implementation/evidence/WP-072.md):
// a program grade survives the frames a host feeds it and persists; its
// preview renders in the program's pipeline; scopes read YUV sources under
// the decode they went through; and the real export entry points
// (MediaPlayer::exportJob, program JSON, MediaExport::renderProvider) write
// the committed finishing, never a drag in progress.
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

MediaDecodedFrame decoded(const QString &path, qint64 index)
{
    MediaDecoder decoder;
    if (decoder.open(path)) return {};
    return decoder.decodeFrame(index, QSize());
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

QImage rgb(const QImage &image) { return image.convertToFormat(QImage::Format_RGB32); }

int barColumn(int bar)
{
    return int(std::lround((bar + 0.5) * VideoScopeAnalysis::kWaveColumns / 7.0));
}

// The Y' plane codes at the centre of each bar of frame 1, read by ffmpeg
// from the file itself (yuv444p decodes to rawvideo without conversion).
QVector<int> fileLumaCodes(const QString &path, const QString &rawPath)
{
    if (!runFfmpeg({QStringLiteral("-i"), path, QStringLiteral("-vf"), QStringLiteral("select=eq(n\\,1)"),
                    QStringLiteral("-frames:v"), QStringLiteral("1"), QStringLiteral("-f"), QStringLiteral("rawvideo"),
                    QStringLiteral("-pix_fmt"), QStringLiteral("yuv444p"), rawPath}))
        return {};
    QFile file(rawPath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = file.readAll();
    if (bytes.size() != kWidth * kHeight * 3) return {};
    QVector<int> codes;
    for (int bar = 0; bar < 7; ++bar) codes.append(quint8(bytes.at(2 * kWidth + bar * 64 + 32)));
    return codes;
}
} // namespace

class VideoFinishingPathsTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        if (!MediaDecoder::available()) QSKIP("Built without the FFmpeg media backend");
    }

    void programGradeSurvivesNewFramesAndPersists()
    {
        Signal signal;
        QVERIFY(signal.open(10));
        const ColorContextSnapshot context = MediaFinishing::gradeContext();
        ColorGradeEditSession session;
        QCOMPARE(session.scope(), ColorGradeEditSession::FrameGrade);
        VideoFinishing finishing;
        finishing.setGradeSession(&session);
        QCOMPARE(session.scope(), ColorGradeEditSession::ProgramGrade);
        session.setFrame(QStringLiteral("bars"), QStringLiteral("bars#3"), decoded(signal.path, 3).image, context);
        QVERIFY(session.setExposure(-0.5));
        QVERIFY(session.setWheelValue(QStringLiteral("gain"), 0.2, 0.1));
        const QVariantMap graded = session.committedParameters();

        // The host presents the next frame: the grade and its history stay.
        session.setFrame(QStringLiteral("bars"), QStringLiteral("bars#7"), decoded(signal.path, 7).image, context);
        QCOMPARE(session.committedParameters(), graded);
        QVERIFY(session.canUndo());
        const auto program = signal.finished(finishing.value(false));
        QVERIFY(program->finishing.changesPictures());
        MediaDecoderPool pool;
        const MediaRenderedFrame seven = program->render(7, program->frameSize, pool, {});
        QCOMPARE(differingPixels(seven.image, rgb(session.processedImage())), 0); // the preview is of frame 7
        session.setFrameId(QStringLiteral("bars#8"));
        session.setClipId(QStringLiteral("other-clip"));
        QCOMPARE(session.parameters(), graded);
        QVERIFY(session.undo()); // undo still reaches the gain step
        QCOMPARE(session.exposure(), -0.5);
        QVERIFY(session.redo());
        QCOMPARE(session.committedParameters(), graded);

        // Save/load: the committed grade, without history, in a new session.
        const QString saved = session.saveJson();
        QVERIFY(saved.contains(QStringLiteral("qindatk.color-grade/1")));
        ColorGradeEditSession restored;
        QVERIFY(restored.loadJson(saved));
        QCOMPARE(restored.scope(), ColorGradeEditSession::ProgramGrade);
        QCOMPARE(restored.committedParameters(), graded);
        QVERIFY(!restored.canUndo());
        QVERIFY(!restored.loadJson(QStringLiteral(
            R"({"format":"qindatk.color-grade/1","grade":{"exposureEV":99}})")));
        QVERIFY(!restored.errorMessage().isEmpty());
        QCOMPARE(restored.committedParameters(), graded); // a refused load changes nothing
        VideoFinishing reopened;
        reopened.setGradeSession(&restored);
        QCOMPARE(reopened.value(false).digest(), finishing.value(false).digest());

        // A still (frame-scoped) grade keeps its documented isolation.
        ColorGradeEditSession still;
        still.setFrame(QStringLiteral("bars"), QStringLiteral("bars#3"), decoded(signal.path, 3).image, context);
        QVERIFY(still.setExposure(1.0));
        still.setFrame(QStringLiteral("bars"), QStringLiteral("bars#4"), decoded(signal.path, 4).image, context);
        QCOMPARE(still.exposure(), 0.0);
        QVERIFY(!still.canUndo());
    }

    void programGradePreviewUsesTheProgramPipeline()
    {
        Signal signal;
        QVERIFY(signal.open(6));
        const QImage source = decoded(signal.path, 3).image;
        ColorContextSnapshot p3 = MediaFinishing::gradeContext();
        p3.output = ColorProfile::fromIcc(QColorSpace(QColorSpace::DisplayP3).iccProfile());
        p3.generation = 7;
        QVERIFY(p3.output.isValid());

        // A still grade honours the host's output profile...
        ColorGradeEditSession still;
        still.setFrame(QStringLiteral("bars"), QStringLiteral("bars#3"), source, p3);
        QVERIFY(still.setWheelValue(QStringLiteral("gain"), 0.3, -0.2));
        QVERIFY(still.processedImage().colorSpace() != QColorSpace(QColorSpace::SRgb));

        // ...while the program grade previews exactly what the program renders.
        ColorGradeEditSession session;
        VideoFinishing finishing;
        finishing.setGradeSession(&session);
        session.setFrame(QStringLiteral("bars"), QStringLiteral("bars#3"), source, p3);
        QVERIFY(session.setWheelValue(QStringLiteral("gain"), 0.3, -0.2));
        QCOMPARE(session.renderContext().output.id(), ColorProfile::sRgb().id());
        QCOMPARE(session.renderContext().generation, quint64(7));
        const auto program = signal.finished(finishing.value(false));
        MediaDecoderPool pool;
        const MediaRenderedFrame rendered = program->render(3, program->frameSize, pool, {});
        QVERIFY2(!rendered.error, qPrintable(rendered.error.message));
        QCOMPARE(differingPixels(rendered.image, rgb(session.processedImage())), 0);
        QVERIFY(differingPixels(rendered.image, rgb(still.processedImage())) > 0);
    }

    void scopesReadYuvSourcesUnderTheirDecode()
    {
        if (ffmpeg().isEmpty()) QSKIP("The ffmpeg CLI writes the Y'CbCr fixtures");
        Signal signal;
        QVERIFY(signal.open(3));
        struct Case { QString name; QStringList filter; QString matrix, range; };
        // Rec.601 limited with no colour tags (SD height: the decoder picks
        // Rec.601), and Rec.709 full range tagged in the stream.
        const QList<Case> cases{
            {QStringLiteral("sd601"), {QStringLiteral("-vf"), QStringLiteral("scale=out_color_matrix=bt601:out_range=tv,format=yuv444p")},
             QStringLiteral("bt601"), QStringLiteral("limited")},
            {QStringLiteral("hd709full"), {QStringLiteral("-vf"), QStringLiteral("scale=out_color_matrix=bt709:out_range=pc,format=yuv444p"),
                                           QStringLiteral("-colorspace"), QStringLiteral("bt709"),
                                           QStringLiteral("-color_range"), QStringLiteral("pc")},
             QStringLiteral("bt709"), QStringLiteral("full")}};
        for (const Case &test : cases) {
            const QString path = signal.dir.filePath(test.name + QStringLiteral(".mkv"));
            QVERIFY(runFfmpeg(QStringList{QStringLiteral("-i"), signal.path, QStringLiteral("-an")} + test.filter
                              + QStringList{QStringLiteral("-c:v"), QStringLiteral("ffv1"), path}));
            const QVector<int> fileCodes = fileLumaCodes(path, signal.dir.filePath(test.name + QStringLiteral(".yuv")));
            QCOMPARE(fileCodes.size(), 7);
            const MediaDecodedFrame frame = decoded(path, 1);
            QCOMPARE(frame.matrix, test.matrix);
            QCOMPARE(frame.range, test.range);

            VideoScopesModel scopes; // matrix and range default to "source"
            QVariantMap snapshot = scopes.analyzeDecodedFrame(frame.image.copy(0, 0, kWidth, kHeight / 5),
                                                              test.name, frame.matrix, frame.range);
            QCOMPARE(snapshot.value(QStringLiteral("matrix")).toString(), test.matrix);
            QCOMPARE(snapshot.value(QStringLiteral("signalRange")).toString(), test.range);
            QCOMPARE(snapshot.value(QStringLiteral("matrixOrigin")).toString(), QStringLiteral("source"));
            const QVariantList low = snapshot.value(QStringLiteral("waveformMin")).toList();
            const QVariantList high = snapshot.value(QStringLiteral("waveformMax")).toList();
            for (int bar = 0; bar < 7; ++bar) {
                // The scope reads the file's own Y' codes (8-bit decode and
                // re-encode round within one code).
                QVERIFY2(std::abs(low.at(barColumn(bar)).toInt() - fileCodes.at(bar)) <= 1,
                         qPrintable(QStringLiteral("%1 bar %2: %3 vs %4").arg(test.name).arg(bar)
                                        .arg(low.at(barColumn(bar)).toInt()).arg(fileCodes.at(bar))));
                QVERIFY(std::abs(high.at(barColumn(bar)).toInt() - fileCodes.at(bar)) <= 1);
            }
            // A stated, different convention would misread the same file.
            scopes.setMatrix(test.matrix == QLatin1String("bt601") ? QStringLiteral("bt709") : QStringLiteral("bt601"));
            snapshot = scopes.snapshot();
            QCOMPARE(snapshot.value(QStringLiteral("matrixOrigin")).toString(), QStringLiteral("stated"));
            int worst = 0;
            for (int bar = 0; bar < 7; ++bar)
                worst = std::max(worst, std::abs(snapshot.value(QStringLiteral("waveformMin")).toList()
                                                     .at(barColumn(bar)).toInt() - fileCodes.at(bar)));
            QVERIFY2(worst > 5, qPrintable(QString::number(worst)));

            // Bound to a player, the coding comes from the presented source.
            MediaPlayer player;
            player.setResourceId(path);
            QTRY_COMPARE(player.presentedFrame(), QStringLiteral("0"));
            QCOMPARE(player.presentedSources().first().toMap().value(QStringLiteral("matrix")).toString(), test.matrix);
            VideoScopesModel bound;
            bound.setPlayer(&player);
            QTRY_COMPARE(bound.snapshot().value(QStringLiteral("frameId")).toString(), player.presentedFrameKey());
            QCOMPARE(bound.snapshot().value(QStringLiteral("matrix")).toString(), test.matrix);
            QCOMPARE(bound.snapshot().value(QStringLiteral("signalRange")).toString(), test.range);
            QCOMPARE(bound.snapshot().value(QStringLiteral("sourceCoding")).toString(), test.matrix + QLatin1Char('/') + test.range);
        }
        // RGB-coded video has no matrix: the scopes say so and use the default.
        const MediaDecodedFrame rgbFrame = decoded(signal.path, 1);
        QCOMPARE(rgbFrame.matrix, QStringLiteral("rgb"));
        VideoScopesModel scopes;
        const QVariantMap snapshot = scopes.analyzeDecodedFrame(rgbFrame.image, QStringLiteral("rgb"),
                                                                rgbFrame.matrix, rgbFrame.range);
        QCOMPARE(snapshot.value(QStringLiteral("matrixOrigin")).toString(), QStringLiteral("default"));
        QCOMPARE(snapshot.value(QStringLiteral("matrix")).toString(), QStringLiteral("bt709"));
    }

    void exportJobAndRenderQueueWriteTheCommittedFinishing()
    {
        Signal signal;
        QVERIFY(signal.open(10));
        ResourceResolver resolver;
        QVERIFY(resolver.registerResource(QStringLiteral("A"), fileUri(signal.path)));
        TimeMap map;
        QVERIFY(map.setFrameRate(25, 1));
        TimelineModel timeline;
        timeline.setTimeMap(&map);
        const TimelineTrack v1 = track(1, QStringLiteral("video"));
        OperationError error;
        QVERIFY(timeline.setContent(kTimebase, {v1}, {clip(1, v1, 0, 10 * kFrame25, 0, QStringLiteral("A"))}, {}, &error));

        ColorGradeEditSession session;
        CaptionTrackModel track;
        QVERIFY(track.importSrt(QStringLiteral("1\n00:00:00,080 --> 00:00:00,200\nΚαλησπέρα\n"), QStringLiteral("el")));
        VideoFinishing finishing;
        finishing.setGradeSession(&session);
        finishing.setCaptionTrack(&track);
        finishing.setEmbedCaptions(true);
        MediaPlayer player;
        player.setResolver(&resolver);
        player.setFrameWidth(kWidth);
        player.setFrameHeight(kHeight);
        player.setTimeline(&timeline);
        player.setFinishing(&finishing);
        QTRY_COMPARE(player.presentedFrame(), QStringLiteral("0"));

        const QImage source = decoded(signal.path, 3).image;
        session.setFrame(QStringLiteral("A"), QStringLiteral("A#3"), source, MediaFinishing::gradeContext());
        QVERIFY(session.setExposure(-1.0)); // committed
        QVERIFY(session.previewWheel(QStringLiteral("drag"), QStringLiteral("gain"), 0.6, 0.4)); // a drag in progress
        QVERIFY(session.editing());
        const QImage dragPreview = rgb(session.processedImage());
        // The monitor shows the drag...
        QTRY_COMPARE(player.presentedImage().pixel(10, 2) & 0xffffff, dragPreview.pixel(10, 2) & 0xffffff);

        // ...the queued export captures the committed grade only.
        QTemporaryDir out;
        const QVariantMap job = player.exportJob(out.filePath(QStringLiteral("graded")), QStringLiteral("Graded"));
        QVERIFY2(!job.isEmpty(), qPrintable(player.lastError()));
        QString why;
        const auto queued = MediaProgram::fromJson(
            QJsonObject::fromVariantMap(job).value(QStringLiteral("settings")).toObject()
                .value(QStringLiteral("program")).toObject(), &why);
        QVERIFY2(queued, qPrintable(why));
        QCOMPARE(queued->finishing.digest(), finishing.value(false).digest());
        QVERIFY(queued->finishing.digest() != finishing.value(true).digest());
        QVERIFY(session.finishWheel(QStringLiteral("drag"), QStringLiteral("gain"), 0.6, 0.4)); // after queueing

        const QString stage = out.filePath(QStringLiteral("stage"));
        QVERIFY(QDir().mkpath(stage));
        const RenderProduct product = MediaExport::renderProvider(QJsonObject::fromVariantMap(job), stage, {}, {});
        QVERIFY2(product.error.isEmpty(), qPrintable(product.error));
        const QString file = QDir(stage).filePath(QStringLiteral("program.mkv"));
        ColorGradeEditSession committedOnly;
        committedOnly.setFrame(QStringLiteral("A"), QStringLiteral("A#3"), source, MediaFinishing::gradeContext());
        QVERIFY(committedOnly.setExposure(-1.0));
        const QImage written = decoded(file, 3).image;
        QCOMPARE(differingPixels(written, rgb(committedOnly.processedImage())), 0);
        QVERIFY(differingPixels(written, dragPreview) > 0);
        const MediaCaptionTrack embedded = MediaCaptions::readEmbedded(file);
        QVERIFY2(!embedded.error, qPrintable(embedded.error.message));
        QCOMPARE(embedded.language, QStringLiteral("el"));
        QCOMPARE(embedded.cues.size(), 1);
        QCOMPARE(embedded.cues.first().inMs, qint64(80));
        QCOMPARE(embedded.cues.first().text, QStringLiteral("Καλησπέρα"));
    }
};

QTEST_MAIN(VideoFinishingPathsTest)
#include "tst_video_finishing_paths.moc"
