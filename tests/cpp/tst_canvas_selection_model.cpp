// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtTest>
#include "canvas_selection_model.h"
#include "tool_controller.h"

using namespace QindaTK;

class tst_canvas_selection_model : public QObject {
    Q_OBJECT
private slots:
    void sharedAcrossViewsAndPrunedByDocument();
};

void tst_canvas_selection_model::sharedAcrossViewsAndPrunedByDocument()
{
    CanvasDocument document;
    const auto first = document.addRect(10, 10, 40, 30, QColor(Qt::red));
    const auto second = document.addRect(90, 10, 40, 30, QColor(Qt::blue));
    CanvasSelectionModel shared;
    shared.setDocument(&document);
    ToolController left;
    ToolController right;
    left.setDocument(&document);
    right.setDocument(&document);
    left.setSelectionModel(&shared);
    right.setSelectionModel(&shared);

    left.setSelection({first, second, first});
    QCOMPARE(shared.selection(), (QVector<CanvasObjectId>{first, second}));
    QCOMPARE(right.selection(), shared.selection());
    right.setSelection({second});
    QCOMPARE(left.selection(), (QVector<CanvasObjectId>{second}));
    QVERIFY(document.setObjectVisible(second, false));
    QVERIFY(shared.selection().isEmpty());
    QVERIFY(left.selection().isEmpty());
    QVERIFY(right.selection().isEmpty());

    left.setSelection({first});
    QVERIFY(document.removeObject(first));
    QVERIFY(shared.selection().isEmpty());
    QVERIFY(right.selection().isEmpty());
}

QTEST_MAIN(tst_canvas_selection_model)
#include "tst_canvas_selection_model.moc"
