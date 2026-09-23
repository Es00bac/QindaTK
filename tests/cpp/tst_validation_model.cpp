// SPDX-License-Identifier: LGPL-3.0-or-later
#include "validation_model.h"

#include <QtTest>

using namespace QindaTK;

class TstValidationModel final : public QObject {
    Q_OBJECT
private:
    static SnapshotHeader snapshot(quint64 revision, quint64 generation = 0)
    {
        return {QStringLiteral("test.document"), DocumentId::create(),
                RevisionId{revision}, generation};
    }

    static ValidationEntry entry(ValidationModel::Severity severity,
                                 const QString &code, const QString &path,
                                 const EntityId &target = {})
    {
        return {code, severity, target, path, code + QStringLiteral(" message"),
                QStringLiteral("Review the value")};
    }

private slots:
    void beginRunIsPendingAndQmlIntegersAreDecimalStrings()
    {
        ValidationModel model;
        const auto document = DocumentId::create();
        QVERIFY(model.setSnapshot(document.toString(), QStringLiteral("18446744073709551614"),
                                  QStringLiteral("9007199254740993")));
        const QString run = model.beginRun();
        QVERIFY(!run.isEmpty());
        QVERIFY(model.pending());
        QVERIFY(!model.invalid());
        QCOMPARE(model.revision(), QStringLiteral("18446744073709551614"));
        QCOMPARE(model.resetGeneration(), QStringLiteral("9007199254740993"));
        QCOMPARE(model.runId(), run);
        QVERIFY(!model.completeRun(run, document.toString(), QStringLiteral("2"),
                                   QStringLiteral("9007199254740993"), {}));
        QVERIFY(model.pending());
        QVERIFY(model.completeRun(run, document.toString(),
                                  QStringLiteral("18446744073709551614"),
                                  QStringLiteral("9007199254740993"), {}));
        QVERIFY(!model.pending());
    }

    void staleCompletionCannotReplaceOrBlockNewerRun()
    {
        ValidationModel model;
        const auto oldSnapshot = snapshot(4);
        QVERIFY(model.setSnapshot(oldSnapshot));
        const quint64 oldRun = model.beginRun(oldSnapshot);
        QVERIFY(oldRun != 0);

        auto current = oldSnapshot;
        current.revision.value = 5;
        QVERIFY(model.setSnapshot(current));
        const quint64 currentRun = model.beginRun(current);
        QVERIFY(currentRun > oldRun);
        QVERIFY(model.pending());

        QVERIFY(!model.completeRun(oldRun, oldSnapshot,
                                   {entry(ValidationModel::Error, QStringLiteral("old"),
                                          QStringLiteral("name"))}));
        QVERIFY(model.pending());
        QVERIFY(model.issues().isEmpty());
        QVERIFY(model.completeRun(currentRun, current, {}));
        QVERIFY(!model.pending());
        QVERIFY(!model.invalid());
        QVERIFY(model.issues().isEmpty());
    }

    void resetGenerationInvalidatesResultsEvenWhenRevisionRepeats()
    {
        ValidationModel model;
        const auto beforeReset = snapshot(8, 2);
        QVERIFY(model.setSnapshot(beforeReset));
        const quint64 run = model.beginRun(beforeReset);

        auto afterReset = beforeReset;
        afterReset.resetGeneration = 3;
        QVERIFY(model.setSnapshot(afterReset));
        QVERIFY(!model.pending());
        QVERIFY(!model.completeRun(run, beforeReset,
                                   {entry(ValidationModel::Error, QStringLiteral("stale"),
                                          QStringLiteral("source"))}));
        QVERIFY(model.issues().isEmpty());
    }

    void errorsWarningsAndInfoExposeFirstRelevantTarget()
    {
        ValidationModel model;
        const auto current = snapshot(1);
        QVERIFY(model.setSnapshot(current));
        const auto firstTarget = EntityId::create();
        const auto errorTarget = EntityId::create();
        const auto run = model.beginRun(current);
        QVERIFY(model.completeRun(run, current,
            {entry(ValidationModel::Warning, QStringLiteral("warn"),
                   QStringLiteral("appearance.opacity"), firstTarget),
             entry(ValidationModel::Error, QStringLiteral("invalid"),
                   QStringLiteral("audio.gain"), errorTarget),
             entry(ValidationModel::Info, QStringLiteral("info"),
                   QStringLiteral("metadata"))}));

        QVERIFY(model.invalid());
        QCOMPARE(model.issues().size(), 3);
        QCOMPARE(model.firstIssueTargetId(), errorTarget.toString());
        QCOMPARE(model.firstIssuePath(), QStringLiteral("audio.gain"));
        QCOMPARE(model.firstIssue().value(QStringLiteral("severity")).toString(),
                 QStringLiteral("error"));

        const auto nextRun = model.beginRun(current);
        QVERIFY(model.pending());
        QVERIFY(!model.invalid());
        QVERIFY(model.issues().isEmpty());
        QVERIFY(model.completeRun(nextRun, current,
            {entry(ValidationModel::Info, QStringLiteral("note"),
                   QStringLiteral("metadata"))}));
        QVERIFY(!model.invalid());
        QCOMPARE(model.firstIssuePath(), QStringLiteral("metadata"));
    }

    void clearCancelsPendingAndRemovesCurrentIssues()
    {
        ValidationModel model;
        const auto current = snapshot(12);
        QVERIFY(model.setSnapshot(current));
        const auto run = model.beginRun(current);
        model.clear();
        QVERIFY(!model.pending());
        QVERIFY(!model.completeRun(run, current,
                                   {entry(ValidationModel::Error, QStringLiteral("late"),
                                          QStringLiteral("field"))}));

        const auto next = model.beginRun(current);
        QVERIFY(model.completeRun(next, current,
            {entry(ValidationModel::Error, QStringLiteral("bad"), QStringLiteral("field"))}));
        QVERIFY(model.invalid());
        model.clear();
        QVERIFY(!model.invalid());
        QVERIFY(model.issues().isEmpty());
        QVERIFY(model.firstIssue().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstValidationModel)
#include "tst_validation_model.moc"
