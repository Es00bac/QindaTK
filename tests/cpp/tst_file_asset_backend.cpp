// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/asset_provider.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

using namespace QindaTK;

class TstFileAssetBackend final : public QObject {
    Q_OBJECT
private:
    static void put16(QByteArray &bytes, quint16 value)
    {
        bytes.append(char(value & 0xff)); bytes.append(char(value >> 8));
    }
    static void put32(QByteArray &bytes, quint32 value)
    {
        put16(bytes, quint16(value & 0xffff)); put16(bytes, quint16(value >> 16));
    }
    static QByteArray wave()
    {
        QByteArray fmt("fmt ", 4); put32(fmt, 16); put16(fmt, 1); put16(fmt, 1);
        put32(fmt, 8000); put32(fmt, 16000); put16(fmt, 2); put16(fmt, 16);
        QByteArray samples;
        for (qint16 sample : {qint16(0), qint16(16384), qint16(-8192), qint16(32767)})
            put16(samples, quint16(sample));
        QByteArray data("data", 4); put32(data, quint32(samples.size())); data += samples;
        QByteArray body("WAVE", 4); body += fmt; body += data;
        QByteArray riff("RIFF", 4); put32(riff, quint32(body.size())); riff += body;
        return riff;
    }
    static QByteArray wave24StereoAcrossReadBoundary()
    {
        constexpr quint32 frames = 43691;
        constexpr quint32 dataSize = frames * 6;
        QByteArray fmt("fmt ", 4); put32(fmt, 16); put16(fmt, 1); put16(fmt, 2);
        put32(fmt, 48000); put32(fmt, 288000); put16(fmt, 6); put16(fmt, 24);
        QByteArray samples(int(dataSize), char(0));
        const int last = int((frames - 1) * 6);
        samples[last] = char(0xff); samples[last + 1] = char(0xff); samples[last + 2] = char(0x7f);
        QByteArray data("data", 4); put32(data, dataSize); data += samples;
        QByteArray body("WAVE", 4); body += fmt; body += data;
        QByteArray riff("RIFF", 4); put32(riff, quint32(body.size())); riff += body;
        return riff;
    }
    static QString write(const QString &path, const QByteArray &bytes)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
        return path;
    }

