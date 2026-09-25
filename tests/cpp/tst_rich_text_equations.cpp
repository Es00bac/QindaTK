// SPDX-License-Identifier: LGPL-3.0-or-later
// Equations (WP-052, D-114): the documented TeX subset, typed errors for
// what lies outside it, canonical source round trips, the linear text, the
// box layout, painting, and equations as undoable inline document objects.
#include "rich_text_codec.h"
#include "rich_text_equation.h"
#include "rich_text_layout.h"
#include "rich_text_objects.h"
#include <QImage>
#include <QPainter>
#include <QtTest>
#include <cmath>

using namespace QindaTK;
using namespace QindaTK::RichText;

namespace {

QString sid(const QUuid &id) { return id.toString(QUuid::WithoutBraces); }

QString dump(const QString &source)
{
    const Equation::Parsed parsed = Equation::parse(source);
    return parsed.root ? Equation::structure(*parsed.root) : QStringLiteral("error: ") + parsed.error.message;
}

Equation::Box boxOf(const QString &source, double size = 12)
{
    const Equation::Parsed parsed = Equation::parse(source);
    return parsed.root ? Equation::layout(*parsed.root, size) : Equation::Box{};
}

int inkPixels(const QImage &image, int top, int bottom)
{
    int count = 0;
    for (int y = top; y < bottom; ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qGray(image.pixel(x, y)) < 128) ++count;
    return count;
}

struct RecordingRenderer final : RichTextObjectRenderer {
    QStringList painted;
    void paint(QPainter *, const QRectF &, const RichObject &object, const PaintContext &) override
    {
        painted.append(object.id);
    }
};

} // namespace

class RichTextEquationsTest : public QObject {
    Q_OBJECT
private slots:
    void parsesTheDocumentedSubset()
    {
        QCOMPARE(dump(QStringLiteral("\\frac{a+b}{2}")), QStringLiteral("row[frac[row[id:a op:+ id:b] row[num:2]]]"));
        QCOMPARE(dump(QStringLiteral("x_i^2")), QStringLiteral("row[scripts_^[id:x id:i num:2]]"));
        QCOMPARE(dump(QStringLiteral("x^2_i")), QStringLiteral("row[scripts_^[id:x id:i num:2]]"));
        QCOMPARE(dump(QStringLiteral("\\sum_{i=1}^{n} i")),
                 QStringLiteral("row[scripts_^[big:∑ row[id:i op:= num:1] id:n] id:i]"));
        QCOMPARE(dump(QStringLiteral("\\sqrt[3]{x}")), QStringLiteral("row[root[row[id:x] row[num:3]]]"));
        QCOMPARE(dump(QStringLiteral("\\left( a \\right]")), QStringLiteral("row[fenced:(][row[id:a]]]"));
        QCOMPARE(dump(QStringLiteral("\\alpha\\,\\text{if}")), QStringLiteral("row[id:α space:, text:if]"));
        QCOMPARE(dump(QStringLiteral("f'")), QStringLiteral("row[id:f op:′]"));
        QCOMPARE(dump(QStringLiteral("\\sin x")), QStringLiteral("row[fn:sin id:x]"));
        QCOMPARE(dump(QStringLiteral("3.14 r")), QStringLiteral("row[num:3.14 id:r]"));
    }

    void canonicalSourceRoundTrips()
    {
        const QList<std::pair<QString, QString>> cases{
            {QStringLiteral(" \\frac { a+b } {2} "), QStringLiteral("\\frac{a+b}{2}")},
            {QStringLiteral("x^{2}"), QStringLiteral("x^2")},
            {QStringLiteral("x^{10}"), QStringLiteral("x^{10}")},
            {QStringLiteral("x^2_i"), QStringLiteral("x_i^2")},
            {QStringLiteral("\\alpha x"), QStringLiteral("\\alpha x")},
            {QStringLiteral("\\sin{x}"), QStringLiteral("\\sin x")},
            {QStringLiteral("\\left( a \\right)"), QStringLiteral("\\left(a\\right)")},
            {QStringLiteral("{a+b}^2"), QStringLiteral("{a+b}^2")},
            {QStringLiteral("1 2"), QStringLiteral("1 2")},
            {QStringLiteral("f'"), QStringLiteral("f\\prime")},
            {QStringLiteral("\\sqrt[3]{x}"), QStringLiteral("\\sqrt[3]{x}")},
            {QStringLiteral("\\text{max} x"), QStringLiteral("\\text{max}x")},
            {QStringLiteral("\\left\\langle x \\right\\rangle"), QStringLiteral("\\left\\langle x\\right\\rangle")},
        };
        for (const auto &[input, canonical] : cases) {
            const Equation::Parsed first = Equation::parse(input);
            QVERIFY2(first.root.has_value(), qPrintable(input));
            QCOMPARE(Equation::canonicalSource(*first.root), canonical);
            // The canonical source parses to the same tree and is stable.
            const Equation::Parsed second = Equation::parse(canonical);
            QVERIFY2(second.root.has_value(), qPrintable(canonical));
            QVERIFY2(*second.root == *first.root, qPrintable(canonical));
            QCOMPARE(Equation::canonicalSource(*second.root), canonical);
        }
    }

