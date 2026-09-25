// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-056: the persistent slide deck (docs/slides.md): master/layout
// inheritance with deliberate overrides, reorder/duplicate, rich text boxes,
// undo, and save/recover/reopen/export through the shared session services.
#include "document_manager.h"
#include "rich_text_document.h"
#include "slide_deck.h"
#include "undo_service.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaTK;

namespace {
QVariantMap objectWithRole(const SlideDeck &deck, const QString &slideId, const QString &role)
{
    for (const QVariant &v : deck.objects(slideId))
        if (v.toMap().value("role").toString() == role) return v.toMap();
    return {};
}
QVariantMap objectWithId(const SlideDeck &deck, const QString &slideId, const QString &id)
{
    for (const QVariant &v : deck.objects(slideId))
        if (v.toMap().value("id").toString() == id) return v.toMap();
    return {};
}
QVariantMap box(double x, double y, double w, double h)
{
    return {{"x", x}, {"y", y}, {"width", w}, {"height", h}};
}
QVariantMap placeholder(const SlideDeck &deck, const QString &masterId, const QString &role)
{
    for (const QVariant &m : deck.masterSummaries())
        if (m.toMap().value("id").toString() == masterId)
            for (const QVariant &p : m.toMap().value("placeholders").toList())
                if (p.toMap().value("role").toString() == role) return p.toMap();
    return {};
}
int pdfPages(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return -1;
    const QByteArray bytes = file.readAll();
    if (!bytes.startsWith("%PDF-")) return -1;
    int pages = 0; // page objects, not the /Pages tree node
    auto it = QRegularExpression(QStringLiteral("/Type\\s*/Page(?![a-zA-Z])")).globalMatch(QString::fromLatin1(bytes));
    while (it.hasNext()) { it.next(); ++pages; }
    return pages;
}
bool saved(DocumentSession *session, const std::function<bool()> &start)
{
    QSignalSpy finished(session, &DocumentSession::saveFinished);
    if (!start() || !finished.wait(10000)) return false;
    return finished.constFirst().at(0).toBool();
}
} // namespace

class SlideDeckTest : public QObject {
    Q_OBJECT
private slots:
    void newDeckHasMasterLayoutsAndATitleSlide()
    {
        SlideDeck deck;
        QCOMPARE(deck.slideIds().size(), 1);
        QCOMPARE(deck.masterSummaries().size(), 1);
        const QVariantMap master = deck.masterSummaries().first().toMap();
        QCOMPARE(master.value("id").toString(), QStringLiteral("master-default"));
        QCOMPARE(master.value("layouts").toList().size(), 2);
        const QString slide = deck.slideIds().first();
        const QVariantMap title = objectWithRole(deck, slide, "title");
        QCOMPARE(title.value("text").toString(), QStringLiteral("Click to add title"));
        QVERIFY(title.value("inheritedText").toBool() && title.value("inheritedBounds").toBool());
        // Title layout position (80, 180, 800 x 110), not the master's.
        QCOMPARE(title.value("bounds").toMap().value("y").toDouble(), 180.0);
        QVERIFY(!deck.canUndo());

        // Sorter thumbnails come from the slide painter, cached until an edit.
        const QString preview = deck.slideSummaries().first().toMap().value("previewUrl").toString();
        QVERIFY(preview.startsWith(QStringLiteral("data:image/png;base64,")));
        QCOMPARE(deck.slideSummaries().first().toMap().value("previewUrl").toString(), preview);
        QVERIFY(!deck.insertObject(slide, "shape", box(0, 0, 480, 270)).isEmpty());
        QVERIFY(deck.slideSummaries().first().toMap().value("previewUrl").toString() != preview);
    }