private slots:
    void thumbnailIsScaledPngAndReportsSourceMetadata()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage source(80, 40, QImage::Format_RGB32);
        source.fill(Qt::cyan);
        const QString path = dir.filePath(QStringLiteral("source.png"));
        QVERIFY(source.save(path));
        AssetRequest request;
        request.uri = path; request.kind = QStringLiteral("thumbnail"); request.size = QSize(20, 20);
        FileAssetBackend backend;
        const AssetResult result = backend.fetch(request, {}, {});
        QVERIFY2(!result.error, qPrintable(result.error.message));
        QCOMPARE(result.mimeType, QStringLiteral("image/png"));
        QCOMPARE(result.metadata.value(QStringLiteral("sourceWidth")).toInt(), 80);
        QCOMPARE(result.metadata.value(QStringLiteral("sourceHeight")).toInt(), 40);
        QBuffer buffer;
        buffer.setData(result.bytes);
        QVERIFY(buffer.open(QIODevice::ReadOnly));
        QImageReader reader(&buffer);
        QCOMPARE(reader.size(), QSize(20, 10));
    }

    void waveformPeaksAreDeterministic()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = write(dir.filePath(QStringLiteral("samples.wav")), wave());
        QVERIFY(!path.isEmpty());
        AssetRequest request;
        request.uri = path; request.kind = QStringLiteral("waveform"); request.columns = 2;
        FileAssetBackend backend;
        const AssetResult result = backend.fetch(request, {}, {});
        QVERIFY2(!result.error, qPrintable(result.error.message));
        QCOMPARE(result.peaks.size(), 2);
        QCOMPARE(result.peaks.at(0).toDouble(), 0.5);
        QCOMPARE(result.peaks.at(1).toDouble(), 32767.0 / 32768.0);
        QCOMPARE(result.metadata.value(QStringLiteral("sampleRate")).toUInt(), 8000U);
        QCOMPARE(result.metadata.value(QStringLiteral("channels")).toUInt(), 1U);
        QCOMPARE(result.metadata.value(QStringLiteral("frames")).toLongLong(), 4LL);
    }

    void waveformKeepsFramesAcrossUnalignedReadBoundary()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = write(dir.filePath(QStringLiteral("stereo24.wav")),
                                   wave24StereoAcrossReadBoundary());
        QVERIFY(!path.isEmpty());
        AssetRequest request; request.uri = path; request.kind = QStringLiteral("waveform");
        request.columns = 1;
        FileAssetBackend backend;
        const AssetResult result = backend.fetch(request, {}, {});
        QVERIFY2(!result.error, qPrintable(result.error.message));
        QCOMPARE(result.metadata.value(QStringLiteral("frames")).toLongLong(), 43691LL);
        QCOMPARE(result.peaks.size(), 1);
        QVERIFY(result.peaks.first().toDouble() > 0.999);
    }

    void metadataHandlesOrdinaryFileAndMissingPath()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = write(dir.filePath(QStringLiteral("notes.txt")), QByteArrayLiteral("hello"));
        QVERIFY(!path.isEmpty());
        FileAssetBackend backend;
        AssetRequest request; request.uri = path;
        AssetResult result = backend.fetch(request, {}, {});
        QVERIFY(!result.error);
        QCOMPARE(result.metadata.value(QStringLiteral("mediaType")).toString(), QStringLiteral("file"));
        QCOMPARE(result.metadata.value(QStringLiteral("fileSize")).toLongLong(), 5LL);
        request.uri = dir.filePath(QStringLiteral("absent.png"));
        result = backend.fetch(request, {}, {});
        QCOMPARE(result.error.code, ErrorCode::Unavailable);
    }

    void unsupportedVideoAndCancellationAreExplicit()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = write(dir.filePath(QStringLiteral("clip.mp4")), QByteArrayLiteral("not a video"));
        QVERIFY(!path.isEmpty());
        FileAssetBackend backend;
        AssetRequest request; request.uri = path; request.kind = QStringLiteral("filmstrip");
        AssetResult result = backend.fetch(request, {}, {});
        QCOMPARE(result.error.code, ErrorCode::Unsupported);
        CancellationSource source; source.cancel();
        request.kind = QStringLiteral("metadata");
        result = backend.fetch(request, source.token(), {});
        QCOMPARE(result.error.code, ErrorCode::Cancelled);
    }

    void pdfPageRendersAtRequestedSize()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("pdftoppm")).isEmpty()
            || QStandardPaths::findExecutable(QStringLiteral("pdfinfo")).isEmpty()
            || QStandardPaths::findExecutable(QStringLiteral("pdftotext")).isEmpty())
            QSKIP("Poppler command-line tools are unavailable");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("sample.pdf"));
        {
            QPdfWriter writer(path);
            writer.setPageSize(QPageSize(QPageSize::A4));
            QPainter painter(&writer);
            QVERIFY(painter.isActive());
            painter.fillRect(QRect(100, 100, 500, 300), Qt::red);
            painter.drawText(QPoint(100, 500), QStringLiteral("Searchable PDF word"));
        }
        FileAssetBackend backend;
        AssetRequest request;
        request.uri = path;
        request.kind = QStringLiteral("metadata");
        AssetResult result = backend.fetch(request, {}, {});
        QVERIFY2(!result.error, qPrintable(result.error.message));
        QCOMPARE(result.metadata.value(QStringLiteral("pageCount")).toInt(), 1);
        request.kind = QStringLiteral("pdf-page");
        request.size = QSize(320, 320);
        result = backend.fetch(request, {}, {});
        QVERIFY2(!result.error, qPrintable(result.error.message));
        QImage image;
        QVERIFY(image.loadFromData(result.bytes, "PNG"));
        QCOMPARE(std::max(image.width(), image.height()), 320);
        request.rotation = 90;
        result = backend.fetch(request, {}, {});
        QVERIFY2(!result.error, qPrintable(result.error.message));
        QImage rotated;
        QVERIFY(rotated.loadFromData(result.bytes, "PNG"));
        QCOMPARE(rotated.size(), QSize(image.height(), image.width()));
        request.kind = QStringLiteral("pdf-text");
        result = backend.fetch(request, {}, {});
        QVERIFY2(!result.error, qPrintable(result.error.message));
        QVERIFY(result.metadata.value(QStringLiteral("text")).toString()
                    .contains(QStringLiteral("Searchable")));
        QVERIFY(!result.metadata.value(QStringLiteral("words")).toList().isEmpty());
    }

    void videoFrameAndThumbnailDecodeRealMedia()
    {
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (ffmpeg.isEmpty() || QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty())
            QSKIP("FFmpeg command-line tools are unavailable");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("clip.mkv"));
        QProcess encoder;
        encoder.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
            QStringLiteral("error"), QStringLiteral("-nostdin"), QStringLiteral("-f"),
            QStringLiteral("lavfi"), QStringLiteral("-i"),
            QStringLiteral("testsrc=size=32x16:rate=2"), QStringLiteral("-frames:v"),
            QStringLiteral("2"), QStringLiteral("-c:v"), QStringLiteral("ffv1"), path});
        QVERIFY(encoder.waitForFinished(5000));
        QCOMPARE(encoder.exitCode(), 0);
        FileAssetBackend backend;
        AssetRequest request;
        request.uri = path;
        request.kind = QStringLiteral("metadata");
        AssetResult result = backend.fetch(request, {}, {});
        QVERIFY2(!result.error, qPrintable(result.error.message));
        QCOMPARE(result.metadata.value(QStringLiteral("mediaType")).toString(), QStringLiteral("video"));
        QCOMPARE(result.metadata.value(QStringLiteral("width")).toInt(), 32);
        request.kind = QStringLiteral("frame");
        request.size = QSize(32, 32);
        request.frameId = QStringLiteral("1");
        result = backend.fetch(request, {}, {});
        QVERIFY2(!result.error, qPrintable(result.error.message));
        QVERIFY(result.metadata.value(QStringLiteral("exactFrameIndex")).toBool());
        QImage frame;
        QVERIFY(frame.loadFromData(result.bytes, "PNG"));
        QCOMPARE(frame.size(), QSize(32, 16));
        request.kind = QStringLiteral("thumbnail");
        request.frameId.clear();
        result = backend.fetch(request, {}, {});
        QVERIFY2(!result.error, qPrintable(result.error.message));
        QVERIFY(!result.bytes.isEmpty());
    }
};

QTEST_MAIN(TstFileAssetBackend)
#include "tst_file_asset_backend.moc"
