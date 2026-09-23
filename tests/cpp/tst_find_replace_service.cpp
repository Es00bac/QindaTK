// SPDX-License-Identifier: LGPL-3.0-or-later
#include "find_replace_service.h"

#include <QQuickItem>
#include <QQuickTextDocument>
#include <QTextDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QtTest>
#include <memory>

using namespace QindaTK;

class TstFindReplaceService final : public QObject {
    Q_OBJECT
private slots:
    void searchesScopedTextAndGroupsReplaceAllUndo()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nTextEdit { text: 'alpha beta alpha' }", {});
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> editor(component.create());
        QVERIFY(editor != nullptr);
        auto *wrapper = editor->property("textDocument").value<QQuickTextDocument *>();
        QVERIFY(wrapper != nullptr);
        auto *document = wrapper->textDocument();
        QVERIFY(document != nullptr);
        FindReplaceService service;
        service.setTextDocument(wrapper);
        QSignalSpy ready(&service, &FindReplaceService::resultsReady);

        const auto request = service.search(QStringLiteral("alpha"));
        QVERIFY(!request.isEmpty());
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(service.matchCount(), 2);
        QCOMPARE(service.results().at(1).toMap().value(QStringLiteral("start")).toInt(), 11);
        const int before = service.revision();
        QVERIFY(service.setCurrentMatch(1));
        const auto single = service.replaceCurrent(before, QStringLiteral("X"));
        QVERIFY(single.value(QStringLiteral("ok")).toBool());
        QCOMPARE(single.value(QStringLiteral("replaced")).toInt(), 1);
        QCOMPARE(document->toPlainText(), QStringLiteral("alpha beta X"));
        document->undo();
        QCOMPARE(document->toPlainText(), QStringLiteral("alpha beta alpha"));

        ready.clear();
        service.search(QStringLiteral("alpha"));
        QTRY_COMPARE(ready.size(), 1);
        const auto all = service.replaceAll(service.revision(), QStringLiteral("Q"));
        QVERIFY(all.value(QStringLiteral("ok")).toBool());
        QCOMPARE(all.value(QStringLiteral("replaced")).toInt(), 2);
        QCOMPARE(document->toPlainText(), QStringLiteral("Q beta Q"));
        document->undo();
        QCOMPARE(document->toPlainText(), QStringLiteral("alpha beta alpha"));

        ready.clear();
        service.search(QStringLiteral("alpha"), {},
                       {{QStringLiteral("start"), 6}, {QStringLiteral("length"), 10}});
        QTRY_COMPARE(ready.size(), 1);
        QCOMPARE(service.matchCount(), 1);
        QCOMPARE(service.results().first().toMap().value(QStringLiteral("start")).toInt(), 11);
    }

    void rejectsStaleReadOnlyCompositionAndInvalidPattern()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nTextEdit { text: 'one two one' }", {});
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> editor(component.create());
        QVERIFY(editor != nullptr);
        auto *wrapper = editor->property("textDocument").value<QQuickTextDocument *>();
        QVERIFY(wrapper != nullptr);
        auto *document = wrapper->textDocument();
        QVERIFY(document != nullptr);
        FindReplaceService service;
        service.setTextDocument(wrapper);
        QSignalSpy ready(&service, &FindReplaceService::resultsReady);
        service.search(QStringLiteral("one"));
        QTRY_COMPARE(ready.size(), 1);
        const int revision = service.revision();
        service.setReadOnly(true);
        QVERIFY(!service.replaceAll(revision, QStringLiteral("x")).value(QStringLiteral("ok")).toBool());
        service.setReadOnly(false);
        service.setComposing(true);
        QVERIFY(!service.replaceCurrent(revision, QStringLiteral("x")).value(QStringLiteral("ok")).toBool());
        service.setComposing(false);
        document->setPlainText(QStringLiteral("changed"));
        QVERIFY(!service.replaceAll(revision, QStringLiteral("x")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(document->toPlainText(), QStringLiteral("changed"));

        ready.clear();
        service.search(QStringLiteral("("), {{QStringLiteral("pattern"), true}});
        QTRY_COMPARE(ready.size(), 1);
        QVERIFY(!service.lastError().isEmpty());
        QCOMPARE(service.matchCount(), 0);
    }

    void cancellationAndDocumentEditsDiscardLateSearchResults()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nTextEdit { text: 'alpha beta alpha' }", {});
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> editor(component.create());
        QVERIFY(editor != nullptr);
        auto *wrapper = editor->property("textDocument").value<QQuickTextDocument *>();
        QVERIFY(wrapper != nullptr);
        FindReplaceService service;
        service.setTextDocument(wrapper);
        QSignalSpy ready(&service, &FindReplaceService::resultsReady);

        const auto cancelled = service.search(QStringLiteral("alpha"));
        QVERIFY(service.cancelSearch(cancelled));
        QTest::qWait(30);
        QCOMPARE(ready.size(), 0);
        QCOMPARE(service.matchCount(), 0);
        QVERIFY(!service.busy());

        service.search(QStringLiteral("alpha"));
        wrapper->textDocument()->setPlainText(QStringLiteral("beta"));
        QTest::qWait(30);
        QCOMPARE(ready.size(), 0);
        QCOMPARE(service.matchCount(), 0);
        QVERIFY(!service.busy());
    }
};

QTEST_MAIN(TstFindReplaceService)
#include "tst_find_replace_service.moc"
