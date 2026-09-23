// SPDX-License-Identifier: LGPL-3.0-or-later
#include "expression_evaluator.h"
#include "file_dialog_adapter.h"
#include "locale_service.h"
#include "property_binding.h"
#include "property_document.h"
#include "token_editor_service.h"
#include "undo_service.h"
#include "uri_validator.h"

#include <QTemporaryDir>
#include <QFile>
#include <QSignalSpy>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QScopedPointer>
#include <QHash>
#include <QtTest>

using namespace QindaTK;

class CompletionFixture final : public QObject {
    Q_OBJECT
public:
    Q_INVOKABLE QString request(const QString &query) {
        m_lastQuery = query;
        return QString::number(++m_serial);
    }
    QString lastId() const { return QString::number(m_serial); }
signals:
    void finished(const QString &requestId, const QVariantMap &result);
private:
    int m_serial = 0;
    QString m_lastQuery;
};

class ManualDialogBackend final : public FileDialogBackend {
public:
    bool available() const override { return true; }
    bool supportsRemoteUrls() const override { return true; }
    void start(const QString &id, const FileDialogRequest &, QWindow *,
               std::function<void(FileDialogResult)> completion) override {
        pending.insert(id, std::move(completion));
    }
    void cancel(const QString &id) override { pending.remove(id); }
    void accept(const QString &id, const QUrl &url) {
        const auto callback = pending.take(id);
        if (callback) callback({FileDialogResult::Status::Accepted, {url}, {}});
    }
    QHash<QString, std::function<void(FileDialogResult)>> pending;
};