    void linearTextIsReadable()
    {
        const QList<std::pair<QString, QString>> cases{
            {QStringLiteral("\\frac{a+b}{2}"), QStringLiteral("(a + b)/2")},
            {QStringLiteral("x^2"), QStringLiteral("x^2")},
            {QStringLiteral("\\sum_{i=1}^{n} i"), QStringLiteral("∑_(i = 1)^n i")},
            {QStringLiteral("\\sqrt{x^2+1}"), QStringLiteral("√(x^2 + 1)")},
            {QStringLiteral("E = mc^2"), QStringLiteral("E = mc^2")},
            {QStringLiteral("a = -b"), QStringLiteral("a = -b")},
            {QStringLiteral("\\sin x"), QStringLiteral("sin x")},
            {QStringLiteral("\\left(\\frac{1}{2}\\right)"), QStringLiteral("(1/2)")},
            {QStringLiteral("\\alpha\\leq\\beta"), QStringLiteral("α ≤ β")},
        };
        for (const auto &[input, linear] : cases) {
            const Equation::Parsed parsed = Equation::parse(input);
            QVERIFY2(parsed.root.has_value(), qPrintable(input));
            QCOMPARE(Equation::linearText(*parsed.root), linear);
        }
    }

    void unsupportedConstructsAreTypedErrors()
    {
        struct Case {
            QString source;
            ErrorCode code;
            int position;
        };
        const QList<Case> cases{
            {QStringLiteral("\\begin{matrix}a\\end{matrix}"), ErrorCode::Unsupported, 0},
            {QStringLiteral("a & b"), ErrorCode::Unsupported, 2},
            {QStringLiteral("\\mathbb{R}"), ErrorCode::Unsupported, 0},
            {QStringLiteral("x \\over y"), ErrorCode::Unsupported, 2},
            {QStringLiteral("\\frac{a}"), ErrorCode::InvalidArgument, 8},
            {QStringLiteral("x^^2"), ErrorCode::InvalidArgument, 2},
            {QStringLiteral("x^2^3"), ErrorCode::InvalidArgument, 3},
            {QStringLiteral("{a"), ErrorCode::InvalidArgument, 2},
            {QStringLiteral("a}"), ErrorCode::InvalidArgument, 1},
            {QStringLiteral("\\left( a"), ErrorCode::InvalidArgument, 8},
            {QString(5000, u'x'), ErrorCode::InvalidArgument, 0},
        };
        for (const Case &c : cases) {
            const Equation::Parsed parsed = Equation::parse(c.source);
            QVERIFY2(!parsed.root, qPrintable(c.source.left(40)));
            QCOMPARE(int(parsed.error.code), int(c.code));
            QCOMPARE(parsed.error.position, c.position);
            QVERIFY(!parsed.error.message.isEmpty());
        }
        const QString deep = QString(40, u'{') + u'x' + QString(40, u'}');
        QCOMPARE(int(Equation::parse(deep).error.code), int(ErrorCode::InvalidArgument));
    }

