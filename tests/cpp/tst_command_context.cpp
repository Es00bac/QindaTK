// SPDX-License-Identifier: LGPL-3.0-or-later
#include "command_context.h"

#include <QtTest>
#include <limits>

using namespace QindaTK;

class TstCommandContext final : public QObject {
    Q_OBJECT
private:
    static QVariantMap headerMap(const DocumentId &document, const QString &revision,
                                 const QString &reset = QStringLiteral("0"))
    {
        return {{QStringLiteral("schemaId"), QStringLiteral("scene.v1")},
                {QStringLiteral("documentId"), document.toString()},
                {QStringLiteral("revision"), revision},
                {QStringLiteral("resetGeneration"), reset}};
    }

private slots:
    void resolvesActiveTreeWithMostSpecificOverrides()
    {
        CommandContext context;
        QObject appOwner;
        QObject documentOwner;
        QObject viewOwner;
        const auto app = context.pushScope(QStringLiteral("application"), &appOwner);
        const auto doc = context.pushScope(QStringLiteral("document-one"), &documentOwner, app);
        const auto view = context.pushScope(QStringLiteral("canvas-view"), &viewOwner, doc);
        QVERIFY(!app.isEmpty());
        QVERIFY(!doc.isEmpty());
        QVERIFY(!view.isEmpty());

        const auto appDocument = DocumentId::create();
        const auto viewId = EntityId::create();
        const auto appSelection = EntityId::create();
        const auto viewSelection = EntityId::create();
        QVERIFY(context.updateScope(app, {
            {QStringLiteral("snapshotHeader"), headerMap(appDocument, QStringLiteral("3"))},
            {QStringLiteral("selectionIds"), QStringList{appSelection.toString()}},
            {QStringLiteral("focusedViewId"), QStringLiteral("")},
            {QStringLiteral("toolId"), QStringLiteral("select")}}));
        QVERIFY(context.updateScope(view, {
            {QStringLiteral("selectionIds"), QStringList{viewSelection.toString()}},
            {QStringLiteral("focusedViewId"), viewId.toString()},
            {QStringLiteral("toolId"), QStringLiteral("paint")}}));

        QVERIFY(context.activate(view));
        QCOMPARE(context.activePath(), QStringList({view, doc, app}));
        const auto snapshot = context.snapshot();
        QCOMPARE(snapshot.request.expected.documentId, appDocument);
        QCOMPARE(snapshot.request.expected.revision.value, quint64(3));
        QCOMPARE(snapshot.focusedView, viewId);
        QCOMPARE(snapshot.selection, QVector<EntityId>{viewSelection});
        QCOMPARE(snapshot.request.targets, snapshot.selection);
        QCOMPARE(snapshot.toolId, QStringLiteral("paint"));
        QCOMPARE(snapshot.focusScopeId, QStringLiteral("canvas-view"));
    }

    void switchingDocumentsSelectsIndependentSnapshots()
    {
        CommandContext context;
        QObject appOwner;
        QObject firstOwner;
        QObject secondOwner;
        const auto app = context.pushScope(QStringLiteral("application"), &appOwner);
        const auto first = context.pushScope(QStringLiteral("document-a"), &firstOwner, app);
        const auto second = context.pushScope(QStringLiteral("document-b"), &secondOwner, app);
        const auto docA = DocumentId::create();
        const auto docB = DocumentId::create();
        const auto selectionA = EntityId::create();
        const auto selectionB = EntityId::create();
        QVERIFY(context.updateScope(first, {
            {QStringLiteral("snapshotHeader"), headerMap(docA, QStringLiteral("10"))},
            {QStringLiteral("selectionIds"), QStringList{selectionA.toString()}}}));
        QVERIFY(context.updateScope(second, {
            {QStringLiteral("snapshotHeader"), headerMap(docB, QStringLiteral("20"))},
            {QStringLiteral("selectionIds"), QStringList{selectionB.toString()}}}));

        QVERIFY(context.activate(first));
        auto snapshot = context.snapshot();
        QCOMPARE(snapshot.request.expected.documentId, docA);
        QCOMPARE(snapshot.selection, QVector<EntityId>{selectionA});
        QVERIFY(context.activate(second));
        snapshot = context.snapshot();
        QCOMPARE(context.activeScopeId(), QStringLiteral("document-b"));
        QCOMPARE(snapshot.request.expected.documentId, docB);
        QCOMPARE(snapshot.request.expected.revision.value, quint64(20));
        QCOMPARE(snapshot.selection, QVector<EntityId>{selectionB});
        QVERIFY(context.activate(first));
        QCOMPARE(context.snapshot().request.expected.documentId, docA);
    }

