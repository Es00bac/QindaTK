// SPDX-License-Identifier: LGPL-3.0-or-later
#include "asset_provider.h"
#include "media_fixture.h"

#include <QColorSpace>
#include <QDir>
#include <QTemporaryDir>
#include <QtTest>
#include <cmath>
#include <initializer_list>

using namespace QindaTK;
using namespace MediaFixture;

namespace {
QVector<MediaVideoFrameEntry> entries(std::initializer_list<qint64> pts, qint64 duration = 0)
{
    QVector<MediaVideoFrameEntry> frames;
    for (const qint64 value : pts) frames.append({value, value, duration, false});
    return frames;
}

qint64 at(const std::optional<qint64> &value)
{
    return value.value_or(-99);
}

constexpr double kPi = 3.14159265358979323846;
} // namespace

class TstMediaDecode final : public QObject {
    Q_OBJECT
private slots:
    void ratePolicyAndFrameIdentityAreExact()
    {
        // 24000/1001 video stored with Matroska's rounded millisecond stamps.
        MediaSourceInfo film;
        film.ptsPerSecond = {1000, 1};
        for (qint64 i = 0; i < 48; ++i) {
            const qint64 ms = (2 * i * 1001 + 24) / 48; // i * 1001 / 24, rounded half up
            film.frames.append({ms, ms, 42, true});
        }
        film.finalizeIndex(TimelineRate{24000, 1001}, TimelineRate{24000, 1001});
        QVERIFY(film.constantRate);
        QCOMPARE(film.nominalRate.numerator, qint64(24000));
        QCOMPARE(film.nominalRate.denominator, qint64(1001));
        QCOMPARE(at(film.frameStartTicks(1, kTimebase)), qint64(29429400));
        QCOMPARE(at(film.frameStartTicks(47, kTimebase)), qint64(1383181800));
        QCOMPARE(at(film.durationTicks(kTimebase)), qint64(1412611200));
        QCOMPARE(at(film.frameAtTicks(29429399, kTimebase)), qint64(0));
        QCOMPARE(at(film.frameAtTicks(29429400, kTimebase)), qint64(1));
        QCOMPARE(at(film.frameAtTicks(1412611199, kTimebase)), qint64(47));
        QVERIFY(!film.frameAtTicks(1412611200, kTimebase));
        QVERIFY(!film.frameAtTicks(-1, kTimebase));
        QCOMPARE(at(film.frameStartTicks(1, TimelineRate{48000, 1})), qint64(2002));
        for (qint64 i = 0; i < 48; ++i)
            QCOMPARE(at(film.frameAtTicks(at(film.frameStartTicks(i, kTimebase)), kTimebase)), i);

        // Uneven timestamps keep their own times (variable-rate policy).
        MediaSourceInfo vfr;
        vfr.ptsPerSecond = {1000, 1};
        vfr.frames = entries({0, 40, 120, 160, 280}, 40);
        vfr.finalizeIndex(TimelineRate{25, 1}, TimelineRate{25, 1});
        QVERIFY(!vfr.constantRate);
        QCOMPARE(at(vfr.frameStartTicks(2, kTimebase)), qint64(84672000));
        QCOMPARE(at(vfr.frameAtTicks(84671999, kTimebase)), qint64(1));
        QCOMPARE(at(vfr.frameAtTicks(84672000, kTimebase)), qint64(2));
        QCOMPARE(at(vfr.frameAtTicks(197567999, kTimebase)), qint64(3));
        QCOMPARE(at(vfr.frameAtTicks(197568000, kTimebase)), qint64(4));
        QCOMPARE(at(vfr.durationTicks(kTimebase)), qint64(225792000));
        QCOMPARE(at(vfr.frameAtTicks(225791999, kTimebase)), qint64(4));
        QVERIFY(!vfr.frameAtTicks(225792000, kTimebase));

        // Decode-order packets sort to presentation order; duplicates are damage.
        MediaSourceInfo reordered;
        reordered.ptsPerSecond = {1000, 1};
        reordered.frames = entries({0, 120, 40, 80, 80});
        reordered.frames[0].key = true;
        reordered.frames[3].key = true;
        reordered.finalizeIndex(TimelineRate{25, 1}, std::nullopt);
        QCOMPARE(reordered.frames.size(), 4);
        QCOMPARE(reordered.frames.at(1).pts, qint64(40));
        QCOMPARE(reordered.frames.at(3).pts, qint64(120));
        QVERIFY(reordered.constantRate);
        QVERIFY(reordered.damaged);
        QVERIFY(reordered.damage.contains(QStringLiteral("1 duplicate")));
        QCOMPARE(reordered.keyframeAtOrBefore(1), qint64(0));
        QCOMPARE(reordered.keyframeAtOrBefore(2), qint64(2));
        QCOMPARE(reordered.keyframeAtOrBefore(3), qint64(2));
        QCOMPARE(reordered.indexOfPts(80), qint64(2));
        QCOMPARE(reordered.indexOfPts(81), qint64(-1));
    }

