// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-067 notation layout: a dense two-voice measure (shared columns, stems by
// voice, beams by beat, rest positions), tuplet brackets and ties across a
// system break, pagination with forced breaks and justification, the
// ScoreView snapshot and reading order, part extraction, PDF and image
// output from the same layout, and the view's canvas matching the PDF/image
// painter pixel for pixel (docs/notation.md#layout).
#include "score_model.h"
#include "score_paint.h"

#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickView>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {

bool ok(const QVariantMap &result)
{
    return result.value(QStringLiteral("ok")).toBool();
}

struct Dense {
    ScoreModel model;
    QString part, staff, voice1, voice2, measure;
    QStringList eighths;
    QString a4;
    Dense()
    {
        const QVariantMap made = model.newScore(QStringLiteral("Dense"), 1, QStringLiteral("4/4"), 0);
        part = made.value(QStringLiteral("partId")).toString();
        staff = made.value(QStringLiteral("staffId")).toString();
        voice1 = made.value(QStringLiteral("voiceId")).toString();
        measure = model.measures().value(0).toMap().value(QStringLiteral("id")).toString();
        const QStringList pitches{QStringLiteral("C5"), QStringLiteral("D5"), QStringLiteral("E5"), QStringLiteral("F5")};
        for (int i = 0; i < 4; ++i)
            eighths << model.insertNote(part, staff, voice1, measure, QString::number(i * 480), pitches[i], QStringLiteral("eighth"))
                           .value(QStringLiteral("eventId")).toString();
        voice2 = model.addVoice(staff).value(QStringLiteral("voiceId")).toString();
        a4 = model.insertNote(part, staff, voice2, measure, QStringLiteral("0"), QStringLiteral("A4"), QStringLiteral("half"))
                 .value(QStringLiteral("eventId")).toString();
    }
};

const ScoreEventLayout *findEvent(const ScoreStaffLayout &staff, const QString &id)
{
    for (const ScoreMeasureLayout &measure : staff.measures)
        for (const ScoreVoiceLayout &voice : measure.voices)
            for (const ScoreEventLayout &event : voice.events)
                if (event.id == QUuid::fromString(id)) return &event;
    return nullptr;
}

} // namespace

class TestScoreLayout : public QObject {
    Q_OBJECT
private slots:
    void denseTwoVoiceMeasure()
    {
        Dense d;
        QVERIFY(!d.a4.isEmpty());
        const ScoreLayout layout = d.model.layout();
        QCOMPARE(layout.pages.size(), 1);
        const ScoreStaffLayout &staff = layout.pages[0].systems[0].staves[0];
        const ScoreMeasureLayout &measure = staff.measures[0];
        QList<qint64> ticks;
        for (int c = 0; c < measure.columns.size(); ++c) {
            ticks << measure.columns[c].first;
            if (c > 0) QVERIFY(measure.columns[c].second > measure.columns[c - 1].second);
        }
        QCOMPARE(ticks, (QList<qint64>{0, 480, 960, 1440, 1920}));
        const ScoreEventLayout *c5 = findEvent(staff, d.eighths[0]);
        const ScoreEventLayout *d5 = findEvent(staff, d.eighths[1]);
        const ScoreEventLayout *a4 = findEvent(staff, d.a4);
        QVERIFY(c5 && d5 && a4);
        QCOMPARE(a4->x, c5->x); // one column for both voices
        QCOMPARE(c5->step, 5.0);  // C5: two steps above the middle line
        QCOMPARE(a4->step, 3.0);
        QCOMPARE(c5->stem, 1);    // voice 1 up, voice 2 down in a two-voice measure
        QCOMPARE(a4->stem, -1);
        QCOMPARE(a4->stemEnd, -4.0);
        // Eighths beam by beat: two groups, stems end on the beam line.
        QCOMPARE(staff.beams.size(), 2);
        QVERIFY(c5->beamed && d5->beamed);
        QCOMPARE(c5->flags, 0);
        QCOMPARE(c5->stemEnd, 12.0);
        QCOMPARE(d5->stemEnd, 13.0);
        QCOMPARE(staff.beams[0].s0, 12.0);
        QCOMPARE(staff.beams[0].s1, 13.0);
        // The voice-1 half rest sits high in a two-voice measure.
        const ScoreEventLayout &rest = measure.voices[0].events.back();
        QVERIFY(rest.rest);
        QCOMPARE(rest.step, 6.0);
    }