    void explicitEmptySelectionAndToolOverrideInheritedValues()
    {
        CommandContext context;
        QObject appOwner;
        QObject childOwner;
        const auto app = context.pushScope(QStringLiteral("application"), &appOwner);
        const auto child = context.pushScope(QStringLiteral("editor"), &childOwner, app);
        const auto inherited = EntityId::create();
        QVERIFY(context.updateScope(app, {
            {QStringLiteral("selectionIds"), QStringList{inherited.toString()}},
            {QStringLiteral("toolId"), QStringLiteral("select")}}));
        QVERIFY(context.updateScope(child, {
            {QStringLiteral("selectionIds"), QStringList{}},
            {QStringLiteral("toolId"), QStringLiteral("")}}));
        QVERIFY(context.activate(child));
        const auto snapshot = context.snapshot();
        QVERIFY(snapshot.selection.isEmpty());
        QVERIFY(snapshot.request.targets.isEmpty());
        QVERIFY(snapshot.toolId.isEmpty());
    }

    void invalidUpdateIsAtomic()
    {
        CommandContext context;
        QObject owner;
        const auto token = context.pushScope(QStringLiteral("document"), &owner);
        const auto document = DocumentId::create();
        const auto originalSelection = EntityId::create();
        QVERIFY(context.updateScope(token, {
            {QStringLiteral("snapshotHeader"), headerMap(document, QStringLiteral("7"))},
            {QStringLiteral("selectionIds"), QStringList{originalSelection.toString()}},
            {QStringLiteral("toolId"), QStringLiteral("select")}}));
        const auto generation = context.generation();

        QVariantMap inaccurateHeader = headerMap(document, QStringLiteral("7"));
        inaccurateHeader[QStringLiteral("revision")] = QVariant::fromValue<qulonglong>(9007199254740993ULL);
        QVERIFY(!context.updateScope(token, {
            {QStringLiteral("selectionIds"), QStringList{}},
            {QStringLiteral("toolId"), QStringLiteral("erase")},
            {QStringLiteral("snapshotHeader"), inaccurateHeader}}));
        QCOMPARE(context.generation(), generation);
        auto snapshot = context.snapshot();
        QCOMPARE(snapshot.selection, QVector<EntityId>{originalSelection});
        QCOMPARE(snapshot.toolId, QStringLiteral("select"));
        QCOMPARE(snapshot.request.expected.revision.value, quint64(7));

        QVERIFY(!context.updateScope(token, {
            {QStringLiteral("toolId"), QStringLiteral("erase")},
            {QStringLiteral("selectionIds"), QStringList{QStringLiteral("bad-id")}}}));
        QCOMPARE(context.generation(), generation);
        snapshot = context.snapshot();
        QCOMPARE(snapshot.selection, QVector<EntityId>{originalSelection});
        QCOMPARE(snapshot.toolId, QStringLiteral("select"));
    }

    void qmlMapKeepsRevisionBeyondJavaScriptIntegerPrecision()
    {
        CommandContext context;
        QObject owner;
        const auto token = context.pushScope(QStringLiteral("document"), &owner);
        const auto document = DocumentId::create();
        const QString revision = QStringLiteral("9007199254740993");
        QVERIFY(context.updateScope(token, {
            {QStringLiteral("snapshotHeader"), headerMap(document, revision,
                                                         QStringLiteral("9007199254740995"))}}));
        const auto snapshot = context.snapshot();
        QCOMPARE(snapshot.request.expected.revision.value, quint64(9007199254740993ULL));
        QCOMPARE(snapshot.request.expected.resetGeneration, quint64(9007199254740995ULL));

        auto badHeader = headerMap(document, revision);
        badHeader[QStringLiteral("revision")] = 9007199254740993.0;
        QVERIFY(!context.updateScope(token, {{QStringLiteral("snapshotHeader"), badHeader}}));
        QCOMPARE(context.snapshot().request.expected.revision.value,
                 quint64(9007199254740993ULL));
    }

    void ownerDestructionRemovesDescendantsAndCancelsRequest()
    {
        CommandContext context;
        QObject appOwner;
        auto *documentOwner = new QObject;
        auto *viewOwner = new QObject;
        const auto app = context.pushScope(QStringLiteral("application"), &appOwner);
        const auto document = context.pushScope(QStringLiteral("document"), documentOwner, app);
        const auto view = context.pushScope(QStringLiteral("view"), viewOwner, document);
        QVERIFY(context.updateScope(app, {
            {QStringLiteral("toolId"), QStringLiteral("global")}}));
        QVERIFY(context.activate(view));
        const auto copiedRequest = context.snapshot().request;
        QVERIFY(!copiedRequest.cancellation.isCancelled());

        delete documentOwner;
        QVERIFY(!context.contains(document));
        QVERIFY(!context.contains(view));
        QVERIFY(!context.activate(view));
        QVERIFY(copiedRequest.cancellation.isCancelled());
        QCOMPARE(context.activeToken(), app);
        QCOMPARE(context.activeScopeId(), QStringLiteral("application"));
        QCOMPARE(context.snapshot().toolId, QStringLiteral("global"));

        delete viewOwner;
        QVERIFY(context.activate(app));
        QVERIFY(!context.activate(document));
        QVERIFY(!context.activate(view));
    }
};

QTEST_GUILESS_MAIN(TstCommandContext)
#include "tst_command_context.moc"
