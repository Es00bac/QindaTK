// SPDX-License-Identifier: LGPL-3.0-or-later
// Reference fields (WP-052, D-115): captions number in document order,
// cross-references and citations follow their targets through inserts,
// deletes and moves in the same undo step, report broken targets instead of
// pointing elsewhere, stay atomic under typing, and persist (D-117).
#include "rich_text_codec.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QtTest>

using namespace QindaTK;
using namespace QindaTK::RichText;

namespace {

QString sid(const QUuid &id) { return id.toString(QUuid::WithoutBraces); }

QStringList texts(const RichTextDocument &doc)
{
    QStringList list;
    for (const Paragraph *p : doc.paragraphsInOrder()) list.append(p->text);
    return list;
}

QString pid(const RichTextDocument &doc, int index) { return sid(doc.paragraphsInOrder().at(index)->id); }

QVariantMap reference(const RichTextDocument &doc, const QString &id)
{
    for (const QVariant &value : doc.referenceSnapshots())
        if (value.toMap().value(QStringLiteral("id")).toString() == id) return value.toMap();
    return {};
}

QString status(const RichTextDocument &doc, const QString &id)
{
    return reference(doc, id).value(QStringLiteral("status")).toString();
}

} // namespace

class RichTextReferencesTest : public QObject {
    Q_OBJECT
private slots:
    void captionsRenumberInTheSameUndoStep()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Intro\n: alpha\nMiddle\n: beta\nSee "));
        const QString figA = doc.insertReference(pid(doc, 1), 0, QStringLiteral("caption"), QStringLiteral("sequence:Figure"));
        const QString figB = doc.insertReference(pid(doc, 3), 0, QStringLiteral("caption"), QStringLiteral("Figure"));
        const QString xref = doc.insertReference(pid(doc, 4), 4, QStringLiteral("cross-reference"), figB);
        QVERIFY(!figA.isEmpty() && !figB.isEmpty() && !xref.isEmpty());
        QCOMPARE(texts(doc), (QStringList{QStringLiteral("Intro"), QStringLiteral("Figure 1: alpha"), QStringLiteral("Middle"),
                                          QStringLiteral("Figure 2: beta"), QStringLiteral("See Figure 2")}));
        QCOMPARE(status(doc, xref), QStringLiteral("resolved"));
        QCOMPARE(reference(doc, xref).value(QStringLiteral("targetId")).toString(), figB);
        QCOMPARE(reference(doc, figA).value(QStringLiteral("targetId")).toString(), QStringLiteral("sequence:Figure"));
        QCOMPARE(reference(doc, figB).value(QStringLiteral("number")).toInt(), 2);

