// SPDX-License-Identifier: LGPL-3.0-or-later
#include "property_binding.h"

#include <QtTest>

using namespace QindaTK;

class TstPropertyBinding final : public QObject {
    Q_OBJECT
private:
    struct Fixture {
        PropertyDocument document;
        UndoService undo;
        PropertyBinding binding;
        EntityId first = EntityId::create();
        EntityId second = EntityId::create();

        explicit Fixture(QMetaType type = QMetaType::fromType<int>(),
                         QVariant initial = QVariant::fromValue(4),
                         QVariant secondInitial = QVariant::fromValue(4))
        {
            QVERIFY(document.defineProperty({QStringLiteral("gain"), type,
                                             initial, QStringLiteral("linear"), true, true}));
            QVERIFY(document.addEntity(first, {{QStringLiteral("gain"), initial}}));
            QVERIFY(document.addEntity(second, {{QStringLiteral("gain"), secondInitial}}));
            QVERIFY(undo.addDocument(&document));
            binding.setDocument(&document);
            binding.setUndoService(&undo);
            binding.setPropertyId(QStringLiteral("gain"));
            binding.setTargetIds({first.toString(), second.toString()});
        }
    };

private slots:
    void projectsMixedStateAndOwnerChangesSilently()
    {
        Fixture fixture(QMetaType::fromType<int>(), 4, 9);
        QSignalSpy accepted(&fixture.binding, &PropertyBinding::userEditAccepted);
        QVERIFY(fixture.binding.mixed());
        QVERIFY(!fixture.binding.value().isValid());
        QVERIFY(fixture.binding.writable());

        EditTransaction ownerEdit;
        ownerEdit.setDocument(&fixture.document);
        ownerEdit.setUndoService(&fixture.undo);
        QVERIFY(ownerEdit.begin({fixture.first, fixture.second}, QStringLiteral("gain"), QStringLiteral("Owner update")));
        QVERIFY(ownerEdit.update(QVariant::fromValue(12)));
        QVERIFY(ownerEdit.commit());
        QVERIFY(!fixture.binding.mixed());
        QCOMPARE(fixture.binding.value().toInt(), 12);
        QCOMPARE(accepted.count(), 0);
    }

    void userEditIsOneTransactionAndOneUndoStep()
    {
        Fixture fixture;
        QSignalSpy accepted(&fixture.binding, &PropertyBinding::userEditAccepted);
        QVERIFY(fixture.binding.begin());
        QVERIFY(fixture.binding.update(QVariant::fromValue(7)));
        QCOMPARE(fixture.document.value(fixture.first, QStringLiteral("gain")).toInt(), 7);
        QCOMPARE(fixture.document.committedValue(fixture.first, QStringLiteral("gain")).toInt(), 4);
        QVERIFY(fixture.binding.commit());
        QCOMPARE(accepted.count(), 1);
        QCOMPARE(accepted.first().at(0).toString(), QStringLiteral("gain"));
        QCOMPARE(accepted.first().at(1).toStringList(),
                 QStringList({fixture.first.toString(), fixture.second.toString()}));
        QVERIFY(fixture.undo.canUndo());
        QVERIFY(fixture.undo.undo());
        QCOMPARE(fixture.document.value(fixture.first, QStringLiteral("gain")).toInt(), 4);
        QCOMPARE(fixture.document.value(fixture.second, QStringLiteral("gain")).toInt(), 4);
    }

    void cancelRestoresTheCommittedProjection()
    {
        Fixture fixture;
        QSignalSpy accepted(&fixture.binding, &PropertyBinding::userEditAccepted);
        QVERIFY(fixture.binding.begin());
        QVERIFY(fixture.binding.update(QVariant::fromValue(10)));
        QCOMPARE(fixture.binding.value().toInt(), 10);
        fixture.binding.cancel();
        QVERIFY(!fixture.binding.editing());
        QCOMPARE(fixture.binding.value().toInt(), 4);
        QCOMPARE(fixture.document.committedValue(fixture.first, QStringLiteral("gain")).toInt(), 4);
        QCOMPARE(accepted.count(), 0);
        QVERIFY(!fixture.undo.canUndo());
    }

    void readOnlyTargetRejectsWholeSelection()
    {
        Fixture fixture;
        QVERIFY(fixture.document.setTargetWritable(fixture.second, QStringLiteral("gain"), false));
        fixture.binding.refresh();
        QVERIFY(!fixture.binding.writable());
        QVERIFY(!fixture.binding.availabilityError().isEmpty());
        QSignalSpy accepted(&fixture.binding, &PropertyBinding::userEditAccepted);
        QVERIFY(!fixture.binding.begin());
        QVERIFY(!fixture.binding.update(QVariant::fromValue(8)));
        QCOMPARE(fixture.document.committedValue(fixture.first, QStringLiteral("gain")).toInt(), 4);
        QCOMPARE(fixture.document.committedValue(fixture.second, QStringLiteral("gain")).toInt(), 4);
        QCOMPARE(accepted.count(), 0);
        QVERIFY(!fixture.undo.canUndo());
    }

    void duplicateTargetSpellingsRejectBeforeEdit()
    {
        Fixture fixture;
        const QString bracedUpper = QStringLiteral("{")
            + fixture.first.toString().toUpper() + QStringLiteral("}");
        fixture.binding.setTargetIds({fixture.first.toString(), bracedUpper});
        QVERIFY(!fixture.binding.writable());
        QVERIFY(!fixture.binding.begin());
        QCOMPARE(fixture.document.committedValue(fixture.first, QStringLiteral("gain")), QVariant(4));
        QVERIFY(!fixture.undo.canUndo());
    }

    void exactIntegerUsesDecimalStringAcrossQmlBoundary()
    {
        const qlonglong initial = 9007199254740993LL;
        Fixture fixture(QMetaType::fromType<qlonglong>(), QVariant::fromValue(initial),
                        QVariant::fromValue(initial));
        QVERIFY(fixture.binding.requiresExactString());
        QCOMPARE(fixture.binding.exactIntegerValue(), QStringLiteral("9007199254740993"));
        QVERIFY(fixture.binding.begin());
        QVERIFY(fixture.binding.updateExactInteger(QStringLiteral("9007199254740995")));
        QVERIFY(fixture.binding.commit());
        QCOMPARE(fixture.document.committedValue(fixture.first, QStringLiteral("gain")).toLongLong(),
                 9007199254740995LL);
        QVERIFY(!fixture.binding.updateExactInteger(QStringLiteral("09007199254740995")));
        QCOMPARE(fixture.document.committedValue(fixture.first, QStringLiteral("gain")).toLongLong(),
                 9007199254740995LL);
    }
};

QTEST_GUILESS_MAIN(TstPropertyBinding)
#include "tst_property_binding.moc"
