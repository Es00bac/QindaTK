// SPDX-License-Identifier: LGPL-3.0-or-later
// D-319: HTML import (paste, notebook HTML, RichTextHtml::toFragment) is
// hostile input. No image source survives that the engine would load from
// the network or the disk, and the parser itself loads nothing.
#include "rich_text_html.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextDocument>
#include <QtTest>

using namespace QindaTK;
using namespace QindaTK::RichText;

namespace {
QList<RichObject> objectsOf(const Fragment &fragment)
{
    QList<RichObject> out;
    for (const Block &block : fragment.blocks) {
        if (!block.paragraph) continue;
        int at = 0;
        for (const Span &span : block.paragraph->spans) {
            const CharFormat format = fragment.formats.value(span.format);
            for (int i = 0; i < span.length; ++i)
                if (block.paragraph->text.at(at + i) == QChar(ObjectChar))
                    out.append(fragment.objects.value(format.objectId));
            at += span.length;
        }
    }
    return out;
}
} // namespace

class TstRichTextHtmlImport : public QObject {
    Q_OBJECT
private slots:
    void cleanup()
    {
        for (const QString &scheme : RichTextHtml::registeredImageSchemes())
            RichTextHtml::unregisterImageScheme(scheme);
    }

    void hostileImageSourcesBecomePlaceholders()
    {
        const QString html = QStringLiteral(
            "<p>a<img src=\"https://tracker.example/p.png?u=1\" alt=\"remote\" width=\"10\" height=\"10\">"
            "b<img src=\"http://tracker.example/x.gif\">"
            "c<img src=\"file:///etc/passwd\" alt=\"local\">"
            "d<img src=\"//tracker.example/protocol-relative.png\">"
            "e<img src=\"relative/picture.png\">"
            "f<img srcset=\"https://tracker.example/hi.png 2x, https://tracker.example/lo.png 1x\" alt=\"set\">"
            "g<img src=\"https://tracker.example/fallback.png\" srcset=\"https://tracker.example/hi.png 2x\">"
            "h<img src=\" FILE:/home/user/secret.png \">"
            "i<img src=\"qrc:/qt-project.org/styles/logo.png\">"
            "j<img src=\"data:image/svg+xml;base64,PHN2Zz48L3N2Zz4=\">"
            "k<img src=\"javascript:alert(1)\">"
            "l<img src=\"&#x66;ile:///etc/shadow\"></p>");
        const Fragment fragment = RichTextHtml::toFragment(html);
        const QList<RichObject> objects = objectsOf(fragment);
        QCOMPARE(objects.size(), 12);
        for (const RichObject &object : objects) {
            QVERIFY2(object.source.isEmpty(), qPrintable(object.source));
            QCOMPARE(object.kind, QStringLiteral("image"));
        }
        // The placeholders keep their alt text and size, never the address.
        QCOMPARE(objects.at(0).altText, QStringLiteral("remote"));
        QCOMPARE(objects.at(2).altText, QStringLiteral("local"));
        QCOMPARE(objects.at(5).altText, QStringLiteral("set"));
        QCOMPARE(objects.at(0).widthPt, 10 / PxPerPt);
        for (auto it = fragment.objects.cbegin(); it != fragment.objects.cend(); ++it) {
            QVERIFY(!it->source.contains(QLatin1String("tracker")));
            QVERIFY(!it->source.contains(QLatin1String("passwd")));
            QVERIFY(!it->source.contains(QLatin1String("secret")));
        }
        QCOMPARE(fragment.blocks.first().paragraph->text.count(QChar(ObjectChar)), 12);
    }

