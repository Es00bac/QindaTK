// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_fixture.h"
#include "media_program.h"
#include "media_transition.h"
#include "multicam_model.h"
#include "sequence_fixture.h"

#include <QColor>
#include <cmath>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;
using SequenceFixture::F;
using SequenceFixture::Session;
using SequenceFixture::uid;

// WP-071 output: sequence edits and transitions render and export the
// expected frames and sound (docs/sequence-edits.md "Evaluation"). Sources
// are the toolkit's own test signal: a frame code in every picture, and pops
// (white picture plus a 1 kHz tone) every `popInterval` frames.
namespace {
const QString kVideo = QStringLiteral("video");
const QString kAudio = QStringLiteral("audio");

std::shared_ptr<MediaProgram> programOf(const TimelineModel &model, const QHash<QString, QString> &paths)
{
    QString error;
    auto program = MediaProgram::fromTimeline(model, [paths](const QString &id) {
        return std::pair<QString, QString>{paths.value(id), QStringLiteral("1")};
    }, QSize(128, 72), 2, &error);
    if (!program) qWarning("%s", qPrintable(error));
    return program;
}

qint64 codeAt(const MediaProgram &program, qint64 frame, MediaDecoderPool &pool)
{
    const MediaRenderedFrame rendered = program.render(frame, program.frameSize, pool, {});
    return rendered.error ? -2 : MediaExport::readTestSignalCode(rendered.image);
}

// Channel value of a dissolve between grey levels at incoming weight w.
int mixed(int outgoing, int incoming, qint64 w)
{
    return int((outgoing * (65536 - w) + incoming * w + 32768) >> 16);
}

int differingPixels(const QImage &left, const QImage &right)
{
    if (left.size() != right.size()) return -1;
    int count = 0;
    for (int y = 0; y < left.height(); ++y)
        for (int x = 0; x < left.width(); ++x)
            if (qRgb(qRed(left.pixel(x, y)), qGreen(left.pixel(x, y)), qBlue(left.pixel(x, y)))
                != qRgb(qRed(right.pixel(x, y)), qGreen(right.pixel(x, y)), qBlue(right.pixel(x, y))))
                ++count;
    return count;
}

// Peak of channel 0 over program samples [first, first + count) of a mix that starts at `origin`.
float peakAt(const QVector<float> &samples, qint64 origin, qint64 first, qint64 count)
{
    float result = 0.0f;
    for (qint64 s = first; s < first + count; ++s) {
        const qsizetype index = qsizetype((s - origin) * 2);
        if (index >= 0 && index < samples.size()) result = std::max(result, std::fabs(samples.at(index)));
    }
    return result;
}
} // namespace

class SequenceRenderTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        if (!MediaDecoder::available()) QSKIP("Built without the FFmpeg media backend");
    }
    void transitionsRenderAndExportTheSameFrames();
    void linkedTrimsKeepPictureAndSoundTogether();
    void multicamSwitchesRenderSyncedAngles();
};

