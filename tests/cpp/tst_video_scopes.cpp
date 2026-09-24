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
};

QTEST_GUILESS_MAIN(VideoScopesTest)
#include "tst_video_scopes.moc"
