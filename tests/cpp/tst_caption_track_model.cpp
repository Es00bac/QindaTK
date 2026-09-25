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

    // VID-06 completion check: split, merge and shift, then undo each.
    void splitMergeShiftAndUndo()
    {
        CaptionTrackModel model; // cue-001 [1200, 4300) and cue-002 [5100, 7900), lane 0
        QVERIFY(model.splitCue(QStringLiteral("cue-001"), QStringLiteral("2500")));
        QCOMPARE(model.cues().size(), 3);
        QCOMPARE(model.cues().at(0).outMs, qint64(2500));
        QCOMPARE(model.cues().at(0).text, QStringLiteral("A quiet signal"));
        QCOMPARE(model.cues().at(1).id, QStringLiteral("cue-003"));
        QCOMPARE(model.cues().at(1).inMs, qint64(2500));
        QCOMPARE(model.cues().at(1).outMs, qint64(4300));
        QCOMPARE(model.cues().at(1).text, QStringLiteral("crosses the harbor."));
        QCOMPARE(model.selectedCueId(), QStringLiteral("cue-003"));
        QVERIFY(!model.splitCue(QStringLiteral("cue-003"), QStringLiteral("2500"))); // on its boundary
        QCOMPARE(model.acknowledgement().value("status").toString(), QStringLiteral("rejected"));

        QVERIFY(model.mergeCues(QStringLiteral("cue-003"), QStringLiteral("cue-001")));
        QCOMPARE(model.cues().size(), 2);
        QCOMPARE(model.cues().at(0).id, QStringLiteral("cue-003"));
        QCOMPARE(model.cues().at(0).inMs, qint64(1200));
        QCOMPARE(model.cues().at(0).text, QStringLiteral("A quiet signal\ncrosses the harbor."));

        QVERIFY(model.shiftCues({}, QStringLiteral("-200")));
        QCOMPARE(model.cues().at(0).inMs, qint64(1000));
        QCOMPARE(model.cues().at(1).outMs, qint64(7700));
        QVERIFY(!model.shiftCues({QStringLiteral("cue-002")}, QStringLiteral("-1000"))); // would overlap cue-003
        QVERIFY(model.error().contains(QStringLiteral("overlap")));
        QVERIFY(!model.shiftCues({}, QStringLiteral("-1001"))); // before zero
        QVERIFY(model.shiftCues({QStringLiteral("cue-002")}, QStringLiteral("+300")));
        QCOMPARE(model.cues().at(1).inMs, qint64(5200));

        for (int i = 0; i < 4; ++i) QVERIFY(model.undo());
        QVERIFY(!model.undo());
        QCOMPARE(model.cues().size(), 2);
        QCOMPARE(model.cues().at(0).id, QStringLiteral("cue-001"));
        QCOMPARE(model.cues().at(0).inMs, qint64(1200));
        QCOMPARE(model.cues().at(0).outMs, qint64(4300));
        QCOMPARE(model.cues().at(0).text, QStringLiteral("A quiet signal crosses the harbor."));
        QCOMPARE(model.cues().at(1).inMs, qint64(5100));
    }

    // Multilingual WebVTT round trip, reading warnings and format capabilities.
    void webVttKeepsEveryLanguage()
    {
        const QString source = QStringLiteral(
            "WEBVTT\nLanguage: en-US\n\nopening\n00:00:00.000 --> 00:00:02.000\nHello &lt;friend&gt; &amp; co.\n\n"
            "japanese\n00:00:02.500 --> 00:00:04.000\n<lang ja-JP>こんにちは、港。</lang>\n\n"
            "arabic\n00:00:04.000 --> 00:00:04.500\n<lang ar>مرحبا بكم في الميناء الهادئ هذا المساء</lang>\n");
        CaptionTrackModel model;
        QVERIFY(model.importWebVtt(source));
        QCOMPARE(model.language(), QStringLiteral("en-US"));
        QCOMPARE(model.cues().size(), 3);
        QCOMPARE(model.cues().at(0).text, QStringLiteral("Hello <friend> & co."));
        QCOMPARE(model.cues().at(0).language, QStringLiteral("en-US"));
        QCOMPARE(model.cues().at(1).language, QStringLiteral("ja-JP"));
        QCOMPARE(model.cues().at(1).text, QStringLiteral("こんにちは、港。"));
        QCOMPARE(model.cues().at(2).language, QStringLiteral("ar"));

        const QString vtt = model.exportWebVtt();
        QVERIFY(vtt.startsWith(QStringLiteral("WEBVTT\nLanguage: en-US\n\n")));
        QVERIFY(vtt.contains(QStringLiteral("Hello &lt;friend&gt; &amp; co.")));
        QVERIFY(vtt.contains(QStringLiteral("<lang ja-JP>こんにちは、港。</lang>")));
        CaptionTrackModel reopened;
        QVERIFY(reopened.importWebVtt(vtt));
        QCOMPARE(reopened.language(), QStringLiteral("en-US"));
        QCOMPARE(reopened.cues().size(), model.cues().size());
        for (qsizetype i = 0; i < model.cues().size(); ++i) {
            QCOMPARE(reopened.cues().at(i).id, model.cues().at(i).id);
            QCOMPARE(reopened.cues().at(i).inMs, model.cues().at(i).inMs);
            QCOMPARE(reopened.cues().at(i).outMs, model.cues().at(i).outMs);
            QCOMPARE(reopened.cues().at(i).text, model.cues().at(i).text);
            QCOMPARE(reopened.cues().at(i).language, model.cues().at(i).language);
        }
        QVERIFY(reopened.importWebVtt(vtt, QStringLiteral("fr-FR"))); // an explicit language wins
        QCOMPARE(reopened.language(), QStringLiteral("fr-FR"));
        QCOMPARE(reopened.cues().at(0).language, QStringLiteral("fr-FR"));
        QCOMPARE(reopened.cues().at(1).language, QStringLiteral("ja-JP"));

        QVERIFY(!model.mergeCues(QStringLiteral("japanese"), QStringLiteral("arabic")));
        QVERIFY(model.error().contains(QStringLiteral("different languages")));
        QVERIFY(model.selectCue(QStringLiteral("arabic")));
        QStringList kinds;
        for (const QVariant &value : model.warnings())
            if (value.toMap().value("cueId").toString() == QLatin1String("arabic"))
                kinds << value.toMap().value("kind").toString();
        QCOMPARE(kinds, (QStringList{QStringLiteral("short"), QStringLiteral("reading-rate")}));
        QVERIFY(model.warningText().contains(QStringLiteral("833 ms minimum")));
        QVERIFY(model.addCue(QStringLiteral("1500"), QStringLiteral("2600"), QStringLiteral("Overlap")));
        QCOMPARE(model.cues().at(1).lane, 1);
        bool overlap = false;
        for (const QVariant &value : model.warnings())
            overlap = overlap || value.toMap().value("kind").toString() == QLatin1String("overlap");
        QVERIFY(overlap);

        QStringList formats;
        for (const QVariant &value : model.formats()) formats << value.toMap().value("id").toString();
        QCOMPARE(formats, (QStringList{QStringLiteral("json"), QStringLiteral("webvtt"), QStringLiteral("srt"),
                                       QStringLiteral("matroska-subrip"), QStringLiteral("burned-in")}));
    }

    // WP-072 audit: an import replaces the language with the cues, so its
    // undo must bring both back; otherwise the old cues export under the
    // imported language header and <lang> spans.
    void undoAfterImportRestoresTheLanguage()
    {
        CaptionTrackModel model;
        QVERIFY(model.importWebVtt(QStringLiteral(
            "WEBVTT\nLanguage: en-US\n\nhello\n00:00:00.000 --> 00:00:02.000\nHello\n\n"
            "bonjour\n00:00:02.000 --> 00:00:04.000\n<lang fr-FR>Bonjour</lang>\n")));
        const QString before = model.exportWebVtt();
        QVERIFY(model.shiftCues({}, QStringLiteral("100"))); // a plain edit keeps the language on undo
        QVERIFY(model.importSrt(QStringLiteral("1\n00:00:00,000 --> 00:00:01,000\nこんにちは\n"),
                                QStringLiteral("ja-JP")));
        QCOMPARE(model.language(), QStringLiteral("ja-JP"));
        QVERIFY(model.undo());
        QCOMPARE(model.language(), QStringLiteral("en-US"));
        QCOMPARE(model.cues().size(), 2);
        QVERIFY(model.exportWebVtt().startsWith(QStringLiteral("WEBVTT\nLanguage: en-US\n\n")));
        QVERIFY(model.exportWebVtt().contains(QStringLiteral("<lang fr-FR>Bonjour</lang>")));
        QVERIFY(!model.exportWebVtt().contains(QStringLiteral("<lang en-US>")));
        model.setLanguage(QStringLiteral("en-GB")); // not an undo step: undoing the shift keeps it
        QVERIFY(model.undo());
        QCOMPARE(model.language(), QStringLiteral("en-GB"));
        QCOMPARE(model.cues().at(0).inMs, qint64(0));
        QVERIFY(before.startsWith(QStringLiteral("WEBVTT\nLanguage: en-US")));
    }
};

QTEST_GUILESS_MAIN(CaptionTrackModelTest)
#include "tst_caption_track_model.moc"