void SequenceRenderTest::transitionsRenderAndExportTheSameFrames()
{
    QTemporaryDir dir;
    const QString a = MediaFixture::signal(dir.filePath(QStringLiteral("a.mkv")), 75, 128, 72, 2, 1); // all pops
    const QString b = MediaFixture::signal(dir.filePath(QStringLiteral("b.mkv")), 75, 128, 72, 2, 0); // bars, silence
    QVERIFY(!a.isEmpty() && !b.isEmpty());
    const QHash<QString, QString> paths{{QStringLiteral("A"), a}, {QStringLiteral("B"), b}};
    Session s;
    using namespace SequenceFixture;
    QVERIFY(s.model.setContent({S, 1}, {track(V1, kVideo), track(A1, kAudio)},
                               {clip(ClipA, V1, kVideo, 0, 25 * F, 0, 75 * F, QStringLiteral("A"), LinkA),
                                clip(ClipB, V1, kVideo, 25 * F, 25 * F, 25 * F, 75 * F, QStringLiteral("B"), LinkB),
                                clip(SoundA, A1, kAudio, 0, 25 * F, 0, 75 * F, QStringLiteral("A"), LinkA),
                                clip(SoundB, A1, kAudio, 25 * F, 25 * F, 25 * F, 75 * F, QStringLiteral("B"), LinkB)},
                               {}));
    // A linked 4-frame centred dissolve: window [23F, 27F) in picture and sound.
    const OperationError error = s.transition(ClipB, QStringLiteral("dissolve"), 4 * F, QStringLiteral("center"), {}, true);
    QVERIFY2(!error, qPrintable(error.message));
    const auto program = programOf(s.model, paths);
    QVERIFY(program);
    QCOMPARE(program->frameCount, qint64(50));
    QCOMPARE(program->video.size(), 2);
    QCOMPARE(program->video.at(0).duration, 27 * F);
    QCOMPARE(program->video.at(1).start, 23 * F);
    QCOMPARE(program->video.at(1).sourceOffset, 23 * F);
    QCOMPARE(program->video.at(1).fadeIn.start, 23 * F);
    QCOMPARE(program->video.at(1).fadeIn.duration, 4 * F);
    QCOMPARE(program->audio.at(0).fadeOut.duration, 4 * F);

    MediaDecoderPool pool;
    const MediaRenderedFrame before = program->render(22, program->frameSize, pool, {});
    QVERIFY(!before.error);
    QCOMPARE(before.image.pixelColor(5, 5), QColor(255, 255, 255));
    QCOMPARE(before.sources.size(), 1);
    for (qint64 frame = 23; frame < 27; ++frame) {
        const MediaRenderedFrame rendered = program->render(frame, program->frameSize, pool, {});
        QVERIFY2(!rendered.error, qPrintable(rendered.error.message));
        QCOMPARE(rendered.sources.size(), 2);
        QCOMPARE(rendered.sources.at(0).frame, frame); // A's own frame, past its cut
        QCOMPARE(rendered.sources.at(1).frame, frame); // B's handle frame, before its cut
        // Midpoint weight: (2i + 1) / 8 of 65536, exact for this window.
        const qint64 w = 8192 * (2 * (frame - 23) + 1);
        QCOMPARE(MediaTransition::frameWeight(frame * F, (frame + 1) * F, 23 * F, 4 * F), int(w));
        const QColor pixel = rendered.image.pixelColor(5, 5);
        QCOMPARE(pixel.red(), mixed(255, 191, w));
        QCOMPARE(pixel.green(), mixed(255, 191, w));
        QCOMPARE(pixel.blue(), mixed(255, 191, w));
    }
    QCOMPARE(codeAt(*program, 27, pool), qint64(27));
    QCOMPARE(program->render(27, program->frameSize, pool, {}).image.pixelColor(5, 5), QColor(191, 191, 191));

    // The queued-export form of the program carries the same windows.
    const auto restored = MediaProgram::fromJson(program->toJson());
    QVERIFY(restored);
    QCOMPARE(differingPixels(restored->render(24, restored->frameSize, pool, {}).image,
                             program->render(24, program->frameSize, pool, {}).image), 0);

    // Sound: the tone of A fades out linearly over the window's samples.
    const qint64 first = 23 * 1920, last = 27 * 1920;
    const MediaAudioMix mix = program->mixAudio(first - 1920, 6 * 1920, pool, {});
    QVERIFY2(!mix.error, qPrintable(mix.error.message));
    QVERIFY(peakAt(mix.samples, mix.firstSample, first - 1920, 1920) > 0.49f);
    QVERIFY(peakAt(mix.samples, mix.firstSample, first, 480) > 0.46f);
    const float middle = peakAt(mix.samples, mix.firstSample, (first + last) / 2 - 240, 480);
    QVERIFY2(middle > 0.22f && middle < 0.28f, qPrintable(QString::number(middle)));
    QVERIFY(peakAt(mix.samples, mix.firstSample, last - 480, 480) < 0.035f);
    QCOMPARE(peakAt(mix.samples, mix.firstSample, last, 1920), 0.0f);

    // Export writes exactly the frames playback renders.
    if (!MediaEncoder::available()) QSKIP("The FFV1/PCM encoder is unavailable");
    const QString out = dir.filePath(QStringLiteral("dissolve.mkv"));
    const MediaExportResult exported = MediaExport::exportProgram(*program, out, 0, program->frameCount);
    QVERIFY2(!exported.error, qPrintable(exported.error.message));
    MediaDecoder decoder;
    QVERIFY(!decoder.open(out));
    for (qint64 frame = 20; frame < 30; ++frame)
        QCOMPARE(differingPixels(decoder.decodeFrame(frame, {}).image,
                                 program->render(frame, program->frameSize, pool, {}).image), 0);
    const MediaAudioBlock sound = decoder.readAudio((first + last) / 2 - 240, 480, 48000, 2);
    QVERIFY(!sound.error);
    const float exportedMiddle = MediaFixture::peak(sound.samples, 0, sound.samples.size());
    QVERIFY2(exportedMiddle > 0.22f && exportedMiddle < 0.28f, qPrintable(QString::number(exportedMiddle)));

    // Wipe and dip use the same window and weights.
    QVERIFY(!s.transition(ClipB, QStringLiteral("wipe"), 4 * F, QStringLiteral("center")));
    const auto wipe = programOf(s.model, paths);
    const QImage wiped = wipe->render(24, wipe->frameSize, pool, {}).image; // w = 24576: 48 columns of B
    QCOMPARE(wiped.pixelColor(5, 5), QColor(191, 191, 191));
    QCOMPARE(wiped.pixelColor(47, 5), QColor(0, 191, 191));
    QCOMPARE(wiped.pixelColor(48, 5), QColor(255, 255, 255));
    QVERIFY(!s.transition(ClipB, QStringLiteral("dip"), 4 * F, QStringLiteral("center")));
    const auto dip = programOf(s.model, paths);
    const QColor dipped = dip->render(23, dip->frameSize, pool, {}).image.pixelColor(5, 5); // w = 8192
    QCOMPARE(dipped.red(), mixed(255, 0, 16384));
}

