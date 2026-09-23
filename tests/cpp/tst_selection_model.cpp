// SPDX-License-Identifier: LGPL-3.0-or-later
#include "selection_model.h"

#include <QtTest>

using namespace QindaTK;

class TstSelectionModel final : public QObject {
    Q_OBJECT
private:
    static QString id(int value)
    {
        return QUuid(QStringLiteral("{00000000-0000-4000-8000-%1}")
                         .arg(value, 12, 10, QLatin1Char('0')))
            .toString(QUuid::WithoutBraces);
    }

private slots:
    void canonicalizesUuidAndRetainsIdentityAcrossReorder()
    {
        SelectionModel model;
        const QString a = id(1), b = id(2), c = id(3);
        const QString upperBraced = QUuid(a).toString(QUuid::WithoutBraces).toUpper();
        QVERIFY(model.select(QStringLiteral("{") + upperBraced + QStringLiteral("}")));
        QCOMPARE(model.selectedIds(), QStringList{a});
        QVERIFY(model.toggle(b));
        QVERIFY(model.toggle(c));
        QCOMPARE(model.selectedIds(), (QStringList{a, b, c}));

        // The full live model order can change arbitrarily; selected IDs stay
        // in their prior selection order and no delegate index is retained.
        QVERIFY(model.reconcileLiveIds({c, a, b}));
        QCOMPARE(model.selectedIds(), (QStringList{a, b, c}));
        QCOMPARE(model.currentId(), c);
        QCOMPARE(model.anchorId(), a);
    }

    void extendsInSuppliedVisibleOrderIncludingFilteredRowsOnly()
    {
        SelectionModel model;
        const QString a = id(1), b = id(2), c = id(3), d = id(4), hidden = id(5);
        QVERIFY(model.select(c));
        QVERIFY(model.extendTo(d, {a, c, d}));
        QCOMPARE(model.selectedIds(), (QStringList{c, d}));
        QCOMPARE(model.anchorId(), c);
        QCOMPARE(model.currentId(), d);

        // A later sort changes the visible range and still resolves by IDs.
        QVERIFY(model.extendTo(a, {d, c, b, a}));
        QCOMPARE(model.selectedIds(), (QStringList{c, b, a}));
        QVERIFY(!model.extendTo(hidden, {a, b, c, d}));
        QCOMPARE(model.selectedIds(), (QStringList{c, b, a}));
    }

    void reconciliationDropsDeletedIdsWithoutSelectingReplacement()
    {
        SelectionModel model;
        const QString a = id(1), b = id(2), c = id(3);
        QSignalSpy modified(&model, &SelectionModel::selectionModified);
        QVERIFY(model.select(a));
        QVERIFY(model.toggle(b));
        modified.clear();
        QVERIFY(model.reconcileLiveIds({c, b}));
        QCOMPARE(model.selectedIds(), QStringList{b});
        QCOMPARE(model.currentId(), b);
        QVERIFY(model.anchorId().isEmpty());
        QCOMPARE(modified.size(), 0);
        QVERIFY(model.reconcileLiveIds({c}));
        QVERIFY(model.selectedIds().isEmpty());
        QVERIFY(model.currentId().isEmpty());
        QVERIFY(model.anchorId().isEmpty());
        QCOMPARE(modified.size(), 0);
    }

    void sourceRemovalKeepsFilteredSelectionWithoutFullEnumeration()
    {
        SelectionModel model;
        const QString a = id(1), hidden = id(2), c = id(3);
        QVERIFY(model.select(a));
        QVERIFY(model.toggle(hidden));
        QVERIFY(model.toggle(c));
        QSignalSpy modified(&model, &SelectionModel::selectionModified);
        QVERIFY(model.removeIds({a}));
        QCOMPARE(model.selectedIds(), (QStringList{hidden, c}));
        QCOMPARE(model.currentId(), c);
        QVERIFY(model.anchorId().isEmpty());
        QCOMPARE(modified.size(), 0);
        QVERIFY(model.removeIds({c}));
        QCOMPARE(model.selectedIds(), QStringList{hidden});
        QVERIFY(model.currentId().isEmpty());
        QVERIFY(!model.removeIds({QStringLiteral("bad-id")}));
        QCOMPARE(model.selectedIds(), QStringList{hidden});
    }

    void modelResetReconcilesSelectionAndPublishesDecimalGenerationSilently()
    {
        SelectionModel model;
        QVERIFY(model.select(id(1)));
        QVERIFY(model.toggle(id(2)));
        QSignalSpy modified(&model, &SelectionModel::selectionModified);
        QSignalSpy selectionChanged(&model, &SelectionModel::selectionChanged);
        QSignalSpy generationChanged(&model, &SelectionModel::generationChanged);
        QVERIFY(model.resetModel({id(2), id(1)}, QStringLiteral("9007199254740993")));
        QCOMPARE(model.selectedIds(), (QStringList{id(1), id(2)}));
        QCOMPARE(model.currentId(), id(2));
        QCOMPARE(model.anchorId(), id(1));
        QCOMPARE(model.generation(), QStringLiteral("9007199254740993"));
        QCOMPARE(modified.size(), 0);
        QCOMPARE(selectionChanged.size(), 0);
        QCOMPARE(generationChanged.size(), 1);
        QVERIFY(!model.resetModel({}, QStringLiteral("9007199254740993")));
        QCOMPARE(model.generation(), QStringLiteral("9007199254740993"));
        QVERIFY(model.resetModel({id(2)}, QStringLiteral("9007199254740994")));
        QCOMPARE(model.selectedIds(), (QStringList{id(2)}));
        QCOMPARE(model.currentId(), id(2));
        QVERIFY(model.anchorId().isEmpty());
        QCOMPARE(selectionChanged.size(), 1);
    }

    void invalidRequestsAreAtomic()
    {
        SelectionModel model;
        const QString a = id(1), b = id(2);
        QVERIFY(model.select(a));
        QVERIFY(model.toggle(b));
        const auto selectedBefore = model.selectedIds();
        const auto currentBefore = model.currentId();
        const auto anchorBefore = model.anchorId();
        const auto generationBefore = model.generation();
        QSignalSpy modified(&model, &SelectionModel::selectionModified);

        QVERIFY(!model.select(QStringLiteral("not-a-uuid")));
        QVERIFY(!model.toggle(QStringLiteral("")));
        QVERIFY(!model.extendTo(id(3), {a, a})); // duplicate by UUID
        QVERIFY(!model.extendTo(id(3), {a, QStringLiteral("bad")}));
        QVERIFY(!model.reconcileLiveIds({a, QStringLiteral("bad")}));
        QVERIFY(!model.reconcileLiveIds({a, QStringLiteral("{") + a.toUpper() + QStringLiteral("}")}));
        QVERIFY(!model.resetModel({a, a}, QStringLiteral("8")));
        QVERIFY(!model.resetModel({a}, QStringLiteral("-1")));
        QVERIFY(!model.resetModel({a}, QStringLiteral("+8")));
        QVERIFY(!model.resetModel({a}, QStringLiteral("18446744073709551616")));
        QVERIFY(!model.resetModel({a}, QStringLiteral("0")));
        QCOMPARE(model.selectedIds(), selectedBefore);
        QCOMPARE(model.currentId(), currentBefore);
        QCOMPARE(model.anchorId(), anchorBefore);
        QCOMPARE(model.generation(), generationBefore);
        QCOMPARE(modified.size(), 0);
    }
};

QTEST_GUILESS_MAIN(TstSelectionModel)
#include "tst_selection_model.moc"