    void layoutPlacesKnownStructures()
    {
        // Fraction: numerator above the bar, denominator below, bar full width.
        const Equation::Box fraction = boxOf(QStringLiteral("\\frac{1}{2}"));
        QCOMPARE(fraction.glyphs.size(), 2);
        QCOMPARE(fraction.glyphs.at(0).text, QStringLiteral("1"));
        QCOMPARE(fraction.glyphs.at(1).text, QStringLiteral("2"));
        QCOMPARE(fraction.rules.size(), 1);
        const QRectF bar = fraction.rules.first();
        QVERIFY(fraction.glyphs.at(0).origin.y() < bar.top());
        QVERIFY(fraction.glyphs.at(1).origin.y() > bar.bottom());
        QCOMPARE(bar.width(), fraction.width);
        QVERIFY(fraction.ascent > 0 && fraction.descent > 0);
        // Superscript: 70 % size, raised, after the base; the base is italic.
        const Equation::Box power = boxOf(QStringLiteral("x^2"));
        QCOMPARE(power.glyphs.size(), 2);
        QVERIFY(power.glyphs.at(0).italic);
        QCOMPARE(power.glyphs.at(1).sizePt, 12 * 0.7);
        QVERIFY(power.glyphs.at(1).origin.y() < 0);
        QVERIFY(power.glyphs.at(1).origin.x() > 0);
        QVERIFY(boxOf(QStringLiteral("x_i")).glyphs.at(1).origin.y() > 0);
        // Radical: the sign ends where the overbar starts, above the radicand.
        const Equation::Box root = boxOf(QStringLiteral("\\sqrt{x}"));
        QCOMPARE(root.strokes.size(), 1);
        QCOMPARE(root.rules.size(), 1);
        QCOMPARE(root.rules.first().left(), root.strokes.first().last().x());
        QVERIFY(root.glyphs.first().origin.x() > root.rules.first().left());
        QVERIFY(root.rules.first().bottom() < -boxOf(QStringLiteral("x")).ascent);
        // Sum: limits centred above and below a 140 % glyph.
        const Equation::Box sum = boxOf(QStringLiteral("\\sum_{i=1}^{n}"));
        const Equation::Box sign = boxOf(QStringLiteral("\\sum"));
        QCOMPARE(sum.glyphs.size(), 5);
        QCOMPARE(sum.glyphs.at(0).text, QStringLiteral("∑"));
        QCOMPARE(sum.glyphs.at(0).sizePt, 12 * 1.4);
        QCOMPARE(sum.glyphs.at(1).text, QStringLiteral("n"));
        QVERIFY(sum.glyphs.at(1).origin.y() < -sign.ascent);
        QCOMPARE(sum.glyphs.at(2).text, QStringLiteral("i"));
        QVERIFY(sum.glyphs.at(2).origin.y() > sign.descent);
        // Integral: limits at the side, one above the other.
        const Equation::Box integral = boxOf(QStringLiteral("\\int_0^1"));
        QCOMPARE(integral.glyphs.size(), 3);
        QCOMPARE(integral.glyphs.at(1).origin.x(), integral.glyphs.at(2).origin.x());
        QVERIFY(integral.glyphs.at(1).origin.y() < integral.glyphs.at(2).origin.y());
        QVERIFY(integral.glyphs.at(1).origin.x() > 0);
    }

    void paintsInkInsideTheBox()
    {
        QImage image(200, 200, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        Equation::paint(&painter, QRectF(10, 10, 180, 180), boxOf(QStringLiteral("\\frac{1}{2}")), Qt::black);
        painter.end();
        // Numerator in the upper half, denominator in the lower, nothing outside the rect.
        QVERIFY(inkPixels(image, 10, 100) > 0);
        QVERIFY(inkPixels(image, 100, 190) > 0);
        QCOMPARE(inkPixels(image, 0, 8), 0);
        QCOMPARE(inkPixels(image, 192, 200), 0);
    }

    void equationsAreUndoableInlineObjects()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("Energy: "));
        const Paragraph *p = doc.paragraphsInOrder().first();
        const QString id = doc.insertEquation(sid(p->id), 8, QStringLiteral("E = mc^2"));
        QVERIFY(!id.isEmpty());
        const RichObject *object = doc.object(id);
        QVERIFY(object);
        QCOMPARE(object->kind, QStringLiteral("equation"));
        QCOMPARE(int(object->placement), int(Placement::Inline));
        QCOMPARE(object->source, QStringLiteral("E=mc^2"));
        QCOMPARE(object->altText, QStringLiteral("E = mc^2"));
        const Equation::Box box = boxOf(QStringLiteral("E=mc^2"), doc.defaultFontSize());
        QCOMPARE(object->widthPt, std::ceil(box.width * 100) / 100);
        QCOMPARE(object->heightPt, std::ceil(box.height() * 100) / 100);
        QCOMPARE(p->text, QStringLiteral("Energy: ") + QChar(ObjectChar));
        // It takes its width in the line like any inline object.
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        const qreal advance = layout.caretBox({p->id, 9}).rect.x() - layout.caretBox({p->id, 8}).rect.x();
        QVERIFY2(qAbs(advance - object->widthPt * PxPerPt) <= 1.0 / 16, qPrintable(QString::number(advance)));