void SequenceRenderTest::linkedTrimsKeepPictureAndSoundTogether()
{
    if (!MediaEncoder::available()) QSKIP("The FFV1/PCM encoder is unavailable");
    QTemporaryDir dir;
    const QString a = MediaFixture::signal(dir.filePath(QStringLiteral("a.mkv")), 100, 128, 72, 2, 25);
    const QString b = MediaFixture::signal(dir.filePath(QStringLiteral("b.mkv")), 100, 128, 72, 2, 50);
    QVERIFY(!a.isEmpty() && !b.isEmpty());
    const QHash<QString, QString> paths{{QStringLiteral("A"), a}, {QStringLiteral("B"), b}};
    Session s;
    using namespace SequenceFixture;
    QVERIFY(s.model.setContent({S, 1}, {track(V1, kVideo), track(A1, kAudio)},
                               {clip(ClipA, V1, kVideo, 0, 50 * F, 0, 100 * F, QStringLiteral("A"), LinkA),
                                clip(ClipB, V1, kVideo, 50 * F, 50 * F, 25 * F, 100 * F, QStringLiteral("B"), LinkB),
                                clip(SoundA, A1, kAudio, 0, 50 * F, 0, 100 * F, QStringLiteral("A"), LinkA),
                                clip(SoundB, A1, kAudio, 50 * F, 50 * F, 25 * F, 100 * F, QStringLiteral("B"), LinkB)},
                               {}));
    QVERIFY(!s.edit(TimelineEditKind::Roll, {ClipA, ClipB}, 5 * F));
    QVERIFY(!s.edit(TimelineEditKind::Slip, {ClipB}, 3 * F));
    const auto program = programOf(s.model, paths);
    QVERIFY(program);
    MediaDecoderPool pool;
    // A now runs to 55F; B starts there showing source frame 25 + 5 + 3.
    QCOMPARE(codeAt(*program, 54, pool), qint64(54));
    QCOMPARE(codeAt(*program, 55, pool), qint64(33));
    QCOMPARE(codeAt(*program, 99, pool), qint64(77));
    const QString out = dir.filePath(QStringLiteral("trimmed.mkv"));
    QVERIFY(!MediaExport::exportProgram(*program, out, 0, program->frameCount).error);
    MediaDecoder decoder;
    QVERIFY(!decoder.open(out));
    QCOMPARE(MediaExport::readTestSignalCode(decoder.decodeFrame(55, {}).image), qint64(33));
    // B's source pop (frame 50) lands on program frame 55 + 17 = 72 in
    // picture and in sound, because the sound slipped with the picture.
    for (const qint64 frame : {50, 72})
        QCOMPARE(decoder.decodeFrame(frame, {}).image.pixelColor(5, 5), QColor(255, 255, 255));
    QCOMPARE(decoder.decodeFrame(71, {}).image.pixelColor(5, 5), QColor(191, 191, 191));
    for (const qint64 frame : {50, 72}) {
        const MediaAudioBlock edge = decoder.readAudio(frame * 1920 - 10, 20, 48000, 2);
        QVERIFY2(!edge.error, qPrintable(edge.error.message));
        QCOMPARE(MediaFixture::peak(edge.samples, 0, 20), 0.0f);
        QVERIFY(MediaFixture::peak(edge.samples, 20, 20) > 0.1f);
    }
}