    void colourPolicyTagsFrames()
    {
        MediaSourceInfo info;
        QString policy;
        info.colorPrimaries = QStringLiteral("bt709");
        info.colorTransfer = QStringLiteral("bt709");
        QVERIFY(info.frameColorSpace(&policy) == QColorSpace(QColorSpace::SRgb));
        QCOMPARE(policy, QStringLiteral("srgb-approximation"));
        info.colorPrimaries = QStringLiteral("bt2020");
        info.colorTransfer = QStringLiteral("smpte2084");
        const QColorSpace pq = info.frameColorSpace(&policy);
        QCOMPARE(policy, QStringLiteral("hdr-pq"));
        QVERIFY(pq.primaries() == QColorSpace::Primaries::Bt2020);
        QVERIFY(pq.transferFunction() == QColorSpace::TransferFunction::St2084);
        info.colorTransfer = QStringLiteral("arib-std-b67");
        QVERIFY(info.frameColorSpace(&policy).transferFunction() == QColorSpace::TransferFunction::Hlg);
        QCOMPARE(policy, QStringLiteral("hdr-hlg"));
        info.colorTransfer = QStringLiteral("bt709");
        QVERIFY(info.frameColorSpace(&policy).primaries() == QColorSpace::Primaries::Bt2020);
        QCOMPARE(policy, QStringLiteral("wide-gamut-bt2020"));
    }

    void testSignalPopPhaseMovesTheFlash()
    {
        // A phase keeps frame 0, which monitors and thumbnails show first, on bars.
        MediaTestSignalSettings settings;
        settings.frameSize = QSize(128, 72);
        settings.popInterval = 25;
        settings.popPhase = 12;
        const QColor bars(191, 191, 191);
        const QColor flash(255, 255, 255);
        QCOMPARE(MediaExport::testSignalFrame(0, settings).pixelColor(5, 5), bars);
        QCOMPARE(MediaExport::testSignalFrame(11, settings).pixelColor(5, 5), bars);
        QCOMPARE(MediaExport::testSignalFrame(12, settings).pixelColor(5, 5), flash);
        QCOMPARE(MediaExport::testSignalFrame(25, settings).pixelColor(5, 5), bars);
        QCOMPARE(MediaExport::testSignalFrame(37, settings).pixelColor(5, 5), flash);
        for (const qint64 frame : {0, 12, 37, 4095})
            QCOMPARE(MediaExport::readTestSignalCode(MediaExport::testSignalFrame(frame, settings)), frame);
        settings.popPhase = 0;
        QCOMPARE(MediaExport::testSignalFrame(0, settings).pixelColor(5, 5), flash);
        settings.popPhase = -1;
        QCOMPARE(MediaExport::writeTestSignal(QStringLiteral("unused.mkv"), settings).error.code,
                 ErrorCode::InvalidArgument);
    }