    void tupletsAndTiesAcrossASystemBreak()
    {
        ScoreModel model;
        const QVariantMap made = model.newScore(QStringLiteral("Ties"), 2, QStringLiteral("4/4"), 0);
        const QString part = made.value(QStringLiteral("partId")).toString();
        const QString staffId = made.value(QStringLiteral("staffId")).toString();
        const QString voice = made.value(QStringLiteral("voiceId")).toString();
        const QString m0 = model.measures().value(0).toMap().value(QStringLiteral("id")).toString();
        const QString m1 = model.measures().value(1).toMap().value(QStringLiteral("id")).toString();
        for (int i = 0; i < 3; ++i)
            QVERIFY(ok(model.insertNote(part, staffId, voice, m0, QString::number(i * 320), QStringLiteral("G4"),
                                        QStringLiteral("triplet-eighth"))));
        const QString held = model.insertNote(part, staffId, voice, m0, QStringLiteral("2880"), QStringLiteral("B5"),
                                              QStringLiteral("quarter")).value(QStringLiteral("eventId")).toString();
        QVERIFY(ok(model.insertNote(part, staffId, voice, m1, QStringLiteral("3840"), QStringLiteral("B5"), QStringLiteral("half"))));
        QVERIFY(ok(model.editEvent(held, QStringLiteral("tie"))));
        QVERIFY(ok(model.setBreak(m0, QStringLiteral("system"))));
        const ScoreLayout layout = model.layout();
        QCOMPARE(layout.pages[0].systems.size(), 2);
        const ScoreStaffLayout &first = layout.pages[0].systems[0].staves[0];
        const ScoreStaffLayout &second = layout.pages[0].systems[1].staves[0];
        QCOMPARE(first.tuplets.size(), 1);
        QCOMPARE(first.tuplets[0].label, QStringLiteral("3"));
        // The tie leaves the first system at its end and enters the second at its start.
        QCOMPARE(first.ties.size(), 1);
        QCOMPARE(second.ties.size(), 1);
        QCOMPARE(first.ties[0].x1, first.x1 - 0.3 * layout.staffSpace);
        QCOMPARE(second.ties[0].x0, second.measures.front().contentX - 0.5 * layout.staffSpace);
        // B5 needs a ledger-free head above the staff: step 11 (ledger lines start at 10).
        QCOMPARE(findEvent(first, held)->step, 11.0);
    }

    void pagesFollowBreaksAndJustify()
    {
        ScoreModel model;
        model.newScore(QStringLiteral("Long"), 240, QStringLiteral("4/4"), 0);
        QStringList measures;
        for (const QVariant &m : model.measures()) measures << m.toMap().value(QStringLiteral("id")).toString();
        QVERIFY(ok(model.setBreak(measures[3], QStringLiteral("system"))));
        QVERIFY(ok(model.setBreak(measures[10], QStringLiteral("page"))));
        const ScoreLayout layout = model.layout();
        QVERIFY(layout.pages.size() >= 3);
        QCOMPARE(model.pageCount(), int(layout.pages.size()));
        const double right = layout.pageSize.width() - model.margin();
        const double bottom = layout.pageSize.height() - model.margin();
        int expected = 0, systems = 0, total = 0;
        bool sawSystemBreak = false;
        for (const ScorePageLayout &page : layout.pages) total += int(page.systems.size());
        for (int p = 0; p < layout.pages.size(); ++p)
            for (int s = 0; s < layout.pages[p].systems.size(); ++s) {
                const ScoreSystemLayout &system = layout.pages[p].systems[s];
                QCOMPARE(system.firstMeasure, expected); // contiguous, every measure once
                expected = system.lastMeasure + 1;
                QVERIFY(system.bottom <= bottom);
                sawSystemBreak = sawSystemBreak || system.lastMeasure == 3;
                if (system.firstMeasure == 11) {
                    QCOMPARE(s, 0); // the page break starts a new page
                    QVERIFY(p > 0);
                }
                if (++systems < total) QVERIFY(qAbs(system.staves[0].x1 - right) < 1e-6); // justified
            }
        QCOMPARE(expected, 240);
        QVERIFY(sawSystemBreak);
        QVERIFY(model.layoutSnapshot(0).contains(QStringLiteral("title")));
        QVERIFY(!model.layoutSnapshot(1).contains(QStringLiteral("title")));
        QVERIFY(!model.layoutSnapshot(int(layout.pages.size())).value(QStringLiteral("available")).toBool());
    }

