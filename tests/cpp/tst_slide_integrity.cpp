// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-056 audit fixes (docs/slides.md): only deliberate text edits become
// overrides, placeholder prompts never reach an output, text and deck size
// are bounded at edit time so every accepted deck saves and reopens, and the
// undo history charges text at its real size.
#include "rich_text_document.h"
#include "slide_canvas_document.h"
#include "slide_deck.h"
#include "slide_render.h"
#include "slide_timeline.h"
#include "undo_service.h"

#include <QFile>
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
QVariantMap box(double x, double y, double w, double h)
{
    return {{"x", x}, {"y", y}, {"width", w}, {"height", h}};
}
// Every pixel of `image` equals `color`.
bool uniform(const QImage &image, QRgb color)
{
    const QImage rgb = image.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < rgb.height(); ++y)
        for (int x = 0; x < rgb.width(); ++x)
            if ((rgb.pixel(x, y) & 0xffffff) != (color & 0xffffff)) return false;
    return true;
}
} // namespace

class SlideIntegrityTest : public QObject {
    Q_OBJECT
private slots:
    void committingAnUntouchedPlaceholderKeepsItInheriting()
    {
        SlideDeck deck;
        const QString master = QStringLiteral("master-default");
        const QString slide = deck.insertSlide(deck.slideIds().first());
        const QString title = objectWithRole(deck, slide, "title").value("id").toString();
        QVERIFY(deck.setPlaceholderDefault(master, "title", QStringLiteral("Quarterly review")));
        const QString revision = deck.revision();

        // Open and commit without typing: no override, no undo step.
        QVERIFY(deck.beginTextEdit(title));
        QVERIFY(deck.commitTextEdit());
        QVERIFY(objectWithRole(deck, slide, "title").value("inheritedText").toBool());
        QCOMPARE(deck.revision(), revision);
        QCOMPARE(deck.undoService()->undoText(&deck), QStringLiteral("Edit master placeholder"));
        QVERIFY(deck.setPlaceholderDefault(master, "title", QStringLiteral("Annual review")));
        QCOMPARE(objectWithRole(deck, slide, "title").value("text").toString(), QStringLiteral("Annual review"));

        // Typing is deliberate: the slide keeps its text from then on.
        RichTextDocument *document = deck.beginTextEdit(title);
        QVERIFY(document);
        QVERIFY(document->insertText(document->endPosition(), QStringLiteral(" 2026")));
        QVERIFY(deck.commitTextEdit());
        QVERIFY(!objectWithRole(deck, slide, "title").value("inheritedText").toBool());
        QVERIFY(deck.setPlaceholderDefault(master, "title", QStringLiteral("Other")));
        QCOMPARE(objectWithRole(deck, slide, "title").value("text").toString(), QStringLiteral("Annual review 2026"));
    }

    void placeholderPromptsNeverReachAnOutput()
    {
        SlideDeck deck;
        const QString slide = deck.slideIds().first();
        const QVariantMap title = objectWithRole(deck, slide, "title");
        QCOMPARE(title.value("text").toString(), QString());
        QCOMPARE(title.value("prompt").toString(), QStringLiteral("Click to add title"));
        QCOMPARE(deck.slideSummaries().first().toMap().value("title").toString(), QString());

        // Audience frames, thumbnails and export pages paint only content:
        // an untouched title slide is the master background alone.
        QVERIFY(uniform(deck.renderFrame(slide, -1, "0", 480, 270), qRgb(255, 255, 255)));
        const QString inserted = deck.insertSlide(slide);
        QVERIFY(uniform(deck.renderFrame(inserted, 0, "0", 480, 270), qRgb(255, 255, 255)));
        QVERIFY(uniform(Slides::renderImage(deck.state(), Slides::finalFrame(deck.state(), slide), QSize(320, 180)),
                        qRgb(255, 255, 255)));

        // The editing canvas shows the prompt, muted; hiding prompts leaves it empty.
        SlideCanvasDocument canvas;
        canvas.setDeck(&deck);
        canvas.setSlideId(slide);
        const CanvasObject *object = canvas.object(canvas.canvasObjectId(title.value("id").toString()));
        QVERIFY(object);
        const CanvasTiledImagePtr withPrompt = canvas.image(object->image);
        QVERIFY(withPrompt && !withPrompt->tiles.isEmpty());
        bool inked = false;
        for (const QImage &tile : withPrompt->tiles)
            for (int y = 0; y < tile.height() && !inked; ++y)
                for (int x = 0; x < tile.width() && !inked; ++x) inked = qAlpha(tile.pixel(x, y)) > 0;
        QVERIFY(inked);
        canvas.setShowPrompts(false);
        for (const QImage &tile : canvas.image(object->image)->tiles)
            for (int y = 0; y < tile.height(); ++y)
                for (int x = 0; x < tile.width(); ++x) QCOMPARE(qAlpha(tile.pixel(x, y)), 0);

        // Prompts are master data: they round-trip through the codec.
        SlideDeck reopened;
        QVERIFY(reopened.fromJson(deck.toJson()));
        QCOMPARE(objectWithRole(reopened, slide, "title").value("prompt").toString(), QStringLiteral("Click to add title"));
    }

