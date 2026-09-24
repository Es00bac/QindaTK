// SPDX-License-Identifier: LGPL-3.0-or-later
#include "gradient_editor_model.h"
#include <QtTest>
using namespace QindaTK;
class GradientEditorModelTest : public QObject {
  Q_OBJECT
private slots:
  void gestureUndoAndPersistence() {
    GradientEditorModel model;
    const auto before = model.definition();
    QCOMPARE(model.stops().size(), 2);
    QVERIFY(model.selectStop(before.stops[0].id));
    QVERIFY(model.beginGesture("Move stop"));
    QVERIFY2(model.previewMove(before.stops[0].id, .2),
             qPrintable(model.lastError()));
    QCOMPARE(model.definition().stops[0].position, .2);
    QVERIFY(model.commitGesture());
    QCOMPARE(model.undoService()->historyIndex(), 1);
    QVERIFY(model.undo());
    QCOMPARE(model.definition().stops[0].position, 0.0);
    QVERIFY(model.redo());
    QCOMPARE(model.definition().stops[0].position, .2);
    const auto encoded = model.encode();
    QVERIFY(!encoded.isEmpty());
    OperationError error;
    auto decoded = GradientDefinitionCodec::decode(encoded, &error);
    QVERIFY2(decoded.has_value(), qPrintable(error.message));
    QCOMPARE(decoded->stops[0].id, before.stops[0].id);
    QCOMPARE(decoded->stops[0].position, .2);
    QCOMPARE(model.previewSamples().size(), 64);
  }
};
QTEST_MAIN(GradientEditorModelTest)
#include "tst_gradient_editor_model.moc"
