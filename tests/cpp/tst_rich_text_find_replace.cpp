// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rich_text_document.h"
#include "rich_text_find_replace_service.h"
#include <QSignalSpy>
#include <QtTest>

using namespace QindaTK;

class RichTextFindReplaceTest : public QObject {
    Q_OBJECT
private slots:
    void replaceAllIsOneDocumentTransaction()
    {
        RichTextDocument document;
        QVERIFY(document.insertText(document.startPosition(), QStringLiteral("Blue blue\nblue")));
        RichTextFindReplaceService service;
        service.setDocument(&document);
        QSignalSpy ready(&service, &RichTextFindReplaceService::resultsReady);
        const QString request = service.search(QStringLiteral("blue"));
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(ready.first().at(0).toString(), request);
        QCOMPARE(service.matchCount(), 3);
        QCOMPARE(service.results().at(2).toMap().value(QStringLiteral("start")).toInt(), 10);
        const int revision = service.revision();
        const auto result = service.replaceAll(revision, QStringLiteral("red"));
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(result.value(QStringLiteral("replaced")).toInt(), 3);
        QCOMPARE(document.text(), QStringLiteral("red red\nred"));
        QCOMPARE(service.matchCount(), 0);
        QVERIFY(document.undo());
        QCOMPARE(document.text(), QStringLiteral("Blue blue\nblue"));
        QVERIFY(service.replaceAll(revision, QStringLiteral("wrong"))
                    .value(QStringLiteral("ok")).toBool() == false);
    }

    void scopedSearchRespectsParagraphAndGraphemeBoundaries()
    {
        RichTextDocument document;
        QVERIFY(document.insertText(document.startPosition(), QStringLiteral("cat\ncat")));
        RichTextFindReplaceService service;
        service.setDocument(&document);
        QSignalSpy ready(&service, &RichTextFindReplaceService::resultsReady);
        service.search(QStringLiteral("cat"), {}, {{QStringLiteral("start"), 4},
                                                    {QStringLiteral("length"), 3}});
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(service.matchCount(), 1);
        QCOMPARE(service.results().first().toMap().value(QStringLiteral("start")).toInt(), 4);
        QVERIFY(service.replaceCurrent(service.revision(), QStringLiteral("dog"))
                    .value(QStringLiteral("ok")).toBool());
        QCOMPARE(document.text(), QStringLiteral("cat\ndog"));
    }

    void offsetsMatchTheEditorAcrossObjectAnchors()
    {
        RichTextDocument document;
        QVERIFY(document.insertText(document.startPosition(), QStringLiteral("ab")));
        RichText::RichObject object;
        object.widthPt = 12;
        object.heightPt = 12;
        const auto paragraph = document.paragraphsInOrder().first();
        QVERIFY(document.insertObject({paragraph->id, 1}, object));
        RichTextFindReplaceService service;
        service.setDocument(&document);
        QSignalSpy ready(&service, &RichTextFindReplaceService::resultsReady);
        service.search(QStringLiteral("b"));
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(service.matchCount(), 1);
        QCOMPARE(service.results().first().toMap().value(QStringLiteral("start")).toInt(), 2);
        QVERIFY(service.replaceCurrent(service.revision(), QStringLiteral("c"))
                    .value(QStringLiteral("ok")).toBool());
        QCOMPARE(document.text(), QStringLiteral("ac"));
        QCOMPARE(document.objects().size(), 1);
    }
};

QTEST_MAIN(RichTextFindReplaceTest)
#include "tst_rich_text_find_replace.moc"