void SequenceRenderTest::multicamSwitchesRenderSyncedAngles()
{
    QTemporaryDir dir;
    const QString one = MediaFixture::signal(dir.filePath(QStringLiteral("cam1.mkv")), 100, 128, 72, 0, 0);
    const QString two = MediaFixture::signal(dir.filePath(QStringLiteral("cam2.mkv")), 100, 128, 72, 0, 0);
    QVERIFY(!one.isEmpty() && !two.isEmpty());
    const QHash<QString, QString> paths{{QStringLiteral("cam1"), one}, {QStringLiteral("cam2"), two}};
    Session s;
    using namespace SequenceFixture;
    QVERIFY(s.model.setContent({S, 1}, {track(V1, kVideo)}, {}, {}));
    MulticamModel multicam;
    multicam.setTimeline(&s.model);
    multicam.setTrackId(uid(V1).toString(QUuid::WithoutBraces));
    // cam2 started recording 10 frames before cam1.
    QVERIFY(multicam.addAngle({{QStringLiteral("angleId"), QStringLiteral("one")},
                               {QStringLiteral("resourceId"), QStringLiteral("cam1")},
                               {QStringLiteral("sourceDurationTicks"), QString::number(100 * F)}}));
    QVERIFY(multicam.addAngle({{QStringLiteral("angleId"), QStringLiteral("two")},
                               {QStringLiteral("resourceId"), QStringLiteral("cam2")},
                               {QStringLiteral("syncOffsetTicks"), QString::number(10 * F)},
                               {QStringLiteral("sourceDurationTicks"), QString::number(100 * F)}}));
    QVERIFY(multicam.insertProgram(QStringLiteral("one"), QStringLiteral("0"), QString::number(60 * F))
                .value(QStringLiteral("ok")).toBool());
    // Mock playback: the playhead advances frame by frame; the editor cuts live.
    for (qint64 frame = 0; frame < 60; ++frame) {
        multicam.setPlayheadTicks(QString::number(frame * F));
        if (frame == 20) QVERIFY(multicam.switchAngle(QStringLiteral("two")).value(QStringLiteral("ok")).toBool());
        if (frame == 40) QVERIFY(multicam.switchAngle(QStringLiteral("one")).value(QStringLiteral("ok")).toBool());
    }
    QCOMPARE(s.model.clipCount(), 3);
    QVERIFY(multicam.unsyncedClipIds().isEmpty());
    const QList<std::pair<qint64, qint64>> codes{{19, 19}, {20, 30}, {39, 49}, {40, 40}, {59, 59}};
    MediaDecoderPool pool;
    const auto program = programOf(s.model, paths);
    QVERIFY(program);
    for (const auto &[frame, code] : codes) QCOMPARE(codeAt(*program, frame, pool), code);
    QCOMPARE(program->render(20, program->frameSize, pool, {}).sources.first().resourceId, QStringLiteral("cam2"));

    // Reopen from the saved timeline and angle table: same cuts, same sync.
    Session reopened;
    QVERIFY(reopened.model.loadJson(s.model.toJson()).value(QStringLiteral("ok")).toBool());
    MulticamModel restored;
    QVERIFY(restored.loadContent(multicam.contentMap()));
    restored.setTimeline(&reopened.model);
    QCOMPARE(restored.angleAt(QString::number(20 * F)), QStringLiteral("two"));
    QCOMPARE(restored.angleAt(QString::number(40 * F)), QStringLiteral("one"));
    QVERIFY(restored.unsyncedClipIds().isEmpty());
    const auto again = programOf(reopened.model, paths);
    for (const auto &[frame, code] : codes) QCOMPARE(codeAt(*again, frame, pool), code);
}

QTEST_MAIN(SequenceRenderTest)
#include "tst_sequence_render.moc"
