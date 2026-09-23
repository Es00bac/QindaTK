// SPDX-License-Identifier: LGPL-3.0-or-later
#include "edit_transaction.h"
#include <QtTest>

using namespace QindaTK;

class TstEditTransaction final : public QObject {
    Q_OBJECT
private:
    static void setup(PropertyDocument &document, UndoService &undo)
    {
        QVERIFY(document.defineProperty({QStringLiteral("gain"), QMetaType::fromType<double>(),
                                         0.0, QStringLiteral("dB"), true, true}));
        QVERIFY(undo.addDocument(&document));
        undo.setActiveDocument(&document);
    }

    static void configure(EditTransaction &edit, PropertyDocument &document,
                          UndoService &undo)
    {
        edit.setDocument(&document);
        edit.setUndoService(&undo);
    }

private slots:
    void dragIsOneAtomicUndoStep()
    {
        PropertyDocument document;
        UndoService undo;
        setup(document, undo);
        const EntityId a = EntityId::create(), b = EntityId::create();
        QVERIFY(document.addEntity(a, {{QStringLiteral("gain"), 1.0}}));
        QVERIFY(document.addEntity(b, {{QStringLiteral("gain"), 2.0}}));
        undo.markClean();
        const auto originalRevision = document.revision();
        EditTransaction edit;
        configure(edit, document, undo);
        QSignalSpy committed(&edit, &EditTransaction::committed);
        QVector<QPair<double, double>> observations;
        bool committedNotificationHadHistory = true;
        connect(&document, &PropertyDocument::propertyChanged, &document,
                [&](const QString &, const QString &) {
            observations.append({document.value(a, QStringLiteral("gain")).toDouble(),
                                 document.value(b, QStringLiteral("gain")).toDouble()});
            if (document.revision() != originalRevision)
                committedNotificationHadHistory &= undo.canUndo();
        });
        QVERIFY(edit.begin({a, b}, QStringLiteral("gain"), QStringLiteral("Adjust gain")));
        QVERIFY(!edit.updateValue(3.0));
        QVERIFY(!edit.updateValue(4.0));
        QCOMPARE(document.revision(), originalRevision);
        QCOMPARE(document.committedValue(a, QStringLiteral("gain")), QVariant(1.0));
        QCOMPARE(document.value(b, QStringLiteral("gain")), QVariant(4.0));
        QVERIFY(!edit.commitEdit());
        QCOMPARE(committed.size(), 1);
        QVERIFY(undo.canUndo());
        QVERIFY(!undo.canRedo());
        QVERIFY(!undo.isClean());
        QCOMPARE(document.committedValue(a, QStringLiteral("gain")), QVariant(4.0));
        QCOMPARE(document.committedValue(b, QStringLiteral("gain")), QVariant(4.0));
        QCOMPARE(observations.constLast(), (QPair<double, double>{4.0, 4.0}));
        QVERIFY(committedNotificationHadHistory);
        QVERIFY(undo.undo());
        QCOMPARE(document.value(a, QStringLiteral("gain")), QVariant(1.0));
        QCOMPARE(document.value(b, QStringLiteral("gain")), QVariant(2.0));
        QVERIFY(undo.isClean());
        QVERIFY(!undo.canUndo());
        QVERIFY(undo.redo());
        QCOMPARE(document.value(a, QStringLiteral("gain")), QVariant(4.0));
        QCOMPARE(document.value(b, QStringLiteral("gain")), QVariant(4.0));
    }