    void snapshotReadingOrderAndLocate()
    {
        Dense d;
        const QVariantMap snapshot = d.model.layoutSnapshot(0);
        QVERIFY(snapshot.value(QStringLiteral("available")).toBool());
        QCOMPARE(snapshot.value(QStringLiteral("scoreId")).toString(), d.model.scoreId());
        const QVariantMap staff = snapshot.value(QStringLiteral("systems")).toList().value(0).toMap()
                                      .value(QStringLiteral("staves")).toList().value(0).toMap();
        QCOMPARE(staff.value(QStringLiteral("lineGap")).toDouble(), d.model.staffSpace() / d.model.pageHeight());
        const QVariantMap measure = staff.value(QStringLiteral("measures")).toList().value(0).toMap();
        QStringList entry;
        for (const QVariant &position : measure.value(QStringLiteral("entryPositions")).toList())
            entry << position.toMap().value(QStringLiteral("ticks")).toString();
        QCOMPARE(entry, (QStringList{QStringLiteral("0"), QStringLiteral("480"), QStringLiteral("960"), QStringLiteral("1440"),
                                     QStringLiteral("1920"), QStringLiteral("2880")}));
        const QVariantMap first = measure.value(QStringLiteral("voices")).toList().value(0).toMap()
                                      .value(QStringLiteral("events")).toList().value(0).toMap();
        QCOMPARE(first.value(QStringLiteral("label")).toString(), QStringLiteral("Measure 1, beat 1, voice 1: eighth note C 5"));
        QCOMPARE(first.value(QStringLiteral("stemDirection")).toString(), QStringLiteral("up"));
        QCOMPARE(first.value(QStringLiteral("heads")).toList().size(), 1);

        const QVariantList order = d.model.readingOrder();
        QCOMPARE(order.size(), 6);
        QCOMPARE(order.first().toMap().value(QStringLiteral("eventId")).toString(), d.eighths[0]);
        QCOMPARE(order.last().toMap().value(QStringLiteral("eventId")).toString(), d.a4);
        QCOMPARE(order.last().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Measure 1, beat 1, voice 2: half note A 4"));

        const QVariantMap place = d.model.locate(QStringLiteral("960"));
        QVERIFY(ok(place));
        const ScoreLayout layout = d.model.layout();
        const double x = layout.pages[0].systems[0].staves[0].measures[0].columns[2].second / layout.pageSize.width();
        QCOMPARE(place.value(QStringLiteral("x")).toDouble(), x);
        QVERIFY(!ok(d.model.locate(QStringLiteral("3840"))));
    }

    void partsExtractToTheirOwnLayoutAndFile()
    {
        Dense d;
        const QString cello = d.model.addPart(QStringLiteral("Cello"), {QStringLiteral("bass")}, 2).value(QStringLiteral("partId")).toString();
        QCOMPARE(d.model.layout().pages[0].systems[0].staves.size(), 2);
        const QVariantMap options{{QStringLiteral("parts"), QVariantList{cello}}};
        const ScoreLayout part = d.model.layout(options);
        QCOMPARE(part.pages[0].systems[0].staves.size(), 1);
        QCOMPARE(part.pages[0].systems[0].staves[0].partId, QUuid::fromString(cello));
        QCOMPARE(d.model.readingOrder(options).size(), 1); // the cello's whole rest
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("cello.musicxml"));
        QVERIFY(ok(d.model.exportMusicXml(file, options)));
        ScoreModel extracted;
        QVERIFY(ok(extracted.importMusicXml(file)));
        QCOMPARE(extracted.parts().size(), 1);
        QCOMPARE(extracted.parts().value(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Cello"));
    }

    void pdfAndImagesComeFromTheLayout()
    {
        Dense d;
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("score.pdf"));
        const QVariantMap written = d.model.exportPdf(file);
        QVERIFY(ok(written));
        QCOMPARE(written.value(QStringLiteral("pages")).toInt(), 1);
        QFile pdf(file);
        QVERIFY(pdf.open(QIODevice::ReadOnly));
        QVERIFY(pdf.read(5) == QByteArrayLiteral("%PDF-"));

        const double zoom = 2.0;
        const QImage image = d.model.renderPageImage(0, zoom);
        QCOMPARE(image.size(), QSize(1190, 1684));
        const ScoreLayout layout = d.model.layout();
        const ScoreStaffLayout &staff = layout.pages[0].systems[0].staves[0];
        const ScoreEventLayout *c5 = findEvent(staff, d.eighths[0]);
        const double y = staff.top + (8.0 - c5->step) * layout.staffSpace / 2.0;
        QVERIFY(qGray(image.pixel(qRound(c5->x * zoom), qRound(y * zoom))) < 128); // a filled head
        QCOMPARE(qGray(image.pixel(4, 4)), 255);                                    // blank margin
    }

    void viewCanvasPaintsWhatThePageOutputPaints()
    {
        // ScoreView's canvas (ScoreCanvasItem) and PDF/image output share
        // Score::paintSnapshot; with the output's colours the pixels match.
        Dense d;
        QVERIFY(ok(d.model.editEvent(d.eighths[0], QStringLiteral("articulation"), QStringLiteral("accent"))));
        QVERIFY(ok(d.model.editEvent(d.eighths[0], QStringLiteral("dynamic"), QStringLiteral("mf"))));
        const QImage page = d.model.renderPageImage(0, 1.0);
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        view.resize(595, 842);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        QQmlComponent component(view.engine());
        component.setData(QByteArrayLiteral("import QtQuick\nimport QindaTK as Tk\n"
                                            "Tk.ScoreCanvasItem { width: 595; height: 842; surfaceColor: \"white\";"
                                            " inkColor: \"black\"; mutedColor: \"black\"; borderColor: \"black\";"
                                            " accentColor: \"black\" }"),
                          QUrl(QStringLiteral("inline.qml")));
        QScopedPointer<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(root.data());
        QVERIFY(item);
        item->setProperty("snapshot", d.model.layoutSnapshot(0));
        item->setParentItem(view.contentItem());
        const auto grab = item->grabToImage();
        QVERIFY(grab);
        QSignalSpy ready(grab.data(), &QQuickItemGrabResult::ready);
        QVERIFY(!grab->image().isNull() || ready.wait(5000));
        const QImage shown = grab->image().convertToFormat(QImage::Format_ARGB32).scaled(page.size());
        QCOMPARE(shown.size(), page.size());
        int ink = 0, differing = 0;
        for (int y = 0; y < page.height(); ++y)
            for (int x = 0; x < page.width(); ++x) {
                const int a = qGray(page.pixel(x, y)), b = qGray(shown.pixel(x, y));
                if (a < 128) ++ink;
                if (std::abs(a - b) > 96) ++differing;
            }
        QVERIFY2(ink > 800, qPrintable(QString::number(ink))); // a real page, not two blanks
        QVERIFY2(differing * 200 < ink, qPrintable(QStringLiteral("%1 of %2").arg(differing).arg(ink)));
    }
};

QTEST_MAIN(TestScoreLayout)
#include "tst_score_layout.moc"
