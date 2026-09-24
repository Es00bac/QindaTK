// SPDX-License-Identifier: LGPL-3.0-or-later
#include "caption_track_model.h"

#include <QTemporaryDir>
#include <QtTest>

class CaptionTrackModelTest final : public QObject
{
    Q_OBJECT
private slots:
    void formatsExactEditPreviewOverlapUndoAndPersistence()
    {
        CaptionTrackModel model;
        const QString vtt = QStringLiteral("WEBVTT\n\nopening\n00:00:01.200 --> 00:00:03.400\nHello, harbor.\n\nreply\n00:00:03.500 --> 00:00:05.000\nSecond line\n");
        QVERIFY(model.importWebVtt(vtt, QStringLiteral("en-US")));
        QCOMPARE(model.editorCues().size(), 2);
        QCOMPARE(model.editorCues().at(0).toMap().value("inTime").toString(), QStringLiteral("1200"));
        QCOMPARE(model.editorCues().at(0).toMap().value("outTime").toString(), QStringLiteral("3400"));
        QCOMPARE(model.editorCues().at(0).toMap().value("lane").toInt(), 0);
        QCOMPARE(model.editorCues().at(1).toMap().value("lane").toInt(), 0);
        QVERIFY(model.exportWebVtt().contains(QStringLiteral("00:00:01.200 --> 00:00:03.400")));

        const QString request = QStringLiteral("edit-1");
        QVERIFY(!model.previewCueEdit(request, QStringLiteral("reply"), QStringLiteral("move"),
                                      QStringLiteral("2000"), QStringLiteral("4500"), QStringLiteral("Second line"), QStringLiteral("default")));
        QCOMPARE(model.acknowledgement().value("status").toString(), QStringLiteral("rejected"));
        QVERIFY(model.previewCueEdit(request, QStringLiteral("reply"), QStringLiteral("move"),
                                     QStringLiteral("5500"), QStringLiteral("7000"), QStringLiteral("Second line"), QStringLiteral("emphasis")));
        QCOMPARE(model.editorCues().at(1).toMap().value("inTime").toString(), QStringLiteral("5500"));
        QVERIFY(model.cancelCueEdit(QStringLiteral("cancel-1"), QStringLiteral("reply")));
        QCOMPARE(model.editorCues().at(1).toMap().value("inTime").toString(), QStringLiteral("3500"));
        QVERIFY(model.commitCueEdit(QStringLiteral("commit-1"), QStringLiteral("reply"), QStringLiteral("move"),
                                    QStringLiteral("5500"), QStringLiteral("7000"), QStringLiteral("Second line"), QStringLiteral("emphasis")));
        QCOMPARE(model.editorCues().at(1).toMap().value("inTime").toString(), QStringLiteral("5500"));
        QVERIFY(model.undo());
        QCOMPARE(model.editorCues().at(1).toMap().value("inTime").toString(), QStringLiteral("3500"));

        const QString srt = model.exportSrt();
        QVERIFY(srt.contains(QStringLiteral("00:00:01,200 --> 00:00:03,400")));
        QVERIFY(model.importSrt(srt, QStringLiteral("fr-FR")));
        QCOMPARE(model.selectedCueLanguage(), QStringLiteral("fr-FR"));
        QVERIFY(model.toJson().contains(QStringLiteral("\"inMs\": \"1200\"")));
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("track.json"));
        QVERIFY(model.saveToFile(path));
        QVERIFY(model.addCue(QStringLiteral("8000"), QStringLiteral("9000"), QStringLiteral("Tail")));
        QCOMPARE(model.editorCues().size(), 3);
        QVERIFY(model.loadFromFile(path));
        QCOMPARE(model.editorCues().size(), 2);
        QCOMPARE(model.trackId(), QStringLiteral("captions-main"));
    }
};

QTEST_GUILESS_MAIN(CaptionTrackModelTest)
#include "tst_caption_track_model.moc"