    void cancellationAndFailedValidationRestoreOriginal()
    {
        PropertyDocument document;
        UndoService undo;
        setup(document, undo);
        const EntityId a = EntityId::create();
        QVERIFY(document.addEntity(a, {{QStringLiteral("gain"), 1.0}}));
        EditTransaction edit;
        configure(edit, document, undo);
        QSignalSpy canceled(&edit, &EditTransaction::canceled);
        QVERIFY(edit.begin({a}, QStringLiteral("gain"), QStringLiteral("Adjust gain")));
        QVERIFY(!edit.updateValue(5.0));
        edit.cancel(); // Escape or lost pointer grab takes this same path.
        QCOMPARE(document.value(a, QStringLiteral("gain")), QVariant(1.0));
        QCOMPARE(canceled.size(), 1);
        QVERIFY(!undo.canUndo());

        edit.setValidator([](const QVector<PropertyChange> &changes) {
            if (!changes.isEmpty() && changes.front().after.toDouble() > 3.0)
                return OperationError{ErrorCode::Validation,
                                      QStringLiteral("Gain exceeds limit"),
                                      QStringLiteral("gain"), {}};
            return OperationError{};
        });
        QVERIFY(edit.begin({a}, QStringLiteral("gain"), QStringLiteral("Adjust gain")));
        QVERIFY(!edit.updateValue(5.0));
        const auto rejected = edit.commitEdit();
        QCOMPARE(rejected.code, ErrorCode::Validation);
        QCOMPARE(edit.errorMessage(), QStringLiteral("Gain exceeds limit"));
        QCOMPARE(document.value(a, QStringLiteral("gain")), QVariant(1.0));
        QVERIFY(!undo.canUndo());
    }

    void staleRevisionAndResetRejectCommit()
    {
        PropertyDocument document;
        UndoService undo;
        setup(document, undo);
        const EntityId a = EntityId::create(), b = EntityId::create();
        QVERIFY(document.addEntity(a));
        EditTransaction edit;
        configure(edit, document, undo);
        QVERIFY(edit.begin({a}, QStringLiteral("gain"), QStringLiteral("Adjust gain")));
        QVERIFY(!edit.updateValue(5.0));
        QVERIFY(document.addEntity(b));
        QCOMPARE(edit.commitEdit().code, ErrorCode::StaleRevision);
        QCOMPARE(document.value(a, QStringLiteral("gain")), QVariant(0.0));
        QVERIFY(!undo.canUndo());

        QVERIFY(edit.begin({a}, QStringLiteral("gain"), QStringLiteral("Adjust gain")));
        QVERIFY(!edit.updateValue(7.0));
        QVERIFY(document.resetModel({{a, {{QStringLiteral("gain"), 2.0}}}}));
        QCOMPARE(edit.commitEdit().code, ErrorCode::StaleGeneration);
        QCOMPARE(document.value(a, QStringLiteral("gain")), QVariant(2.0));
    }

    void multiTargetPreflightAndMemoryLimit()
    {
        PropertyDocument document;
        UndoService undo;
        setup(document, undo);
        const EntityId a = EntityId::create(), b = EntityId::create();
        QVERIFY(document.addEntity(a));
        QVERIFY(document.addEntity(b));
        QVERIFY(document.setTargetWritable(b, QStringLiteral("gain"), false));
        EditTransaction edit;
        configure(edit, document, undo);
        QVERIFY(!edit.begin({a, b}, QStringLiteral("gain"), QStringLiteral("Adjust gain")));
        QCOMPARE(document.value(a, QStringLiteral("gain")), QVariant(0.0));
        QVERIFY(!undo.canUndo());
        QVERIFY(document.setTargetWritable(b, QStringLiteral("gain"), true));
        QVERIFY(edit.begin({a, b}, QStringLiteral("gain"), QStringLiteral("Adjust gain")));
        QCOMPARE(edit.updateValue(QStringLiteral("wrong type")).code, ErrorCode::Validation);
        QCOMPARE(document.value(a, QStringLiteral("gain")), QVariant(0.0));
        QVERIFY(!edit.updateValue(2.0));
        undo.setMemoryLimit(1024);
        // Two small double values still fit; the cap applies to inverse data.
        QVERIFY(!edit.commitEdit());
        QVERIFY(undo.canUndo());

        PropertyDocument huge;
        UndoService hugeUndo;
        QVERIFY(huge.defineProperty({QStringLiteral("blob"), QMetaType::fromType<QByteArray>(),
                                     QByteArray{}, {}, true, false}));
        QVERIFY(hugeUndo.addDocument(&huge));
        const EntityId blob = EntityId::create();
        QVERIFY(huge.addEntity(blob));
        hugeUndo.setMemoryLimit(1024);
        EditTransaction big;
        configure(big, huge, hugeUndo);
        QVERIFY(big.begin({blob}, QStringLiteral("blob"), QStringLiteral("Replace blob")));
        QVERIFY(!big.updateValue(QByteArray(2048, 'x')));
        QCOMPARE(big.commitEdit().code, ErrorCode::Unavailable);
        QCOMPARE(huge.value(blob, QStringLiteral("blob")), QVariant(QByteArray{}));
    }