    void masterChangesKeepDeliberateOverrides()
    {
        SlideDeck deck;
        const QString master = QStringLiteral("master-default");
        const QString first = deck.slideIds().first();
        const QString second = deck.insertSlide(first);
        QVERIFY2(!second.isEmpty(), qPrintable(deck.lastError()));
        QCOMPARE(deck.slide(second).value("layoutId").toString(), QStringLiteral("layout-content"));
        const QString secondTitle = objectWithRole(deck, second, "title").value("id").toString();
        QVERIFY(deck.setObjectText(secondTitle, QStringLiteral("Agenda")));

        QVERIFY(deck.setPlaceholderDefault(master, "title", QStringLiteral("Quarterly review")));
        QCOMPARE(objectWithRole(deck, first, "title").value("text").toString(), QStringLiteral("Quarterly review"));
        QCOMPARE(objectWithRole(deck, second, "title").value("text").toString(), QStringLiteral("Agenda"));
        QVERIFY(!objectWithRole(deck, second, "title").value("inheritedText").toBool());
        QCOMPARE(placeholder(deck, master, "title").value("overrideCount").toInt(), 1);
        QCOMPARE(deck.placeholderOverrides(master).size(), 1);
        QCOMPARE(deck.slideSummaries().first().toMap().value("title").toString(), QStringLiteral("Quarterly review"));

        // A layout move updates inheriting slides only.
        QVERIFY(deck.setPlaceholderBounds(master, "layout-content", "title", box(100, 20, 700, 80)));
        QCOMPARE(objectWithRole(deck, second, "title").value("bounds").toMap().value("x").toDouble(), 100.0);
        QVERIFY(deck.moveObject(secondTitle, box(300, 300, 200, 60)));
        QVERIFY(deck.setPlaceholderBounds(master, "layout-content", "title", box(60, 30, 700, 80)));
        QCOMPARE(objectWithRole(deck, second, "title").value("bounds").toMap().value("x").toDouble(), 300.0);

        // Resetting the override returns to the inherited text and layout geometry.
        QVERIFY(deck.resetOverride(second, "title"));
        const QVariantMap reset = objectWithRole(deck, second, "title");
        QCOMPARE(reset.value("text").toString(), QStringLiteral("Quarterly review"));
        QCOMPARE(reset.value("bounds").toMap().value("x").toDouble(), 60.0);
        QVERIFY(deck.undo());
        QCOMPARE(objectWithRole(deck, second, "title").value("text").toString(), QStringLiteral("Agenda"));

        // Applying another master keeps the override and adopts the new defaults.
        const QString dark = deck.createMaster(QStringLiteral("Dark"));
        QVERIFY(!dark.isEmpty());
        QVERIFY(deck.setPlaceholderDefault(dark, "body", QStringLiteral("Dark body")));
        QVERIFY(deck.setMasterBackground(dark, "#101418", "#f0f0f0"));
        QVERIFY(deck.applyMaster(dark, {first, second}));
        QCOMPARE(deck.slide(second).value("masterId").toString(), dark);
        QVERIFY(deck.slide(second).value("layoutId").toString().startsWith(QStringLiteral("layout-content-")));
        QCOMPARE(objectWithRole(deck, second, "title").value("text").toString(), QStringLiteral("Agenda"));
        QCOMPARE(objectWithRole(deck, second, "body").value("text").toString(), QStringLiteral("Dark body"));
        QCOMPARE(objectWithRole(deck, first, "title").value("text").toString(), QStringLiteral("Click to add title"));
        QVERIFY(!deck.applyMaster(QStringLiteral("missing"), {first}));
        QVERIFY(!deck.lastError().isEmpty());
    }

    void reorderAndDuplicateKeepContentAndEffects()
    {
        SlideDeck deck;
        const QString first = deck.slideIds().first();
        const QString shape = deck.insertObject(first, "shape", box(100, 100, 200, 100));
        const QString effect = deck.addEffect(shape, "fly-in");
        QVERIFY(!effect.isEmpty());
        QVERIFY(deck.setTransition(first, "fade", 0.3));
        QVERIFY(deck.setNotes(first, QStringLiteral("Open with the customer story")));
        QVERIFY(deck.setSlideSection(first, QStringLiteral("Opening")));
        const QString second = deck.insertSlide(first);
        const Slides::DeckState beforeDuplicate = deck.state();

        const QString copy = deck.duplicateSlide(first);
        QVERIFY(!copy.isEmpty());
        QCOMPARE(deck.slideIds(), (QStringList{first, copy, second}));
        const QVariantList copied = deck.effects(copy);
        QCOMPARE(copied.size(), 1);
        const QString copiedTarget = copied.first().toMap().value("targetId").toString();
        QVERIFY(copiedTarget != shape);
        QVERIFY(copied.first().toMap().value("id").toString() != effect);
        QCOMPARE(objectWithId(deck, copy, copiedTarget).value("kind").toString(), QStringLiteral("shape"));
        QCOMPARE(deck.notes(copy), QStringLiteral("Open with the customer story"));
        QCOMPARE(deck.slide(copy).value("section").toString(), QStringLiteral("Opening"));
        QCOMPARE(deck.slide(copy).value("transition").toMap().value("durationTicks").toString(),
                 QStringLiteral("211680000"));

        QVERIFY(deck.moveSlide(second, 0));
        QCOMPARE(deck.slideIds(), (QStringList{second, first, copy}));
        QVERIFY(deck.setSlideHidden(copy, true));
        QVERIFY(deck.slideSummaries().at(2).toMap().value("hidden").toBool());
        QVERIFY(!deck.moveSlide(second, 3));

        // One undo step per edit; undoing all three restores the exact state.
        QCOMPARE(deck.undoService()->undoText(&deck), QStringLiteral("Hide slide"));
        QVERIFY(deck.undo() && deck.undo() && deck.undo());
        QVERIFY(deck.state() == beforeDuplicate);
        QVERIFY(deck.redo());
        QCOMPARE(deck.slideIds().size(), 3);

        // Deleting an object removes the effects that target it.
        QVERIFY(deck.removeObject(copiedTarget));
        QVERIFY(deck.effects(copy).isEmpty());
        QVERIFY(!deck.removeSlide(QStringLiteral("nope")));
    }