    void dataUrisAndRegisteredSchemesSurvive()
    {
        const QString png = QStringLiteral("data:image/png;base64,iVBORw0KGgo=");
        const QString html = QStringLiteral("<p><img src=\"%1\" alt=\"dot\"><img src=\"asset://library/cover-1\"></p>").arg(png);
        QList<RichObject> objects = objectsOf(RichTextHtml::toFragment(html));
        QCOMPARE(objects.size(), 2);
        QCOMPARE(objects.at(0).source, png);
        QCOMPARE(objects.at(0).altText, QStringLiteral("dot"));
        QVERIFY(objects.at(1).source.isEmpty());

        RichTextHtml::registerImageScheme(QStringLiteral(" Asset "));
        QCOMPARE(RichTextHtml::registeredImageSchemes(), QStringList{QStringLiteral("asset")});
        objects = objectsOf(RichTextHtml::toFragment(html));
        QCOMPARE(objects.at(1).source, QStringLiteral("asset://library/cover-1"));
        RichTextHtml::unregisterImageScheme(QStringLiteral("asset"));
        QVERIFY(objectsOf(RichTextHtml::toFragment(html)).at(1).source.isEmpty());
    }

    void sourceRules_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<bool>("importable");
        QTest::newRow("png data") << QStringLiteral("data:image/png;base64,AAAA") << true;
        QTest::newRow("jpeg data, upper case") << QStringLiteral("DATA:image/JPEG;base64,AAAA") << true;
        QTest::newRow("svg data") << QStringLiteral("data:image/svg+xml,<svg/>") << false;
        QTest::newRow("html data") << QStringLiteral("data:text/html,<b>x</b>") << false;
        QTest::newRow("https") << QStringLiteral("https://example.org/a.png") << false;
        QTest::newRow("http") << QStringLiteral("http://example.org/a.png") << false;
        QTest::newRow("ftp") << QStringLiteral("ftp://example.org/a.png") << false;
        QTest::newRow("file") << QStringLiteral("file:///tmp/a.png") << false;
        QTest::newRow("protocol relative") << QStringLiteral("//example.org/a.png") << false;
        QTest::newRow("relative") << QStringLiteral("images/a.png") << false;
        QTest::newRow("absolute path") << QStringLiteral("/tmp/a.png") << false;
        QTest::newRow("qrc") << QStringLiteral("qrc:/a.png") << false;
        QTest::newRow("windows drive") << QStringLiteral("C:\\\\Users\\\\a.png") << false;
        QTest::newRow("empty") << QString() << false;
        QTest::newRow("unregistered asset") << QStringLiteral("asset:cover") << false;
    }
    void sourceRules()
    {
        QFETCH(QString, source);
        QFETCH(bool, importable);
        QCOMPARE(RichTextHtml::isImportableImageSource(source), importable);
    }

    // The parser loads nothing: a local style sheet named by <link> would
    // turn the paragraph red if QTextDocument were allowed to read it.
    void importLoadsNoResource()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile css(dir.filePath(QStringLiteral("style.css")));
        QVERIFY(css.open(QIODevice::WriteOnly));
        css.write("p { color: #ff0000; }");
        css.close();
        const QString html = QStringLiteral("<html><head><link rel=\"stylesheet\" type=\"text/css\" href=\"%1\"></head>"
                                            "<body><p>styled?</p></body></html>")
                                 .arg(QUrl::fromLocalFile(css.fileName()).toString());
        QTextDocument plain;
        plain.setHtml(html);
        const QColor loaded = plain.begin().begin().fragment().charFormat().foreground().color();
        QCOMPARE(loaded, QColor(QStringLiteral("#ff0000"))); // the attack works on a plain QTextDocument

        const Fragment fragment = RichTextHtml::toFragment(html);
        QCOMPARE(fragment.blocks.size(), 1);
        const Paragraph &paragraph = *fragment.blocks.first().paragraph;
        QCOMPARE(paragraph.text, QStringLiteral("styled?"));
        for (const Span &span : paragraph.spans)
            QVERIFY(fragment.formats.value(span.format).color != QColor(QStringLiteral("#ff0000")));
    }
};

QTEST_MAIN(TstRichTextHtmlImport)
#include "tst_rich_text_html_import.moc"