    void textIsBoundedAtEditTime()
    {
        SlideDeck deck;
        const QString slide = deck.slideIds().first();
        const QString box1 = deck.insertObject(slide, "text", box(10, 10, 300, 100));
        const Slides::DeckState before = deck.state();
        const QString huge(3 * 1024 * 1024, QLatin1Char('x'));
        QVERIFY(!deck.setObjectText(box1, huge));
        QVERIFY(deck.lastError().contains(QStringLiteral("MiB")));
        QVERIFY(!deck.setNotes(slide, huge));
        QVERIFY(deck.state() == before);
        QVERIFY(deck.setNotes(slide, QString(1024 * 1024, QLatin1Char('n'))));
    }

    void undoChargesTextAtItsSize()
    {
        SlideDeck deck;
        const QString slide = deck.slideIds().first();
        const QString text = deck.insertObject(slide, "text", box(10, 10, 300, 100));
        const QString shape = deck.insertObject(slide, "shape", box(400, 10, 100, 100));
        const quint64 small = deck.undoService()->retainedBytes(&deck);
        const qsizetype chars = 1024 * 1024;
        QVERIFY(deck.setObjectText(text, QString(chars, QLatin1Char('t'))));
        const quint64 withText = deck.undoService()->retainedBytes(&deck);
        // The step holds the new text: at least its UTF-16 plain copy.
        QVERIFY2(withText - small >= quint64(chars) * 2, qPrintable(QString::number(withText - small)));
        // Later edits share that text instead of charging it again.
        QVERIFY(deck.moveObject(shape, box(420, 20, 100, 100)));
        QVERIFY2(deck.undoService()->retainedBytes(&deck) - withText < 64 * 1024,
                 qPrintable(QString::number(deck.undoService()->retainedBytes(&deck) - withText)));

        // The memory limit therefore bounds a history of large text edits.
        const quint64 limit = quint64(32) * 1024 * 1024;
        deck.undoService()->setMemoryLimit(limit);
        for (int i = 0; i < 8; ++i)
            QVERIFY(deck.setObjectText(text, QString(chars, QLatin1Char('a' + i))));
        QVERIFY(deck.undoService()->retainedBytes(&deck) <= limit);
        QVERIFY(deck.canUndo());
    }

    void everyAcceptedDeckSavesAndReopens()
    {
        // The bound is an upper estimate of the real encoding, escapes and
        // non-ASCII included.
        SlideDeck deck;
        const QString slide = deck.slideIds().first();
        const QString odd = QStringLiteral("\"\\\n\té中\U0001F600 %1").repeated(500);
        QVERIFY(deck.setNotes(slide, odd));
        const QString t = deck.insertObject(slide, "text", box(1.123456789, 2.5, 333.333333, 44.4444), odd);
        QVERIFY(deck.transformObjects({QVariantMap{{"id", t}, {"bounds", box(1.123456789, 2.5, 333.333333, 44.4444)},
                                                   {"rotation", -33.3333333}}}));
        QVERIFY(deck.setSlideSection(slide, odd.left(200)));
        const QByteArray encoded = deck.toJson().toUtf8();
        QVERIFY2(Slides::encodedBytesUpperBound(deck.state()) + 1024 >= encoded.size(),
                 qPrintable(QStringLiteral("%1 < %2").arg(Slides::encodedBytesUpperBound(deck.state())).arg(encoded.size())));

        // A deck limited to 256 KiB refuses the edit that would pass it, so
        // what it holds always saves and loads again.
        SlideDeck limited;
        limited.setEncodedLimit(256 * 1024);
        const QString first = limited.slideIds().first();
        int accepted = 0;
        QString current = first;
        for (int i = 0; i < 20; ++i) {
            if (!limited.setNotes(current, QString(40 * 1024, QLatin1Char('a' + i)))) break;
            ++accepted;
            current = limited.insertSlide(current);
            QVERIFY(!current.isEmpty());
        }
        QVERIFY(accepted > 1 && accepted < 20);
        QVERIFY(limited.lastError().contains(QStringLiteral("size limit")));
        QTemporaryDir dir;
        const QString path = dir.filePath("limited.qtkslides");
        QVERIFY2(limited.saveToFile(path), qPrintable(limited.lastError()));
        QVERIFY(QFile(path).size() <= 256 * 1024);
        SlideDeck again;
        QVERIFY2(again.loadFromFile(path), qPrintable(again.lastError()));
        QVERIFY(again.state() == limited.state());
    }
};

QTEST_MAIN(SlideIntegrityTest)
#include "tst_slide_integrity.moc"