    void effectEditsUseExactTicks()
    {
        SlideDeck deck;
        const QString slide = deck.slideIds().first();
        const QString shape = deck.insertObject(slide, "shape", box(10, 10, 50, 50));
        const QString effect = deck.addEffect(shape, "fade-in");
        QVERIFY(deck.editEffect(effect, {{"delay", 0.1}, {"duration", 1.25}, {"easing", "linear"}, {"trigger", "after"}}));
        QVariantMap e = deck.effects(slide).first().toMap();
        QCOMPARE(e.value("delayTicks").toString(), QStringLiteral("70560000"));
        QCOMPARE(e.value("durationTicks").toString(), QStringLiteral("882000000"));
        QVERIFY(deck.editEffect(effect, {{"durationTicks", "705600001"}}));
        QCOMPARE(deck.effects(slide).first().toMap().value("durationTicks").toString(), QStringLiteral("705600001"));
        const Slides::DeckState before = deck.state();
        QVERIFY(!deck.editEffect(effect, {{"easing", "bounce"}}));
        QVERIFY(!deck.editEffect(effect, {{"delay", -1}}));
        QVERIFY(deck.state() == before); // rejected edits change nothing
        const QString second = deck.addEffect(shape, "fade-out");
        QVERIFY(deck.moveEffect(second, 0));
        QCOMPARE(deck.effects(slide).first().toMap().value("id").toString(), second);
        QVERIFY(deck.removeEffect(second));
        QCOMPARE(deck.effects(slide).size(), 1);
    }

    void textBoxesEditThroughTheRichTextStack()
    {
        SlideDeck deck;
        const QString slide = deck.slideIds().first();
        const QString text = deck.insertObject(slide, "text", box(100, 400, 400, 80), QStringLiteral("Hello"));
        QCOMPARE(objectWithId(deck, slide, text).value("text").toString(), QStringLiteral("Hello"));
        RichTextDocument *document = deck.beginTextEdit(text);
        QVERIFY2(document, qPrintable(deck.lastError()));
        QVERIFY(deck.textEditing());
        QVERIFY(!deck.canUndo());                    // deck history waits for the text edit
        QVERIFY(!deck.setNotes(slide, QStringLiteral("x")));
        QCOMPARE(document->text(), QStringLiteral("Hello"));
        QVERIFY(document->insertText(document->endPosition(), QStringLiteral(" world")));
        QVERIFY(document->splitParagraph(document->endPosition()));
        QVERIFY(document->insertText(document->endPosition(), QStringLiteral("Second line")));
        QVERIFY(deck.commitTextEdit());
        QVERIFY(!deck.textEditing());
        QCOMPARE(objectWithId(deck, slide, text).value("text").toString(), QStringLiteral("Hello world\nSecond line"));
        QCOMPARE(deck.undoService()->undoText(&deck), QStringLiteral("Edit text"));
        QVERIFY(deck.undo());
        QCOMPARE(objectWithId(deck, slide, text).value("text").toString(), QStringLiteral("Hello"));
        // A cancelled edit leaves content and history untouched.
        QVERIFY(deck.beginTextEdit(text));
        deck.cancelTextEdit();
        QCOMPARE(objectWithId(deck, slide, text).value("text").toString(), QStringLiteral("Hello"));
        QVERIFY(deck.canRedo());
    }

