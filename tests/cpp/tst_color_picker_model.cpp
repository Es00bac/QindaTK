// SPDX-License-Identifier: LGPL-3.0-or-later
#include "color_picker_model.h"
#include <QtTest>

using namespace QindaTK;

class TstColorPickerModel final : public QObject {
    Q_OBJECT
private slots:
    void gestureCommitCancelUndoAndCodec()
    {
        ColorPickerModel model;
        const QString target = model.targetIds().constFirst();
        const ColorValue original = model.colorValue(target);
        const QString secondTarget = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(model.addTarget(secondTarget, original));
        model.setTargetIds({target, secondTarget});
        const QColor next = QColor::fromRgbF(0.12, 0.68, 0.37, 0.45);

        QVERIFY(model.previewSrgb(next));
        QVERIFY(model.editing());
        QCOMPARE(model.previewColor(), next);
        model.cancelEdit();
        QVERIFY(!model.editing());
        QCOMPARE(model.colorValue(target), original);
        QVERIFY(!model.undo());

        QVERIFY(model.previewSrgb(next));
        QVERIFY(model.previewSrgb(QColor::fromRgbF(0.2, 0.5, 0.8, 0.7)));
        QVERIFY(model.commitEdit());
        QVERIFY(!model.editing());
        const ColorValue committed = model.colorValue(target);
        QVERIFY(committed != original);
        QCOMPARE(model.colorValue(secondTarget), committed);
        QCOMPARE(committed.profileId, ColorProfile::sRgb().id());
        QCOMPARE(committed.transferId, QStringLiteral("icc"));
        QVERIFY(model.undo());
        QCOMPARE(model.colorValue(target), original);
        QCOMPARE(model.colorValue(secondTarget), original);
        QVERIFY(model.redo());
        QCOMPARE(model.colorValue(target), committed);

        const QByteArray saved = model.saveJson();
        ColorPickerModel reopened;
        QVERIFY(reopened.loadJson(saved));
        QCOMPARE(reopened.colorValue(target), committed);
        QCOMPARE(reopened.colorValue(secondTarget), committed);
    }

    void staleContextRejectsGesture()
    {
        ColorPickerModel model;
        QVERIFY(model.previewSrgb(QColor::fromRgbF(0.8, 0.2, 0.1, 1.0)));
        auto context = model.colorContext();
        ++context.generation;
        model.setColorContext(context);
        QVERIFY(!model.editing());
        QVERIFY(!model.errorMessage().isEmpty());
        QVERIFY(!model.commitEdit());
    }
};

QTEST_GUILESS_MAIN(TstColorPickerModel)
#include "tst_color_picker_model.moc"
