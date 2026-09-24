// SPDX-License-Identifier: LGPL-3.0-or-later
#include "../../src/core/caption_sequence_exporter.h"

#include <QFile>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

class CaptionSequenceExporterTest : public QObject {
    Q_OBJECT
private slots:
    void writesOrderedFramesAndHalfOpenCueTimes()
    {
        CaptionTrackModel track;
        QVERIFY(track.loadJson(QStringLiteral(R"JSON({"version":1,"trackId":"seq","language":"en","cues":[
            {"id":"first","inMs":"1000","outMs":"2000","text":"First","language":"en","styleId":"default","lane":0},
            {"id":"second","inMs":"1500","outMs":"2500","text":"Second","language":"en","styleId":"emphasis","lane":1}]})JSON")));
        QImage before(640, 360, QImage::Format_ARGB32_Premultiplied);
        before.fill(QColor(22, 37, 51));
        QImage overlap(640, 360, QImage::Format_ARGB32_Premultiplied);
        overlap.fill(QColor(22, 37, 51));
        QImage atEnd(640, 360, QImage::Format_ARGB32_Premultiplied);
        atEnd.fill(QColor(22, 37, 51));
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString target = temp.filePath(QStringLiteral("sequence"));
        CaptionSequenceExporter exporter;
        const QVariantMap result = exporter.exportFrames(
            {QVariant::fromValue(before), QVariant::fromValue(overlap), QVariant::fromValue(atEnd)},
            &track, {QStringLiteral("999"), QStringLiteral("1500"), QStringLiteral("2500")}, target);
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(result.value(QStringLiteral("error")).toString()));
        QCOMPARE(exporter.frameCount(), 3);
        QCOMPARE(exporter.framePaths().size(), 3);
        QJsonParseError parseError;
        QFile manifestFile(exporter.manifestPath());
        QVERIFY(manifestFile.open(QIODevice::ReadOnly));
        const QJsonDocument manifest = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);
        QCOMPARE(parseError.error, QJsonParseError::NoError);
        const QJsonArray rows = manifest.object().value(QStringLiteral("frames")).toArray();
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows.at(0).toObject().value(QStringLiteral("timeMs")).toString(), QStringLiteral("999"));
        QCOMPARE(rows.at(1).toObject().value(QStringLiteral("timeMs")).toString(), QStringLiteral("1500"));
        QCOMPARE(rows.at(2).toObject().value(QStringLiteral("timeMs")).toString(), QStringLiteral("2500"));
        QCOMPARE(rows.at(0).toObject().value(QStringLiteral("activeCueIds")).toArray().size(), 0);
        QCOMPARE(rows.at(1).toObject().value(QStringLiteral("activeCueIds")).toArray().size(), 2);
        QCOMPARE(rows.at(2).toObject().value(QStringLiteral("activeCueIds")).toArray().size(), 0);
        for (int index = 0; index < exporter.framePaths().size(); ++index) {
            QImageReader reader(exporter.framePaths().at(index));
            QVERIFY2(!reader.read().isNull(), qPrintable(reader.errorString()));
        }
        const QVariantMap duplicate = exporter.exportFrames({QVariant::fromValue(before)}, &track,
            {QStringLiteral("1000")}, target);
        QVERIFY(!duplicate.value(QStringLiteral("ok")).toBool());
        QCOMPARE(exporter.frameCount(), 0);
    }
};

QTEST_MAIN(CaptionSequenceExporterTest)
#include "tst_caption_sequence_exporter.moc"