        const QByteArray before = RichTextCodec::encode(doc);
        QVERIFY(doc.setEquationSource(id, QStringLiteral("\\frac{a}{b}")));
        QCOMPARE(doc.object(id)->source, QStringLiteral("\\frac{a}{b}"));
        QCOMPARE(doc.object(id)->heightPt, std::ceil(boxOf(QStringLiteral("\\frac{a}{b}"), doc.defaultFontSize()).height() * 100) / 100);
        const QVariantMap snapshot = doc.equationSnapshot(id);
        QCOMPARE(snapshot.value(QStringLiteral("status")).toString(), QStringLiteral("ready"));
        QCOMPARE(snapshot.value(QStringLiteral("linearText")).toString(), QStringLiteral("a/b"));
        const QVariantMap location = snapshot.value(QStringLiteral("location")).toMap();
        QCOMPARE(location.value(QStringLiteral("paragraphId")).toString(), sid(p->id));
        QCOMPARE(location.value(QStringLiteral("offset")).toInt(), 8);
        QCOMPARE(doc.equationSnapshots().size(), 1);
        QVERIFY(doc.undo());
        QCOMPARE(RichTextCodec::encode(doc), before);
        QVERIFY(doc.redo());
        QCOMPARE(doc.object(id)->source, QStringLiteral("\\frac{a}{b}"));

        // Refused sources change nothing and name their kind.
        const QByteArray current = RichTextCodec::encode(doc);
        QVERIFY(!doc.setEquationSource(id, QStringLiteral("\\begin{matrix}")));
        QCOMPARE(doc.lastErrorCode(), QStringLiteral("unsupported"));
        QVERIFY(doc.insertEquation(sid(p->id), 0, QStringLiteral("\\frac{a}")).isEmpty());
        QCOMPARE(doc.lastErrorCode(), QStringLiteral("invalidArgument"));
        QVERIFY(doc.insertEquation(sid(p->id), 0, QStringLiteral("  ")).isEmpty());
        QCOMPARE(RichTextCodec::encode(doc), current);
        const QVariantMap invalid = doc.validateEquation(QStringLiteral("x \\over y"));
        QCOMPARE(invalid.value(QStringLiteral("valid")).toBool(), false);
        QCOMPARE(invalid.value(QStringLiteral("code")).toString(), QStringLiteral("unsupported"));
        QCOMPARE(invalid.value(QStringLiteral("position")).toInt(), 2);
        QCOMPARE(doc.equationSourceWithSymbol(QStringLiteral("x"), 0, QStringLiteral("alpha")), QStringLiteral("\\alpha x"));
        QCOMPARE(doc.equationSourceWithSymbol(QStringLiteral("x"), 1, QStringLiteral("sup")), QStringLiteral("x^{}"));

        // Equations need no new schema version and reopen unchanged.
        QCOMPARE(RichTextCodec::requiredVersion(doc), 1);
        RichTextDocument reopened;
        QVERIFY(reopened.loadJson(QString::fromUtf8(current)));
        QCOMPARE(RichTextCodec::encode(reopened), current);
        QCOMPARE(reopened.object(id)->source, QStringLiteral("\\frac{a}{b}"));
    }

    void pagesDrawEquationsThemselves()
    {
        RichTextDocument doc;
        doc.insertText(doc.startPosition(), QStringLiteral("See "));
        const Paragraph *p = doc.paragraphsInOrder().first();
        const QString equation = doc.insertEquation(sid(p->id), 4, QStringLiteral("\\sqrt{2}"));
        QVERIFY(!equation.isEmpty());
        RichObject image;
        image.id = QStringLiteral("figure");
        QVERIFY(doc.insertObject({p->id, 0}, image));
        RichTextLayout layout;
        layout.setDocument(&doc);
        layout.paginateAll();
        QImage page(800, 1130, QImage::Format_ARGB32_Premultiplied);
        page.fill(Qt::white);
        QPainter painter(&page);
        RecordingRenderer renderer;
        PaintContext context;
        context.ink = Qt::black;
        context.border = Qt::gray;
        context.objects = &renderer;
        layout.paintPage(&painter, 0, context);
        painter.end();
        // The host renderer draws the image; the equation paints itself.
        QCOMPARE(renderer.painted, QStringList{QStringLiteral("figure")});
        QVERIFY(inkPixels(page, 0, page.height()) > 0);
    }
};

QTEST_MAIN(RichTextEquationsTest)
#include "tst_rich_text_equations.moc"