    void documentStacksMergeAndBranchIndependently()
    {
        PropertyDocument first, second;
        UndoService undo;
        setup(first, undo);
        QVERIFY(second.defineProperty({QStringLiteral("gain"), QMetaType::fromType<double>(),
                                       0.0, {}, true, false}));
        QVERIFY(undo.addDocument(&second));
        const EntityId a = EntityId::create(), b = EntityId::create();
        QVERIFY(first.addEntity(a));
        QVERIFY(second.addEntity(b));
        auto editValue = [&](PropertyDocument &document, EntityId id, double value,
                             const QString &mergeKey = {}) {
            EditTransaction edit;
            configure(edit, document, undo);
            if (!edit.begin({id}, QStringLiteral("gain"), QStringLiteral("Adjust gain"), mergeKey)) return false;
            return !edit.updateValue(value) && !edit.commitEdit();
        };
        QVERIFY(editValue(first, a, 1.0, QStringLiteral("nudge")));
        QVERIFY(editValue(first, a, 2.0, QStringLiteral("nudge")));
        QCOMPARE(undo.undoText(), QStringLiteral("Adjust gain"));
        QVERIFY(undo.undo());
        QCOMPARE(first.value(a, QStringLiteral("gain")), QVariant(0.0));
        QVERIFY(!undo.canUndo());
        QVERIFY(undo.redo());
        undo.markClean();
        QVERIFY(editValue(first, a, 3.0, QStringLiteral("nudge")));
        QVERIFY(undo.undo());
        QVERIFY(undo.isClean());
        QVERIFY(editValue(first, a, 4.0));
        QVERIFY(!undo.canRedo());

        undo.setActiveDocument(&second);
        QVERIFY(!undo.canUndo());
        QVERIFY(editValue(second, b, 9.0));
        QVERIFY(undo.undo());
        QCOMPARE(second.value(b, QStringLiteral("gain")), QVariant(0.0));
        undo.setActiveDocument(&first);
        QCOMPARE(first.value(a, QStringLiteral("gain")), QVariant(4.0));
        QVERIFY(undo.canUndo());
        QVERIFY(first.removeEntity(a));
        QVERIFY(!undo.canUndo()); // no stale identity can resolve after deletion
    }

    void macroGroupsIndependentEditsAndStructuralChangeInvalidatesHistory()
    {
        PropertyDocument document;
        UndoService undo;
        setup(document, undo);
        const EntityId a = EntityId::create(), b = EntityId::create();
        QVERIFY(document.addEntity(a));
        QVERIFY(document.addEntity(b));
        undo.markClean();
        QVERIFY(undo.beginMacro(QStringLiteral("Adjust two tracks")));
        for (const auto &[id, value] : QVector<QPair<EntityId, double>>{{a, 1.0}, {b, 2.0}}) {
            EditTransaction edit;
            configure(edit, document, undo);
            QVERIFY(edit.begin({id}, QStringLiteral("gain"), QStringLiteral("Adjust gain")));
            QVERIFY(!edit.updateValue(value));
            QVERIFY(!edit.commitEdit());
            QVERIFY(!undo.canUndo());
        }
        QVERIFY(undo.endMacro());
        QCOMPARE(undo.undoText(), QStringLiteral("Adjust two tracks"));
        QVERIFY(undo.undo());
        QCOMPARE(document.value(a, QStringLiteral("gain")), QVariant(0.0));
        QCOMPARE(document.value(b, QStringLiteral("gain")), QVariant(0.0));
        QVERIFY(undo.isClean());
        QVERIFY(!undo.canUndo());
        QVERIFY(undo.redo());
        QCOMPARE(document.value(a, QStringLiteral("gain")), QVariant(1.0));
        QCOMPARE(document.value(b, QStringLiteral("gain")), QVariant(2.0));
        QVERIFY(document.addEntity(EntityId::create()));
        QVERIFY(!undo.canUndo());
        QVERIFY(!undo.isClean());
    }
};

QTEST_MAIN(TstEditTransaction)
#include "tst_edit_transaction.moc"
