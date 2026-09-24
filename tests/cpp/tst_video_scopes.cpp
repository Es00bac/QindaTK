// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtTest>
#include <QColorSpace>
#include "video_scopes_model.h"

class VideoScopesTest final : public QObject
{
    Q_OBJECT
private slots:
    void knownPixelsAndConventions()
    {
        VideoScopesModel model;
        QImage image(2, 1, QImage::Format_RGBA8888);
        image.setPixelColor(0, 0, Qt::black);
        image.setPixelColor(1, 0, Qt::white);
        image.setColorSpace(QColorSpace(QColorSpace::SRgb));
        const auto result = model.analyzeFrame(image, QStringLiteral("frame-00042"));
        QVERIFY(result.value("valid").toBool());
        QCOMPARE(result.value("frameId").toString(), QStringLiteral("frame-00042"));
        QCOMPARE(result.value("sampleCount").toInt(), 2);
        const auto histogram = result.value("histogram").toList();
        QCOMPARE(histogram.size(), 128);
        QCOMPARE(histogram.at(0).toInt(), 1);
        QCOMPARE(histogram.at(127).toInt(), 1);
        const auto waveform = result.value("waveform").toList();
        QCOMPARE(waveform.size(), 128 * 64);
        QCOMPARE(waveform.at(0 * 64 + 63).toInt(), 1); // black at first image column
        QCOMPARE(waveform.at(64 * 64 + 0).toInt(), 1);  // white at second image column
        QCOMPARE(result.value("rangeConvention").toString(),
                 QStringLiteral("full-range normalized encoded RGB [0,1]"));
        QVERIFY(result.value("vectorscopeConvention").toString().contains("Cb horizontal"));
    }

    void fixtureAndBounds()
    {
        VideoScopesModel model;
        const auto bars = model.analyzeColorBarsFixture(QStringLiteral("bars-A"));
        QVERIFY(bars.value("valid").toBool());
        QCOMPARE(bars.value("frameId").toString(), QStringLiteral("bars-A"));
        QCOMPARE(bars.value("sampleCount").toInt(), 256 * 64);
        QImage tooLarge(4097, 4097, QImage::Format_RGB32);
        QVERIFY(!model.analyzeFrame(tooLarge, QStringLiteral("large")).value("valid").toBool());
        QVERIFY(model.error().contains("16-megapixel"));
    }

    // WP-072 (docs/video-scopes.md): stated conventions give known codes.
    void conventionsTargetsAndSignalSpace()
    {
        VideoScopesModel model;
        model.setSignalRange(QStringLiteral("limited"));
        QImage red(8, 4, QImage::Format_RGB32);
        red.fill(qRgb(255, 0, 0));
        red.setColorSpace(QColorSpace(QColorSpace::SRgb));
        auto snapshot = model.analyzeFrame(red, QStringLiteral("red"));
        QVERIFY(snapshot.value("valid").toBool());
        const auto levels = snapshot.value("levels").toMap();
        QCOMPARE(levels.value("yMin").toInt(), 63);   // Rec.709 limited 100% red
        QCOMPARE(levels.value("cbMin").toInt(), 102);
        QCOMPARE(levels.value("crMax").toInt(), 240);
        QCOMPARE(levels.value("rMax").toInt(), 235);
        QCOMPARE(levels.value("gMax").toInt(), 16);
        QCOMPARE(snapshot.value("codeScale").toMap().value("black").toInt(), 16);
        QVERIFY(snapshot.value("rangeConvention").toString().startsWith(QStringLiteral("limited-range")));
        QVariantMap target;
        for (const QVariant &value : snapshot.value("vectorTargets").toList())
            if (value.toMap().value("id").toString() == QLatin1String("R100")) target = value.toMap();
        QCOMPARE(target.value("cb").toInt(), 102);
        QCOMPARE(target.value("cr").toInt(), 240);
        const int side = snapshot.value("vectorscopeSide").toInt();
        const int bin = qRound(target.value("y").toDouble() * (side - 1)) * side
            + qRound(target.value("x").toDouble() * (side - 1));
        QCOMPARE(snapshot.value("vectorscope").toList().at(bin).toInt(), 32); // every pixel on the target
        const auto parade = snapshot.value("parade").toList();
        QCOMPARE(parade.size(), 3 * 128 * 64);
        QCOMPARE(snapshot.value("histogramRgb").toList().size(), 3 * 128);

        // "signal" measures delivered values unchanged and names the transfer.
        model.setSignalRange(QStringLiteral("full"));
        model.setAnalysisSpace(QStringLiteral("signal"));
        QImage pq(4, 1, QImage::Format_RGB32);
        pq.fill(qRgb(200, 100, 50));
        pq.setColorSpace(QColorSpace(QColorSpace::Primaries::Bt2020, QColorSpace::TransferFunction::St2084));
        snapshot = model.analyzeFrame(pq, QStringLiteral("pq"));
        QCOMPARE(snapshot.value("transfer").toString(), QStringLiteral("pq"));
        QCOMPARE(snapshot.value("profileConvertedToSrgb").toBool(), false);
        QCOMPARE(snapshot.value("levels").toMap().value("rMax").toInt(), 200);
        QCOMPARE(snapshot.value("levels").toMap().value("bMin").toInt(), 50);

        // Unknown names are refused and leave the convention unchanged.
        model.setMatrix(QStringLiteral("bt2100"));
        QVERIFY(model.error().contains(QStringLiteral("bt2100")));
        QCOMPARE(model.matrix(), QStringLiteral("bt709"));
    }
};

QTEST_GUILESS_MAIN(VideoScopesTest)
#include "tst_video_scopes.moc"