    void testSignalFramesDecodeExactly()
    {
        if (!MediaDecoder::available()) QSKIP("Built without the FFmpeg media backend");
        QTemporaryDir dir;
        const QString path = signal(dir.filePath(QStringLiteral("bars.mkv")), 50, 320, 180, 2, 25);
        QVERIFY(!path.isEmpty());
        MediaDecoder decoder;
        const OperationError opened = decoder.open(fileUri(path));
        QVERIFY2(!opened, qPrintable(opened.message));
        const MediaSourceInfoPtr info = decoder.info();
        QCOMPARE(info->frameCount(), qint64(50));
        QVERIFY(info->constantRate);
        QCOMPARE(info->nominalRate.numerator, qint64(25));
        QCOMPARE(info->nominalRate.denominator, qint64(1));
        QCOMPARE(info->width, 320);
        QCOMPARE(info->height, 180);
        QCOMPARE(info->videoCodec, QStringLiteral("ffv1"));
        QVERIFY(info->hasAudio());
        QCOMPARE(info->audioSampleRate, 48000);
        QCOMPARE(info->audioChannels, 2);
        QCOMPARE(info->audioSampleCount, qint64(96000));
        QCOMPARE(info->audioOffsetSamples, qint64(0));
        QVERIFY(!info->damaged);
        QCOMPARE(at(info->durationTicks(kTimebase)), 50 * kFrame25);
        for (const qint64 index : {49, 0, 24, 25, 26, 1}) {
            const MediaDecodedFrame frame = decoder.decodeFrame(index, {});
            QVERIFY2(!frame.error, qPrintable(frame.error.message));
            QCOMPARE(frame.index, index);
            QCOMPARE(frame.pts, info->frames.at(index).pts);
            QCOMPARE(frame.image.size(), QSize(320, 180));
            QCOMPARE(MediaExport::readTestSignalCode(frame.image), index);
            QVERIFY(frame.image.colorSpace() == QColorSpace(QColorSpace::SRgb));
        }
        QCOMPARE(decoder.decodeFrame(24, {}).image.pixelColor(5, 5), QColor(191, 191, 191));
        QCOMPARE(decoder.decodeFrame(25, {}).image.pixelColor(5, 5), QColor(255, 255, 255));
        const MediaDecodedFrame scaled = decoder.decodeFrame(10, QSize(160, 90));
        QCOMPARE(scaled.image.size(), QSize(160, 90));
        QCOMPARE(MediaExport::readTestSignalCode(scaled.image), qint64(10));
        QCOMPARE(decoder.decodeFrame(50, {}).error.code, ErrorCode::InvalidArgument);
        QCOMPARE(decoder.decodeFrame(-1, {}).error.code, ErrorCode::InvalidArgument);
    }

    void audioIsSampleExactAndConverts()
    {
        if (!MediaDecoder::available()) QSKIP("Built without the FFmpeg media backend");
        QTemporaryDir dir;
        const QString path = signal(dir.filePath(QStringLiteral("pop.mkv")), 50, 128, 72, 2, 25);
        QVERIFY(!path.isEmpty());
        MediaDecoder decoder;
        QVERIFY(!decoder.open(path));
        const MediaAudioBlock silent = decoder.readAudio(1920, 1920, 48000, 2);
        QVERIFY2(!silent.error, qPrintable(silent.error.message));
        QCOMPARE(silent.samples.size(), 3840);
        QCOMPARE(silent.decodedFrames, qint64(1920));
        QCOMPARE(peak(silent.samples, 0, silent.samples.size()), 0.0f);
        // Frame 25 (the second pop) starts at sample 48000 exactly.
        const MediaAudioBlock edge = decoder.readAudio(47990, 20, 48000, 2);
        QVERIFY2(!edge.error, qPrintable(edge.error.message));
        QCOMPARE(peak(edge.samples, 0, 20), 0.0f);
        // The generator writes float samples that the encoder rounds to s16.
        const float written = float(0.5 * std::sin(2.0 * kPi * 1000.0 * 48003.0 / 48000.0));
        const double expected = double(std::lrint(double(written) * 32768.0)) / 32768.0;
        QVERIFY(std::fabs(edge.samples.at(2 * 13) - expected) < 1e-6);
        QVERIFY(std::fabs(edge.samples.at(2 * 13 + 1) - expected) < 1e-6);
        const MediaAudioBlock mono = decoder.readAudio(48000, 48, 48000, 1);
        QVERIFY(!mono.error);
        QCOMPARE(mono.samples.size(), 48);
        QVERIFY(std::fabs(mono.samples.at(3) - expected) < 1e-6);
        const MediaAudioBlock resampled = decoder.readAudio(0, 4410, 44100, 2);
        QVERIFY2(!resampled.error, qPrintable(resampled.error.message));
        QCOMPARE(resampled.samples.size(), 8820);
        QVERIFY(peak(resampled.samples, 2 * 90, 40) > 0.3f);   // inside the 40 ms pop
        QVERIFY(peak(resampled.samples, 2 * 3000, 200) < 1e-3f); // well after it
        QCOMPARE(decoder.readAudio(0, 10, 48000, 0).error.code, ErrorCode::InvalidArgument);
    }

