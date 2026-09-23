// SPDX-License-Identifier: LGPL-3.0-or-later
#include "property_binding.h"

#include <QtTest>
#include <QVector3D>
#include <limits>

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

    void mixedVectorsPatchEachTargetAndUndoTogether()
    {
        const QVector3D first(1.0f, 2.0f, 3.0f);
        const QVector3D second(4.0f, 5.0f, 6.0f);
        Fixture fixture(QMetaType::fromType<QVector3D>(), QVariant::fromValue(first),
                        QVariant::fromValue(second));
        QVERIFY(fixture.binding.mixed());
        QSignalSpy accepted(&fixture.binding, &PropertyBinding::userEditAccepted);
        QVERIFY(fixture.binding.begin());
        QVERIFY(fixture.binding.updateComponent(0, 9.0));
        QCOMPARE(fixture.document.value(fixture.first, QStringLiteral("gain")).value<QVector3D>(),
                 QVector3D(9.0f, 2.0f, 3.0f));
        QCOMPARE(fixture.document.value(fixture.second, QStringLiteral("gain")).value<QVector3D>(),
                 QVector3D(9.0f, 5.0f, 6.0f));
        QVERIFY(fixture.binding.updateComponent(2, 8.0));
        QCOMPARE(fixture.document.value(fixture.first, QStringLiteral("gain")).value<QVector3D>(),
                 QVector3D(9.0f, 2.0f, 8.0f));
        QCOMPARE(fixture.document.value(fixture.second, QStringLiteral("gain")).value<QVector3D>(),
                 QVector3D(9.0f, 5.0f, 8.0f));
        QCOMPARE(fixture.document.committedValue(fixture.first, QStringLiteral("gain")).value<QVector3D>(), first);
        QVERIFY(fixture.binding.commit());
        QCOMPARE(accepted.size(), 1);
        QCOMPARE(accepted.first().at(2).toMap().value(QStringLiteral("componentIndex")).toInt(), 2);
        QCOMPARE(fixture.document.committedValue(fixture.first, QStringLiteral("gain")).value<QVector3D>(),
                 QVector3D(9.0f, 2.0f, 8.0f));
        QVERIFY(fixture.undo.undo());
        QCOMPARE(fixture.document.value(fixture.first, QStringLiteral("gain")).value<QVector3D>(), first);
        QCOMPARE(fixture.document.value(fixture.second, QStringLiteral("gain")).value<QVector3D>(), second);
        QVERIFY(!fixture.undo.canUndo());
    }

    void canceledComponentPreviewRestoresAllTargets()
    {
        const QVector3D first(1.0f, 2.0f, 3.0f);
        const QVector3D second(4.0f, 5.0f, 6.0f);
        Fixture fixture(QMetaType::fromType<QVector3D>(), QVariant::fromValue(first),
                        QVariant::fromValue(second));
        QVERIFY(fixture.binding.begin());
        QVERIFY(fixture.binding.updateComponent(1, 10.0));
        fixture.binding.cancel();
        QCOMPARE(fixture.document.value(fixture.first, QStringLiteral("gain")).value<QVector3D>(), first);
        QCOMPARE(fixture.document.value(fixture.second, QStringLiteral("gain")).value<QVector3D>(), second);
        QVERIFY(!fixture.undo.canUndo());
    }

    void componentPatchRejectsWrongTypeBadIndexAndStaleRevision()
    {
        Fixture scalar;
        QVERIFY(!scalar.binding.updateComponent(0, 5.0));
        QVERIFY(!scalar.binding.editing());
        QVERIFY(!scalar.undo.canUndo());
        Fixture vector(QMetaType::fromType<QVector3D>(),
                       QVariant::fromValue(QVector3D(1, 2, 3)),
                       QVariant::fromValue(QVector3D(4, 5, 6)));
        QVERIFY(!vector.binding.updateComponent(3, 5.0));
        QVERIFY(!vector.binding.editing());
        QVERIFY(!vector.binding.updateComponent(0, std::numeric_limits<double>::quiet_NaN()));
        QVERIFY(!vector.binding.editing());
        QVERIFY(vector.binding.begin());
        QVERIFY(vector.binding.updateComponent(0, 7.0));
        const EntityId extra = EntityId::create();
        QVERIFY(vector.document.addEntity(extra));
        QVERIFY(!vector.binding.updateComponent(1, 8.0));
        QVERIFY(!vector.binding.editing());
        QCOMPARE(vector.document.committedValue(vector.first, QStringLiteral("gain")).value<QVector3D>(),
                 QVector3D(1, 2, 3));
        QVERIFY(!vector.undo.canUndo());
    }

    void doubleVectorListPreservesOtherAxes()
    {
        const QVariantList first{1.0, 2.0};
        const QVariantList second{3.0, 4.0};
        Fixture fixture(QMetaType::fromType<QVariantList>(), first, second);
        QVERIFY(fixture.binding.updateComponent(1, 9.0));
        QVERIFY(fixture.binding.commit());
        QCOMPARE(fixture.document.value(fixture.first, QStringLiteral("gain")).toList(),
                 QVariantList({1.0, 9.0}));
        QCOMPARE(fixture.document.value(fixture.second, QStringLiteral("gain")).toList(),
                 QVariantList({3.0, 9.0}));
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
