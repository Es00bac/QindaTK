// SPDX-License-Identifier: LGPL-3.0-or-later
#include "media_fixture.h"
#include "resource_resolver.h"
#include "timeline_media_provider.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;
using namespace MediaFixture;

// Decoded timeline peaks (EditTimeline audio clips drawn from the media the
// monitor plays) and the test signal's bar rotation, both added for the
// examples/sequence-edit-live demo.
class TstTimelineMediaDecoded final : public QObject {
    Q_OBJECT
private:
    static QString popSignal(const QString &path, int channels, int barRotation = 0)
    {
        MediaTestSignalSettings settings;
        settings.frameSize = QSize(128, 72);
        settings.frames = 50;
        settings.channels = channels;
        settings.popInterval = 25;
        settings.popPhase = 12; // pops at frames 12 and 37
        settings.barRotation = barRotation;
        return MediaExport::writeTestSignal(path, settings).error ? QString() : path;
    }
    static QString peaks(TimelineMediaProvider &provider, const QString &resourceId, qint64 fromFrame,
                         qint64 toFrame, int columns)
    {
        return provider.requestPeaks(QString(), resourceId, QString::number(fromFrame * kFrame25),
                                     QString::number(toFrame * kFrame25), QString::number(kTicks),
                                     QStringLiteral("1"), columns);
    }

private slots:
    void barRotationTellsSignalsApart()
    {
        MediaTestSignalSettings settings;
        settings.frameSize = QSize(140, 72);
        settings.popInterval = 0;
        const QImage plain = MediaExport::testSignalFrame(3, settings);
        settings.barRotation = 2;
        const QImage rotated = MediaExport::testSignalFrame(3, settings);
        QCOMPARE(plain.pixelColor(5, 5), QColor(191, 191, 191));
        QCOMPARE(rotated.pixelColor(5, 5), QColor(0, 191, 191)); // the third bar leads
        QCOMPARE(rotated.pixelColor(135, 5), QColor(191, 191, 0)); // the second bar trails
        QCOMPARE(MediaExport::readTestSignalCode(rotated), qint64(3)); // frame code untouched
        settings.barRotation = -5; // same as 2
        QCOMPARE(MediaExport::testSignalFrame(3, settings).pixelColor(5, 5), QColor(0, 191, 191));
    }

    void decodedPeaksFindThePops()
    {
        if (!MediaDecoder::available()) QSKIP("Built without the FFmpeg media backend");
        QTemporaryDir dir;
        const QString path = popSignal(dir.filePath(QStringLiteral("pops.mkv")), 2);
        QVERIFY(!path.isEmpty());
        TimelineMediaProvider provider;
        provider.setDecodedBackend(true);
        QVERIFY(provider.decodedBackend());
        QCOMPARE(provider.backendId(), QStringLiteral("qindatk.decoded-audio"));
        QSignalSpy ready(&provider, &TimelineMediaProvider::ready);
        // One column per frame over frames 0-49: only the pop frames sound.
        const QString id = peaks(provider, fileUri(path), 0, 50, 50);
        QTRY_COMPARE(ready.count(), 1);
        const QVariantList columns = provider.result(id).value(QStringLiteral("peaks")).toList();
        QCOMPARE(columns.size(), 50);
        for (int column = 0; column < 50; ++column) {
            const double peak = columns.at(column).toDouble();
            if (column == 12 || column == 37) QVERIFY2(peak > 0.45 && peak <= 0.5 + 1e-3, qPrintable(QString::number(peak)));
            else QVERIFY2(peak < 1e-3, qPrintable(QStringLiteral("column %1: %2").arg(column).arg(peak)));
        }
        // A source range starting mid-file maps to that file's own samples.
        const QString later = peaks(provider, fileUri(path), 30, 40, 10);
        QTRY_COMPARE(ready.count(), 2);
        const QVariantList tail = provider.result(later).value(QStringLiteral("peaks")).toList();
        QCOMPARE(tail.size(), 10);
        QVERIFY(tail.at(7).toDouble() > 0.45);
        QVERIFY(tail.at(6).toDouble() < 1e-3 && tail.at(8).toDouble() < 1e-3);
    }

    void decodedBackendRefusesWhatItCannotDraw()
    {
        if (!MediaDecoder::available()) QSKIP("Built without the FFmpeg media backend");
        QTemporaryDir dir;
        const QString silent = popSignal(dir.filePath(QStringLiteral("picture.mkv")), 0);
        QVERIFY(!silent.isEmpty());
        TimelineMediaProvider provider;
        provider.setDecodedBackend(true);
        QSignalSpy failed(&provider, &TimelineMediaProvider::failed);
        peaks(provider, fileUri(silent), 0, 10, 10);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(failed.at(0).at(2).toString().contains(QStringLiteral("no sound")));
        provider.requestFrames(QStringLiteral("v"), fileUri(silent), QStringLiteral("0"),
                               QString::number(kFrame25), QString::number(kTicks), QStringLiteral("1"), 2);
        QTRY_COMPARE(failed.count(), 2);
        QVERIFY(failed.at(1).at(2).toString().contains(QStringLiteral("not video frames")));
        // Switching to the reference backend clears the decoded flag.
        provider.setReferenceBackend(true);
        QVERIFY(!provider.decodedBackend());
    }

    void resolverMapsResourcesAndInvalidatesOnRelink()
    {
        if (!MediaDecoder::available()) QSKIP("Built without the FFmpeg media backend");
        QTemporaryDir dir;
        const QString loud = popSignal(dir.filePath(QStringLiteral("loud.mkv")), 2);
        const QString quiet = popSignal(dir.filePath(QStringLiteral("quiet.mkv")), 0);
        QVERIFY(!loud.isEmpty() && !quiet.isEmpty());
        ResourceResolver resolver;
        QVERIFY(resolver.registerResource(QStringLiteral("res-a"), fileUri(loud)));
        TimelineMediaProvider provider;
        provider.setDecodedBackend(true);
        provider.setResolver(&resolver);
        QCOMPARE(provider.resolver(), &resolver);
        QSignalSpy ready(&provider, &TimelineMediaProvider::ready);
        QSignalSpy failed(&provider, &TimelineMediaProvider::failed);
        QSignalSpy invalidated(&provider, &TimelineMediaProvider::resourceInvalidated);
        const QString first = peaks(provider, QStringLiteral("res-a"), 10, 15, 5);
        QTRY_COMPARE(ready.count(), 1);
        QVERIFY(provider.result(first).value(QStringLiteral("peaks")).toList().at(2).toDouble() > 0.45);
        QVERIFY(provider.cacheEntries() > 0);
        // The same ID now names a file without sound: the cache must not answer.
        QVERIFY(resolver.relink(QStringLiteral("res-a"), fileUri(quiet)));
        QVERIFY(invalidated.count() >= 1);
        peaks(provider, QStringLiteral("res-a"), 10, 15, 5);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(failed.at(0).at(2).toString().contains(QStringLiteral("no sound")));
        // An unknown resource is offline, never read from a guessed path.
        peaks(provider, QStringLiteral("res-unknown"), 0, 5, 5);
        QTRY_COMPARE(failed.count(), 2);
        QVERIFY(failed.at(1).at(2).toString().contains(QStringLiteral("offline")));
    }
};

QTEST_MAIN(TstTimelineMediaDecoded)
#include "tst_timeline_media_decoded.moc"