    void sessionSaveRecoverReopenAndExport()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("talk.qtkslides");
        const QString recovery = dir.filePath("recovery");
        QString documentId;
        Slides::DeckState expected;
        {
            DocumentManager manager;
            manager.recoveryManager()->setDirectory(recovery);
            DocumentSession *session = manager.createDocument(SlideDeck::schemaId());
            QVERIFY2(session, qPrintable(manager.lastError()));
            session->setAutosaveDelayMs(600000);
            QCOMPARE(session->kind(), QStringLiteral("slides"));
            auto *deck = qobject_cast<SlideDeck *>(session->modelObject());
            QVERIFY(deck);
            QCOMPARE(deck->undoService(), manager.undoService());
            const QString first = deck->slideIds().first();
            QVERIFY(deck->setPlaceholderDefault("master-default", "title", QStringLiteral("Saved title")));
            const QString shape = deck->insertObject(first, "shape", box(100, 100, 200, 100));
            QVERIFY(!deck->addEffect(shape, "zoom-in").isEmpty());
            QVERIFY(deck->setNotes(first, QStringLiteral("Speaker only")));
            QVERIFY(saved(session, [&] { return session->requestSaveAs(path); }));
            QVERIFY(!session->isDirty());
            const QString second = deck->insertSlide(first);
            QVERIFY(deck->setObjectText(objectWithRole(*deck, second, "body").value("id").toString(),
                                        QStringLiteral("Recovered body")));
            QVERIFY(session->isDirty());
            QVERIFY(session->autosaveNow());
            documentId = session->documentId();
            expected = deck->state();
            QCOMPARE(manager.recoveryManager()->inspectId(documentId).value("schemaId").toString(),
                     SlideDeck::schemaId());
        }
        DocumentManager restarted;
        restarted.recoveryManager()->setDirectory(recovery);
        DocumentSession *restored = restarted.restoreRecovered(documentId, path);
        QVERIFY2(restored, qPrintable(restarted.lastError()));
        auto *recoveredDeck = qobject_cast<SlideDeck *>(restored->modelObject());
        QVERIFY(recoveredDeck);
        QVERIFY(recoveredDeck->state() == expected);
        QVERIFY(restored->recovered() && restored->isDirty());
        QVERIFY(saved(restored, [&] { return restored->requestSave(); }));
        QVERIFY(restarted.closeDocument(documentId, DocumentManager::ClosePolicy::Discard));

        DocumentSession *reopened = restarted.openDocument(path);
        QVERIFY2(reopened, qPrintable(restarted.lastError()));
        QCOMPARE(reopened->documentId(), documentId);
        auto *deck = qobject_cast<SlideDeck *>(reopened->modelObject());
        QVERIFY(deck);
        QVERIFY(deck->state() == expected);
        QVERIFY(!deck->canUndo());
        QCOMPARE(deck->notes(deck->slideIds().first()), QStringLiteral("Speaker only"));
        // Canonical encoding: a reopened deck encodes to the same bytes.
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray bytes = file.readAll();
        QCOMPARE(SlideDeck::encodeState(deck->state(), deck->modelHeader(), reopened->extensions()), bytes);
        QVERIFY(slideDeckDocumentCodec()->recognizes(bytes));

        // Export: one page per shown slide, notes pages double it.
        QVERIFY(deck->setSlideHidden(deck->slideIds().last(), true));
        const QString pdf = dir.filePath("talk.pdf");
        QVERIFY2(deck->exportPdf(pdf), qPrintable(deck->lastError()));
        QCOMPARE(pdfPages(pdf), 1);
        const QString notesPdf = dir.filePath("talk-notes.pdf");
        QVERIFY(deck->exportPdf(notesPdf, true));
        QCOMPARE(pdfPages(notesPdf), 2);
        QVERIFY(deck->previewDataUrl(deck->slideIds().first(), 160).startsWith(QStringLiteral("data:image/png;base64,")));
    }

    void decodingRejectsBrokenReferences()
    {
        SlideDeck deck;
        const QString slide = deck.slideIds().first();
        const QString shape = deck.insertObject(slide, "shape", box(1, 1, 10, 10));
        QVERIFY(!deck.addEffect(shape, "appear").isEmpty());
        const QByteArray good = deck.toJson().toUtf8();
        QString goodProblem;
        QVERIFY2(SlideDeck::decodeState(good, nullptr, nullptr, nullptr, &goodProblem), qPrintable(goodProblem));

        QJsonObject root = QJsonDocument::fromJson(good).object();
        QJsonArray slides = root.value("slides").toArray();
        QJsonObject first = slides.first().toObject();
        QJsonArray effects = first.value("effects").toArray();
        QJsonObject effect = effects.first().toObject();
        effect["targetId"] = QStringLiteral("object-missing");
        effects[0] = effect;
        first["effects"] = effects;
        slides[0] = first;
        root["slides"] = slides;
        QString problem;
        QVERIFY(!SlideDeck::decodeState(QJsonDocument(root).toJson(), nullptr, nullptr, nullptr, &problem));
        QVERIFY(problem.contains(QStringLiteral("effect")));

        root = QJsonDocument::fromJson(good).object();
        slides = root.value("slides").toArray();
        first = slides.first().toObject();
        first["masterId"] = QStringLiteral("master-missing");
        slides[0] = first;
        root["slides"] = slides;
        QVERIFY(!SlideDeck::decodeState(QJsonDocument(root).toJson(), nullptr, nullptr, nullptr, &problem));
        const Slides::DeckState before = deck.state();
        QVERIFY(!deck.fromJson(QString::fromUtf8(QJsonDocument(root).toJson())));
        QVERIFY(deck.state() == before);
    }
};

QTEST_MAIN(SlideDeckTest)
#include "tst_slide_deck.moc"