        // A new caption in front renumbers every later caption and reference.
        const QByteArray before = RichTextCodec::encode(doc);
        const QString figC = doc.insertReference(pid(doc, 0), 0, QStringLiteral("caption"), QStringLiteral("Figure"));
        QVERIFY(!figC.isEmpty());
        const QStringList renumbered{QStringLiteral("Figure 1Intro"), QStringLiteral("Figure 2: alpha"), QStringLiteral("Middle"),
                                     QStringLiteral("Figure 3: beta"), QStringLiteral("See Figure 3")};
        QCOMPARE(texts(doc), renumbered);
        QCOMPARE(doc.undoText(), QStringLiteral("Insert reference"));
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), before);
        QVERIFY(doc.redo());
        QCOMPARE(texts(doc), renumbered);

        // Deleting the target breaks the reference; it never moves to another caption.
        const int betaLength = int(doc.paragraphsInOrder().at(3)->text.size());
        QVERIFY(doc.deleteRange({doc.paragraphsInOrder().at(2)->id, 6}, {doc.paragraphsInOrder().at(3)->id, betaLength}));
        QCOMPARE(texts(doc), (QStringList{QStringLiteral("Figure 1Intro"), QStringLiteral("Figure 2: alpha"),
                                          QStringLiteral("Middle"), QStringLiteral("See [?]")}));
        const QVariantMap broken = reference(doc, xref);
        QCOMPARE(broken.value(QStringLiteral("status")).toString(), QStringLiteral("broken"));
        QCOMPARE(broken.value(QStringLiteral("formattedText")).toString(), QStringLiteral("[?]"));
        QCOMPARE(broken.value(QStringLiteral("targetId")).toString(), figB);
        QVERIFY(!broken.value(QStringLiteral("error")).toString().isEmpty());
        QVERIFY(doc.undo());
        QCOMPARE(texts(doc), renumbered);
        QCOMPARE(status(doc, xref), QStringLiteral("resolved"));

        // Cut and paste moves the caption with its identity: it renumbers.
        const Paragraph *beta = doc.paragraphsInOrder().at(3);
        const Fragment cut = doc.extractFragment({beta->id, 0}, {beta->id, int(beta->text.size())});
        QVERIFY(doc.deleteRange({beta->id, 0}, {beta->id, int(beta->text.size())}));
        QCOMPARE(status(doc, xref), QStringLiteral("broken"));
        QVERIFY(doc.insertFragment({doc.paragraphsInOrder().at(0)->id, 0}, cut));
        QCOMPARE(texts(doc), (QStringList{QStringLiteral("Figure 1: betaFigure 2Intro"), QStringLiteral("Figure 3: alpha"),
                                          QStringLiteral("Middle"), QString(), QStringLiteral("See Figure 1")}));
        QCOMPARE(status(doc, xref), QStringLiteral("resolved"));
        QCOMPARE(reference(doc, figB).value(QStringLiteral("number")).toInt(), 1);

        // A copy of a caption that is still in place is plain text.
        const Fragment copy = doc.extractFragment({doc.paragraphsInOrder().at(0)->id, 0}, {doc.paragraphsInOrder().at(0)->id, 8});
        QVERIFY(doc.insertFragment({doc.paragraphsInOrder().at(4)->id, 12}, copy));
        QCOMPARE(doc.paragraphsInOrder().at(4)->text, QStringLiteral("See Figure 1Figure 1"));
        QCOMPARE(doc.referenceSnapshots().size(), 4);
    }

    void fieldsAreAtomic()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Look"));
        const QString figure = doc.insertReference(pid(doc, 0), 4, QStringLiteral("caption"), QStringLiteral("Table"));
        QVERIFY(!figure.isEmpty());
        const QUuid p = doc.paragraphsInOrder().first()->id;
        QCOMPARE(doc.paragraphText(sid(p)), QStringLiteral("LookTable 1"));
        // Typing inside the result goes after it and does not join it.
        const auto caret = doc.insertText({p, 6}, QStringLiteral("!"));
        QVERIFY(caret);
        QCOMPARE(caret->offset, 12);
        QCOMPARE(doc.paragraphText(sid(p)), QStringLiteral("LookTable 1!"));
        QCOMPARE(reference(doc, figure).value(QStringLiteral("location")).toMap().value(QStringLiteral("length")).toInt(), 7);
        // Deleting any part of the result deletes all of it, as one step.
        const QByteArray before = RichTextCodec::encode(doc);
        const auto start = doc.deleteRange({p, 10}, {p, 11});
        QVERIFY(start);
        QCOMPARE(start->offset, 4);
        QCOMPARE(doc.paragraphText(sid(p)), QStringLiteral("Look!"));
        QVERIFY(doc.referenceSnapshots().isEmpty());
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), before);
    }

    void citationsAndBibliographyFollowSources()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("A\nB\nRefs"));
        const QString knuth = doc.addSource({{QStringLiteral("author"), QStringLiteral("Knuth")},
                                             {QStringLiteral("title"), QStringLiteral("TAOCP")},
                                             {QStringLiteral("year"), QStringLiteral("1968")}});
        const QString lamport = doc.addSource({{QStringLiteral("author"), QStringLiteral("Lamport")},
                                               {QStringLiteral("title"), QStringLiteral("LaTeX")},
                                               {QStringLiteral("year"), QStringLiteral("1986")},
                                               {QStringLiteral("publisher"), QStringLiteral("Addison-Wesley")}});
        QVERIFY(!knuth.isEmpty() && !lamport.isEmpty());
        const QString c1 = doc.insertReference(pid(doc, 0), 1, QStringLiteral("citation"), lamport);
        const QString c2 = doc.insertReference(pid(doc, 1), 1, QStringLiteral("citation"), knuth);
        const QString c3 = doc.insertReference(pid(doc, 1), 0, QStringLiteral("citation"), lamport);
        const QString bib = doc.insertReference(pid(doc, 2), 4, QStringLiteral("bibliography"), QString());
        QVERIFY(!c1.isEmpty() && !c2.isEmpty() && !c3.isEmpty() && !bib.isEmpty());
        const QString lamportEntry = QStringLiteral("[1] Lamport. LaTeX. Addison-Wesley, 1986.");
        QCOMPARE(texts(doc), (QStringList{QStringLiteral("A[1]"), QStringLiteral("[1]B[2]"),
                                          QStringLiteral("Refs") + lamportEntry + QChar(LineBreakChar)
                                              + QStringLiteral("[2] Knuth. TAOCP. 1968.")}));
        const QVariantList sources = doc.sourceSnapshots();
        QCOMPARE(sources.size(), 2);
        QCOMPARE(sources.at(0).toMap().value(QStringLiteral("id")).toString(), knuth); // sorted by author
        QCOMPARE(sources.at(0).toMap().value(QStringLiteral("number")).toInt(), 2);
        QCOMPARE(sources.at(1).toMap().value(QStringLiteral("cited")).toBool(), true);

        // Removing a source breaks its citations and renumbers the rest.
        const QByteArray before = RichTextCodec::encode(doc);
        QVERIFY(doc.removeSource(lamport));
        QCOMPARE(texts(doc), (QStringList{QStringLiteral("A[?]"), QStringLiteral("[?]B[1]"),
                                          QStringLiteral("Refs[1] Knuth. TAOCP. 1968.")}));
        QCOMPARE(status(doc, c1), QStringLiteral("broken"));
        QCOMPARE(reference(doc, c1).value(QStringLiteral("error")).toString(), QStringLiteral("The cited source was removed."));
        QCOMPARE(status(doc, c2), QStringLiteral("resolved"));
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), before);

        QVERIFY(doc.updateSource(knuth, {{QStringLiteral("year"), QStringLiteral("1997")}}));
        QVERIFY(doc.paragraphsInOrder().at(2)->text.endsWith(QStringLiteral("[2] Knuth. TAOCP. 1997.")));

        // Edits: a label, a new target, removal.
        QVERIFY(doc.editReference(c2, {{QStringLiteral("label"), QStringLiteral("Classic")}}));
        QCOMPARE(reference(doc, c2).value(QStringLiteral("label")).toString(), QStringLiteral("Classic"));
        QVERIFY(doc.editReference(c3, {{QStringLiteral("targetId"), knuth}}));
        QCOMPARE(doc.paragraphsInOrder().at(1)->text, QStringLiteral("[2]B[2]"));
        QVERIFY(doc.removeReference(c2));
        QCOMPARE(doc.paragraphsInOrder().at(1)->text, QStringLiteral("[2]B"));
        QVERIFY(reference(doc, c2).isEmpty());
    }

    void bibliographyEntriesEndEachPartOnce()
    {
        ReferenceSource source;
        source.author = QStringLiteral("Wu, L.");
        source.title = QStringLiteral("Field calibration of soil sensors");
        source.year = QStringLiteral("2024");
        source.publisher = QStringLiteral("Hydrology Press");
        QCOMPARE(bibliographyEntry(source, 1),
                 QStringLiteral("[1] Wu, L. Field calibration of soil sensors. Hydrology Press, 2024."));
        source.title = QStringLiteral("Notes.");
        source.publisher.clear();
        QCOMPARE(bibliographyEntry(source, 2), QStringLiteral("[2] Wu, L. Notes. 2024."));
    }

    void unsupportedRequestsAreTypedAndChangeNothing()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Text"));
        const QString source = doc.addSource({{QStringLiteral("title"), QStringLiteral("Report")}});
        const QString citation = doc.insertReference(pid(doc, 0), 4, QStringLiteral("citation"), source);
        QVERIFY(!citation.isEmpty());
        const QByteArray before = RichTextCodec::encode(doc);
        QVERIFY(doc.insertReference(pid(doc, 0), 0, QStringLiteral("index-entry"), source).isEmpty());
        QCOMPARE(doc.lastErrorCode(), QStringLiteral("unsupported"));
        QVERIFY(doc.insertReference(pid(doc, 0), 0, QStringLiteral("footnote"), QStringLiteral(" ")).isEmpty());
        QCOMPARE(doc.lastErrorCode(), QStringLiteral("invalidArgument")); // a note needs text
        QVERIFY(doc.insertReference(pid(doc, 0), 0, QStringLiteral("cross-reference"), source).isEmpty());
        QCOMPARE(doc.lastErrorCode(), QStringLiteral("invalidArgument"));
        QVERIFY(doc.insertReference(pid(doc, 0), 0, QStringLiteral("caption"), QStringLiteral("sequence:9")).isEmpty());
        QCOMPARE(doc.lastErrorCode(), QStringLiteral("invalidArgument"));
        QVERIFY(doc.insertReference(pid(doc, 0), 0, QStringLiteral("citation"), QStringLiteral("missing")).isEmpty());
        QVERIFY(!doc.editReference(citation, {{QStringLiteral("type"), QStringLiteral("bibliography")}}));
        QCOMPARE(doc.lastErrorCode(), QStringLiteral("unsupported"));
        QVERIFY(!doc.addSource({{QStringLiteral("year"), QStringLiteral("2020")}}).size());
        QCOMPARE(RichTextCodec::encode(doc), before);
        QCOMPARE(doc.referenceTypes(), (QStringList{QStringLiteral("caption"), QStringLiteral("cross-reference"),
                                                    QStringLiteral("citation"), QStringLiteral("bibliography"),
                                                    QStringLiteral("footnote"), QStringLiteral("endnote")}));
    }

    void referencesPersistAndStaleResultsUpdate()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Intro\n: alpha\nSee "));
        const QString figure = doc.insertReference(pid(doc, 1), 0, QStringLiteral("caption"), QStringLiteral("Figure"));
        const QString xref = doc.insertReference(pid(doc, 2), 4, QStringLiteral("cross-reference"), figure);
        const QString source = doc.addSource({{QStringLiteral("author"), QStringLiteral("Wu")}});
        QVERIFY(!doc.insertReference(pid(doc, 0), 5, QStringLiteral("citation"), source).isEmpty());
        const QByteArray json = RichTextCodec::encode(doc);
        const QJsonObject root = QJsonDocument::fromJson(json).object();
        QCOMPARE(root.value(QStringLiteral("version")).toInt(), 2);
        QCOMPARE(root.value(QStringLiteral("fields")).toArray().size(), 3);
        QCOMPARE(root.value(QStringLiteral("sources")).toArray().size(), 1);

        RichTextDocument reopened;
        QVERIFY(reopened.loadJson(QString::fromUtf8(json)));
        QCOMPARE(RichTextCodec::encode(reopened), json);
        QCOMPARE(reopened.referenceSnapshots(), doc.referenceSnapshots());

        // A result edited outside the toolkit reads "stale" until updated.
        RichTextDocument edited;
        QVERIFY(edited.loadJson(QString::fromUtf8(json).replace(QStringLiteral("Figure 1: alpha"), QStringLiteral("Figure 9: alpha"))));
        QCOMPARE(status(edited, figure), QStringLiteral("stale"));
        QCOMPARE(status(edited, xref), QStringLiteral("resolved"));
        QVERIFY(edited.updateFields());
        QCOMPARE(edited.paragraphsInOrder().at(1)->text, QStringLiteral("Figure 1: alpha"));
        QCOMPARE(status(edited, figure), QStringLiteral("resolved"));
        QVERIFY(edited.undo());
        QCOMPARE(edited.paragraphsInOrder().at(1)->text, QStringLiteral("Figure 9: alpha"));

        // A mark naming a missing record is dropped with a warning.
        QJsonObject withoutFields = root;
        withoutFields.remove(QStringLiteral("fields"));
        QStringList warnings;
        RichTextDocument stripped;
        QVERIFY(RichTextCodec::fromJsonObject(withoutFields, stripped, nullptr, &warnings));
        QVERIFY(warnings.join(u'\n').contains(QStringLiteral("field ignored: unknown record")));
        QVERIFY(stripped.referenceSnapshots().isEmpty());

        // An invalid record fails with its path and changes nothing.
        QJsonObject invalid = root;
        QJsonArray fields = invalid.value(QStringLiteral("fields")).toArray();
        QJsonObject first = fields.at(0).toObject();
        first.insert(QStringLiteral("kind"), QStringLiteral("sidenote"));
        fields.replace(0, first);
        invalid.insert(QStringLiteral("fields"), fields);
        OperationError error;
        RichTextDocument untouched;
        const QByteArray empty = RichTextCodec::encode(untouched);
        QVERIFY(!RichTextCodec::fromJsonObject(invalid, untouched, &error));
        QCOMPARE(error.fieldPath, QStringLiteral("fields[0].kind"));
        QCOMPARE(RichTextCodec::encode(untouched), empty);
    }
};

QTEST_MAIN(RichTextReferencesTest)
#include "tst_rich_text_references.moc"
