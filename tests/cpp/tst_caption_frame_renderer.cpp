// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/caption_frame_renderer.h"
#include "../../src/core/caption_track_model.h"

#include <QFileInfo>
#include <QImageReader>
#include <QTemporaryDir>
#include <QtTest>

class CaptionFrameRendererTest : public QObject {
    Q_OBJECT
private slots:
    void honorsSelectionOverlapAndHalfOpenTimeBoundaries()
    {
        CaptionTrackModel track;
        QVERIFY(track.loadJson(QStringLiteral(R"JSON({"version":1,"trackId":"test-track","language":"en","cues":[
            {"id":"cue-first","inMs":"1000","outMs":"2000","text":"First cue","language":"en","styleId":"default","lane":0},
            {"id":"cue-second","inMs":"1500","outMs":"2500","text":"Deuxième cue","language":"fr","styleId":"emphasis","lane":1}]})JSON")));
        const QString firstId = QStringLiteral("cue-first");
        const QString secondId = QStringLiteral("cue-second");
        QVERIFY(track.selectCue(firstId));
        QCOMPARE(track.selectedCueId(), firstId);
        const QVariantList cues = track.editorCues();
        QCOMPARE(cues.size(), 2);
        QCOMPARE(cues.at(0).toMap().value(QStringLiteral("lane")).toInt(), 0);
        QCOMPARE(cues.at(1).toMap().value(QStringLiteral("lane")).toInt(), 1);

        QImage source(640, 360, QImage::Format_ARGB32_Premultiplied);
        source.fill(QColor(30, 40, 50));
        CaptionFrameRenderer renderer;
        auto result = renderer.renderFrame(source, &track, QStringLiteral("999"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QVERIFY(renderer.activeCueIds().isEmpty());
        QCOMPARE(QImage::fromData(QByteArray::fromBase64(renderer.imageDataUrl().mid(22).toLatin1())).pixelColor(0, 0),
                 QColor(30, 40, 50));

        result = renderer.renderFrame(source, &track, QStringLiteral("1000"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(renderer.activeCueIds(), QStringList{firstId});
        const QImage burned = QImage::fromData(QByteArray::fromBase64(renderer.imageDataUrl().mid(22).toLatin1()));
        QVERIFY(!burned.isNull());
        int changedPixels = 0;
        for (int y = 0; y < source.height(); ++y)
            for (int x = 0; x < source.width(); ++x)
                if (burned.pixel(x, y) != source.pixel(x, y)) ++changedPixels;
        QVERIFY(changedPixels > 0);
        result = renderer.renderFrame(source, &track, QStringLiteral("1500"));
        QCOMPARE(renderer.activeCueIds(), (QStringList{firstId, secondId}));
        result = renderer.renderFrame(source, &track, QStringLiteral("2000"));
        QCOMPARE(renderer.activeCueIds(), QStringList{secondId});
        result = renderer.renderFrame(source, &track, QStringLiteral("2500"));
        QVERIFY(renderer.activeCueIds().isEmpty());
        QVERIFY(!renderer.renderFrame(source, &track, QStringLiteral("2500.0")).value(QStringLiteral("ok")).toBool());

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString pngPath = directory.filePath(QStringLiteral("burned-frame.png"));
        QVERIFY(renderer.saveRenderedFrame(pngPath));
        QImageReader reader(pngPath);
        const QImage saved = reader.read();
        QVERIFY2(!saved.isNull(), qPrintable(reader.errorString()));
        QCOMPARE(saved.size(), source.size());
        QVERIFY(QFileInfo(pngPath).size() > 0);
    }
};

QTEST_MAIN(CaptionFrameRendererTest)
#include "tst_caption_frame_renderer.moc"