class TstExpressionEvaluator final : public QObject {
    Q_OBJECT
private slots:
    void arithmeticUnitsAndScopedSymbols() {
        ExpressionEvaluator evaluator;
        evaluator.setUnits({{"mm", QVariantMap{{"scale", 1.0}, {"dimension", "length"}}},
                            {"cm", QVariantMap{{"scale", 10.0}, {"dimension", "length"}}},
                            {"s", QVariantMap{{"scale", 1.0}, {"dimension", "time"}}}});
        evaluator.setSymbols({{"base", QVariantMap{{"value", 2.0}, {"unit", "cm"}}},
                              {"twice", QStringLiteral("base * 2")}});
        const auto valid = evaluator.evaluate(QStringLiteral("clamp(twice + 5mm; 0mm; 50mm)"));
        QVERIFY(valid.value("ok").toBool());
        QCOMPARE(valid.value("value").toDouble(), 45.0);
        QCOMPARE(valid.value("unit").toString(), QStringLiteral("length"));
        QVERIFY(!evaluator.evaluate(QStringLiteral("5mm + 2s")).value("ok").toBool());
        QVERIFY(!evaluator.evaluate(QStringLiteral("2mm * 3cm")).value("ok").toBool());
        QVERIFY(!evaluator.evaluate(QStringLiteral("1 / 0")).value("ok").toBool());
    }
    void localeErrorsCyclesAndBudgets() {
        LocaleService locale;
        locale.setLocaleName(QStringLiteral("de_DE"));
        ExpressionEvaluator evaluator;
        evaluator.setLocaleService(&locale);
        const auto valid = evaluator.evaluate(QStringLiteral("max(1,5;2,25)"));
        QVERIFY(valid.value("ok").toBool());
        QCOMPARE(valid.value("value").toDouble(), 2.25);
        evaluator.setSymbols({{"a", QStringLiteral("b + 1")}, {"b", QStringLiteral("a + 1")}});
        const auto cycle = evaluator.evaluate(QStringLiteral("a"));
        QVERIFY(!cycle.value("ok").toBool());
        QVERIFY(cycle.value("error").toString().contains(QStringLiteral("Cyclic")));
        QCOMPARE(cycle.value("offset").toInt(), 0);
        QVERIFY(!evaluator.evaluate(QStringLiteral("1 + (2" )).value("ok").toBool());
        QVERIFY(!evaluator.evaluate(QString(513, QLatin1Char('1'))).value("ok").toBool());
        QVERIFY(!evaluator.evaluate(QStringLiteral("unknown(1)")).value("ok").toBool());
        QVERIFY(!evaluator.evaluate(QStringLiteral("(((((((((((((((((1)))))))))))))))))")).value("ok").toBool());
    }
    void uriAndTokenProviders() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("present.txt"));
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("x"); file.close();
        UriValidator validator;
        const auto local = validator.inspect(path, QStringLiteral("open"));
        QVERIFY(local.value("valid").toBool());
        QVERIFY(local.value("uri").toString().startsWith(QStringLiteral("file://")));
        const auto remote = validator.inspect(QStringLiteral("sftp://host/a%20b"));
        QCOMPARE(remote.value("status").toString(), QStringLiteral("unknown"));
        QCOMPARE(remote.value("uri").toString(), QStringLiteral("sftp://host/a%20b"));
        QVERIFY(!validator.inspect(directory.filePath(QStringLiteral("missing"))).value("valid").toBool());
        QSignalSpy spy(&validator, &UriValidator::finished);
        const QString canceled = validator.request(path);
        QVERIFY(validator.cancel(canceled));
        QCoreApplication::processEvents();
        QCOMPARE(spy.size(), 0);
        const QString live = validator.request(path);
        QTRY_COMPARE(spy.size(), 1);
        QCOMPARE(spy.first().at(0).toString(), live);

        TokenEditorService tokens;
        const auto parsed = tokens.splitText(QStringLiteral("one,\"two, three\";four\nfive"));
        QVERIFY(parsed.value("ok").toBool());
        QCOMPARE(parsed.value("labels").toList().size(), 4);
        const auto first = tokens.createToken(QStringLiteral("Name"), QStringLiteral("entity:1"));
        const auto second = tokens.createToken(QStringLiteral("Name"), QStringLiteral("entity:1"));
        QVERIFY(!first.value("id").toString().isEmpty());
        QVERIFY(first.value("id") != second.value("id"));
        QVERIFY(!tokens.splitText(QStringLiteral("\"unclosed")).value("ok").toBool());
        QVERIFY(!tokens.splitText(QString(8193, QLatin1Char('x'))).value("ok").toBool());
    }
    void tokenFieldCommitsOnceAndRejectsLateCompletion() {
        PropertyDocument document;
        const EntityId entity = EntityId::create();
        QVERIFY(document.defineProperty({QStringLiteral("recipients"), QMetaType::fromType<QVariantList>(),
                                         QVariantList{}, QStringLiteral("tokens"), true, false}));
        QVERIFY(document.addEntity(entity, {{QStringLiteral("recipients"), QVariantList{}}}));
        UndoService undo;
        QVERIFY(undo.addDocument(&document));
        PropertyBinding binding;
        binding.setDocument(&document);
        binding.setUndoService(&undo);
        binding.setTargetIds({entity.toString()});
        binding.setPropertyId(QStringLiteral("recipients"));
        CompletionFixture provider;
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        engine.rootContext()->setContextProperty(QStringLiteral("testBinding"), &binding);
        engine.rootContext()->setContextProperty(QStringLiteral("testProvider"), &provider);
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport QindaTK as Tk\n"
                          "Tk.TokenField { binding: testBinding; completionProvider: testProvider; label: 'Recipients' }",
                          QUrl());
        QScopedPointer<QObject> field(component.create());
        QVERIFY2(field, qPrintable(component.errorString()));
        engine.rootContext()->setContextProperty(QStringLiteral("field"), field.data());
        auto run = [&](const QString &code) {
            QQmlExpression expression(engine.rootContext(), field.data(), code);
            const QVariant result = expression.evaluate();
            if (expression.hasError()) qWarning().noquote() << expression.error().toString();
            return result;
        };
        const bool added = run(QStringLiteral("field.appendCandidates([{label:'Alpha',value:'person:1'},"
                                                "{label:'Beta',value:'person:2'}])")).toBool();
        QVERIFY(added);
        const auto committed = document.committedValue(entity, QStringLiteral("recipients")).toList();
        QCOMPARE(committed.size(), 2);
        const auto first = committed.at(0).toMap();
        QVERIFY(!first.value(QStringLiteral("id")).toString().isEmpty());
        QVERIFY(run(QStringLiteral("field.move(0, 1)")).toBool());
        const auto reordered = document.committedValue(entity, QStringLiteral("recipients")).toList();
        QCOMPARE(reordered.at(1).toMap().value(QStringLiteral("id")), first.value(QStringLiteral("id")));
        QVERIFY(undo.undo());
        QCOMPARE(document.committedValue(entity, QStringLiteral("recipients")).toList().at(0).toMap().value(QStringLiteral("id")),
                 first.value(QStringLiteral("id")));
        field->setProperty("draft", QStringLiteral("Al"));
        run(QStringLiteral("field.queryCompletion()"));
        const QString stale = provider.lastId();
        field->setProperty("draft", QStringLiteral("Be"));
        run(QStringLiteral("field.queryCompletion()"));
        const QString current = provider.lastId();
        emit provider.finished(stale, {{"candidates", QVariantList{QVariantMap{{"label", "Old"}}}}});
        QCOMPARE(field->property("suggestions").toList().size(), 0);
        emit provider.finished(current, {{"candidates", QVariantList{QVariantMap{{"label", "New"}}}}});
        QCOMPARE(field->property("suggestions").toList().size(), 1);
        QVERIFY(run(QStringLiteral("field.appendCandidates([{label:'New',value:'person:3'}])")).toBool());
        emit provider.finished(current, {{"candidates", QVariantList{QVariantMap{{"label", "Late"}}}}});
        QCOMPARE(field->property("suggestions").toList().size(), 0);
    }
    void filePathBrowseCancellationAndStaleEdit() {
        PropertyDocument document;
        const EntityId entity = EntityId::create();
        QVERIFY(document.defineProperty({QStringLiteral("uri"), QMetaType::fromType<QString>(),
                                         QString{}, QStringLiteral("uri"), true, false}));
        QVERIFY(document.addEntity(entity, {{QStringLiteral("uri"), QStringLiteral("sftp://host/original")}}));
        UndoService undo;
        QVERIFY(undo.addDocument(&document));
        PropertyBinding binding;
        binding.setDocument(&document);
        binding.setUndoService(&undo);
        binding.setTargetIds({entity.toString()});
        binding.setPropertyId(QStringLiteral("uri"));
        auto backend = std::make_shared<ManualDialogBackend>();
        FileDialogAdapter dialog;
        dialog.setBackend(backend);
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
        engine.rootContext()->setContextProperty(QStringLiteral("testBinding"), &binding);
        engine.rootContext()->setContextProperty(QStringLiteral("testDialog"), &dialog);
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport QindaTK as Tk\n"
                          "Tk.FilePathField { binding: testBinding; dialogAdapter: testDialog; label: 'Path' }",
                          QUrl());
        QScopedPointer<QObject> field(component.create());
        QVERIFY2(field, qPrintable(component.errorString()));
        engine.rootContext()->setContextProperty(QStringLiteral("field"), field.data());
        auto run = [&](const QString &code) {
            QQmlExpression expression(engine.rootContext(), field.data(), code);
            const QVariant result = expression.evaluate();
            if (expression.hasError()) qWarning().noquote() << expression.error().toString();
            return result;
        };
        const QString first = run(QStringLiteral("field.browse()")).toString();
        QVERIFY(!first.isEmpty());
        QVERIFY(run(QStringLiteral("field.cancelBrowse(); true")).toBool());
        QCOMPARE(document.committedValue(entity, QStringLiteral("uri")).toString(), QStringLiteral("sftp://host/original"));
        const QString stale = run(QStringLiteral("field.browse()")).toString();
        QVERIFY(!stale.isEmpty());
        field->setProperty("draft", QStringLiteral("sftp://host/manual"));
        run(QStringLiteral("field.setDraft(field.draft)"));
        backend->accept(stale, QUrl(QStringLiteral("sftp://host/stale")));
        QCOMPARE(document.committedValue(entity, QStringLiteral("uri")).toString(), QStringLiteral("sftp://host/original"));
        const QString current = run(QStringLiteral("field.browse()")).toString();
        QVERIFY(!current.isEmpty());
        backend->accept(current, QUrl(QStringLiteral("sftp://host/accepted%20path")));
        QCOMPARE(document.committedValue(entity, QStringLiteral("uri")).toString(), QStringLiteral("sftp://host/accepted%20path"));
        QVERIFY(undo.undo());
        QCOMPARE(document.committedValue(entity, QStringLiteral("uri")).toString(), QStringLiteral("sftp://host/original"));
    }
};
QTEST_MAIN(TstExpressionEvaluator)
#include "tst_expression_evaluator.moc"