    void longSeeksAcrossGopsWithBFrames()
    {
        if (!MediaDecoder::available()) QSKIP("Built without the FFmpeg media backend");
        if (ffmpeg().isEmpty()) QSKIP("The ffmpeg command-line tool is unavailable");
        QTemporaryDir dir;
        const QString raw = dir.filePath(QStringLiteral("code.rgb"));
        const QString mp4 = dir.filePath(QStringLiteral("gop.mp4"));
        QVERIFY(writeFile(raw, blockCodeFrames(600)));
        QVERIFY(runFfmpeg({QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"),
                           QStringLiteral("rgb24"), QStringLiteral("-s"), QStringLiteral("160x16"),
                           QStringLiteral("-r"), QStringLiteral("25"), QStringLiteral("-i"), raw,
                           QStringLiteral("-c:v"), QStringLiteral("mpeg4"), QStringLiteral("-q:v"), QStringLiteral("2"),
                           QStringLiteral("-g"), QStringLiteral("25"), QStringLiteral("-bf"), QStringLiteral("2"),
                           QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), mp4}));
        MediaDecoder decoder;
        const OperationError opened = decoder.open(mp4);
        QVERIFY2(!opened, qPrintable(opened.message));
        const MediaSourceInfoPtr info = decoder.info();
        QCOMPARE(info->frameCount(), qint64(600));
        QVERIFY(info->constantRate);
        QCOMPARE(info->nominalRate.numerator, qint64(25));
        QCOMPARE(info->videoCodec, QStringLiteral("mpeg4"));
        QCOMPARE(at(info->frameStartTicks(599, kTimebase)), 599 * kFrame25);
        // The encoder chooses its key frames (about every 25); find the first.
        qint64 key = -1;
        for (qint64 i = 1; i < info->frameCount() && key < 0; ++i)
            if (info->frames.at(i).key) key = i;
        QVERIFY(key > 1 && key < 60);
        for (const qint64 index : {qint64(599), qint64(0), qint64(300), qint64(301), key - 1, key, key + 1,
                                   qint64(575), qint64(1), qint64(598)}) {
            const MediaDecodedFrame frame = decoder.decodeFrame(index, {});
            QVERIFY2(!frame.error, qPrintable(frame.error.message));
            QCOMPARE(frame.pts, info->frames.at(index).pts);
            QCOMPARE(blockCode(frame.image), index);
        }
    }

    void variableRateIndexHoldsFrames()
    {
        if (!MediaDecoder::available()) QSKIP("Built without the FFmpeg media backend");
        if (ffmpeg().isEmpty()) QSKIP("The ffmpeg command-line tool is unavailable");
        QTemporaryDir dir;
        const QString raw = dir.filePath(QStringLiteral("vfr.rgb"));
        const QString mkv = dir.filePath(QStringLiteral("vfr.mkv"));
        QVERIFY(writeFile(raw, pixelCodeFrames(5, 16, 8)));
        QVERIFY(runFfmpeg({QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"),
                           QStringLiteral("rgb24"), QStringLiteral("-s"), QStringLiteral("16x8"),
                           QStringLiteral("-r"), QStringLiteral("25"), QStringLiteral("-i"), raw, QStringLiteral("-vf"),
                           QStringLiteral("setpts='if(eq(N,0),0,if(eq(N,1),1,if(eq(N,2),3,if(eq(N,3),4,7))))*0.04/TB'"),
                           QStringLiteral("-fps_mode"), QStringLiteral("vfr"), QStringLiteral("-c:v"),
                           QStringLiteral("ffv1"), QStringLiteral("-pix_fmt"), QStringLiteral("bgr0"), mkv}));
        MediaDecoder decoder;
        const OperationError opened = decoder.open(mkv);
        QVERIFY2(!opened, qPrintable(opened.message));
        const MediaSourceInfoPtr info = decoder.info();
        QCOMPARE(info->frameCount(), qint64(5));
        QVERIFY(!info->constantRate);
        QCOMPARE(at(info->frameStartTicks(2, kTimebase)), qint64(84672000));
        QCOMPARE(at(info->frameAtTicks(84671999, kTimebase)), qint64(1));
        QCOMPARE(at(info->durationTicks(kTimebase)), qint64(225792000));
        for (const qint64 index : {2, 4, 0, 3}) {
            const MediaDecodedFrame frame = decoder.decodeFrame(index, {});
            QVERIFY2(!frame.error, qPrintable(frame.error.message));
            QCOMPARE(qint64(qRed(frame.image.pixel(0, 0))), index);
        }
    }

    void audioOffsetPlacesSamplesOnMediaTime()
    {
        if (!MediaDecoder::available()) QSKIP("Built without the FFmpeg media backend");
        if (ffmpeg().isEmpty()) QSKIP("The ffmpeg command-line tool is unavailable");
        QTemporaryDir dir;
        const QString video = dir.filePath(QStringLiteral("video.rgb"));
        const QString audio = dir.filePath(QStringLiteral("audio.pcm"));
        const QString mkv = dir.filePath(QStringLiteral("offset.mkv"));
        // The burst sits at stream sample 14400; the stream starts 100 ms
        // (4800 samples) after the first video frame, so it plays at media
        // sample 19200, the start of frame 10 (the white frame).
        QVERIFY(writeFile(video, pixelCodeFrames(25, 16, 8, 10)));
        QVERIFY(writeFile(audio, burstPcm(48000, 14400, 1920)));
        QVERIFY(runFfmpeg({QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"),
                           QStringLiteral("rgb24"), QStringLiteral("-s"), QStringLiteral("16x8"), QStringLiteral("-r"),
                           QStringLiteral("25"), QStringLiteral("-i"), video, QStringLiteral("-f"), QStringLiteral("s16le"),
                           QStringLiteral("-ar"), QStringLiteral("48000"), QStringLiteral("-ac"), QStringLiteral("1"),
                           QStringLiteral("-i"), audio, QStringLiteral("-af"), QStringLiteral("asetpts=PTS+0.1/TB"),
                           QStringLiteral("-c:v"), QStringLiteral("ffv1"), QStringLiteral("-pix_fmt"),
                           QStringLiteral("bgr0"), QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), mkv}));
        MediaDecoder decoder;
        const OperationError opened = decoder.open(mkv);
        QVERIFY2(!opened, qPrintable(opened.message));
        QCOMPARE(decoder.info()->audioOffsetSamples, qint64(4800));
        const MediaAudioBlock block = decoder.readAudio(19190, 20, 48000, 1);
        QVERIFY2(!block.error, qPrintable(block.error.message));
        for (int i = 0; i < 10; ++i) QCOMPARE(block.samples.at(i), 0.0f);
        for (int i = 10; i < 20; ++i) QCOMPARE(block.samples.at(i), 0.5f);
        const MediaAudioBlock lead = decoder.readAudio(0, 4800, 48000, 1);
        QVERIFY(!lead.error);
        QCOMPARE(lead.decodedFrames, qint64(0)); // no audio exists before its stream starts
        QCOMPARE(qRed(decoder.decodeFrame(10, {}).image.pixel(0, 0)), 10);
        QCOMPARE(decoder.decodeFrame(10, {}).image.pixelColor(8, 4), QColor(255, 255, 255));
    }

    void missingDamagedAndTruncatedMedia()
    {
        if (!MediaDecoder::available()) QSKIP("Built without the FFmpeg media backend");
        QTemporaryDir dir;
        MediaDecoder decoder;
        QCOMPARE(decoder.open(dir.filePath(QStringLiteral("absent.mkv"))).code, ErrorCode::Unavailable);
        const QString text = dir.filePath(QStringLiteral("notes.mkv"));
        QVERIFY(writeFile(text, QByteArrayLiteral("not a media file")));
        QCOMPARE(decoder.open(text).code, ErrorCode::Unsupported);
        QVERIFY(!decoder.isOpen());
        const QString full = signal(dir.filePath(QStringLiteral("full.mkv")), 100, 128, 72, 2, 25);
        QVERIFY(!full.isEmpty());
        QFile source(full);
        QVERIFY(source.open(QIODevice::ReadOnly));
        const QByteArray bytes = source.readAll();
        const QString cut = dir.filePath(QStringLiteral("cut.mkv"));
        QVERIFY(writeFile(cut, bytes.left(bytes.size() * 6 / 10)));
        const OperationError opened = decoder.open(cut);
        QVERIFY2(!opened, qPrintable(opened.message));
        const MediaSourceInfoPtr info = decoder.info();
        QVERIFY(info->truncated);
        QVERIFY(info->damaged);
        QVERIFY(info->frameCount() > 0 && info->frameCount() < 100);
        QCOMPARE(MediaExport::readTestSignalCode(decoder.decodeFrame(0, {}).image), qint64(0));
    }

    void cancellationStopsAndDecoderStaysUsable()
    {
        if (!MediaDecoder::available()) QSKIP("Built without the FFmpeg media backend");
        QTemporaryDir dir;
        const QString path = signal(dir.filePath(QStringLiteral("cancel.mkv")), 30, 128, 72, 2, 25);
        QVERIFY(!path.isEmpty());
        CancellationSource stop;
        stop.cancel();
        MediaDecoder decoder;
        QCOMPARE(decoder.open(path, stop.token()).code, ErrorCode::Cancelled);
        QVERIFY(!decoder.isOpen());
        QVERIFY(!decoder.open(path));
        QCOMPARE(decoder.decodeFrame(5, {}, stop.token()).error.code, ErrorCode::Cancelled);
        QCOMPARE(MediaExport::readTestSignalCode(decoder.decodeFrame(5, {}).image), qint64(5));
        QCOMPARE(decoder.readAudio(0, 100, 48000, 2, stop.token()).error.code, ErrorCode::Cancelled);
        QVERIFY(!decoder.readAudio(0, 100, 48000, 2).error);
        // Export cancelled after its first frame leaves no partial file.
        const QString proxy = dir.filePath(QStringLiteral("proxy.mkv"));
        CancellationSource midway;
        const MediaExportResult result = MediaExport::writeProxy(path, proxy, 36, midway.token(),
                                                                 [&midway](double) { midway.cancel(); });
        QCOMPARE(result.error.code, ErrorCode::Cancelled);
        QVERIFY(!QFile::exists(proxy));
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).filter(QStringLiteral(".part")).size(), 0);
    }

    void decoderPoolReopensChangedFiles()
    {
        if (!MediaDecoder::available()) QSKIP("Built without the FFmpeg media backend");
        QTemporaryDir dir;
        const QString path = signal(dir.filePath(QStringLiteral("pool.mkv")), 20);
        QVERIFY(!path.isEmpty());
        MediaDecoderPool pool;
        OperationError error;
        MediaDecoder *first = pool.acquire(fileUri(path), {}, &error);
        QVERIFY2(first, qPrintable(error.message));
        QCOMPARE(pool.acquire(path, {}, &error), first);
        QCOMPARE(first->info()->frameCount(), qint64(20));
        QVERIFY(!signal(path, 35).isEmpty()); // replaced in place (atomic rename)
        MediaDecoder *second = pool.acquire(path, {}, &error);
        QVERIFY2(second, qPrintable(error.message));
        QCOMPARE(second->info()->frameCount(), qint64(35));
        QVERIFY(QFile::remove(path));
        QVERIFY(!pool.acquire(path, {}, &error));
        QCOMPARE(error.code, ErrorCode::Unavailable);
        QCOMPARE(pool.size(), 0);
    }

    void assetBackendResolvesTimesToExactFrames()
    {
        if (!MediaDecoder::available()) QSKIP("Built without the FFmpeg media backend");
        QTemporaryDir dir;
        const QString path = signal(dir.filePath(QStringLiteral("asset.mkv")), 40);
        QVERIFY(!path.isEmpty());
        FileAssetBackend backend;
        AssetRequest request;
        request.uri = path;
        request.kind = QStringLiteral("frame");
        request.timeTicks = 25 * kFrame25 + 1;
        request.rateNumerator = kTicks;
        request.rateDenominator = 1;
        AssetResult result = backend.fetch(request, {}, {});
        QVERIFY2(!result.error, qPrintable(result.error.message));
        QVERIFY(result.metadata.value(QStringLiteral("exactFrameIndex")).toBool());
        QCOMPARE(result.metadata.value(QStringLiteral("frameIndex")).toString(), QStringLiteral("25"));
        QImage image;
        QVERIFY(image.loadFromData(result.bytes, "PNG"));
        QCOMPARE(MediaExport::readTestSignalCode(image), qint64(25));
        request.kind = QStringLiteral("metadata");
        result = backend.fetch(request, {}, {});
        QVERIFY(!result.error);
        QCOMPARE(result.metadata.value(QStringLiteral("frameCount")).toString(), QStringLiteral("40"));
        QCOMPARE(result.metadata.value(QStringLiteral("mediaType")).toString(), QStringLiteral("video"));
    }
};

QTEST_MAIN(TstMediaDecode)
#include "tst_media_decode.moc"
