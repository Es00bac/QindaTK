// SPDX-License-Identifier: LGPL-3.0-or-later
#include "curve_editor_model.h"
#include <QtTest>

using namespace QindaTK;

class tst_curve_editor_model final : public QObject {
    Q_OBJECT
private slots:
    void gestureUndoAndPersistence() {
        CurveEditorModel model;
        const auto before = model.definition();
        QVERIFY(model.beginKnotEdit("mid"));
        QVERIFY(model.previewKnot("mid", 0.55, 0.48));
        QCOMPARE(model.knots().at(1).toMap().value("x").toDouble(), 0.55);
        QVERIFY(model.commitEdit());
        QVERIFY(model.undo());
        QCOMPARE(model.definition(), before);
        QVERIFY(model.redo());
        QCOMPARE(model.knots().at(1).toMap().value("x").toDouble(), 0.55);
        const QByteArray bytes = model.saveJson();
        CurveEditorModel restored;
        QVERIFY(restored.loadJson(bytes));
        QCOMPARE(restored.definition(), model.definition());
        QVERIFY(restored.beginTangentEdit("mid", "out"));
        QVERIFY(restored.previewTangent("mid", "out", 0.7, 0.5));
        restored.cancelEdit();
        QCOMPARE(restored.definition(), model.definition());
    }
};

QTEST_GUILESS_MAIN(tst_curve_editor_model)
#include "tst_curve_editor_model.moc"
