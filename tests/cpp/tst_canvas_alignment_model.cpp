// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtTest>
#include "canvas_alignment_model.h"
#include "canvas_geometry.h"

using namespace QindaTK;

class tst_canvas_alignment_model : public QObject {
    Q_OBJECT
private slots:
    void alignAndDistributeCommitOneReversibleEdit();
};

void tst_canvas_alignment_model::alignAndDistributeCommitOneReversibleEdit()
{
    CanvasDocument document;
    document.setPageRect(QRectF(0, 0, 200, 100));
    const auto a = document.addRect(20, 20, 10, 10, QColor(Qt::red));
    const auto b = document.addRect(80, 30, 20, 10, QColor(Qt::blue));
    const auto c = document.addRect(150, 50, 10, 10, QColor(Qt::green));
    QVERIFY(document.setObjectTransform(b, {1.0, 0.2, 0.0, 1.0, 80.0, 30.0}));
    const CanvasObject beforeA = *document.object(a);
    const CanvasObject beforeB = *document.object(b);
    const CanvasObject beforeC = *document.object(c);
    CanvasAlignmentModel alignment;
    alignment.setDocument(&document);
    const QVariantList ids{QString::number(a), QString::number(b), QString::number(c)};
    QVERIFY(alignment.apply(QStringLiteral("align.left"), ids, QStringLiteral("page")));
    for (CanvasObjectId id : {a, b, c})
        QVERIFY(qAbs(CanvasGeometry::documentBounds(*document.object(id)).left()) < 1e-7);
    QVERIFY(document.undo());
    QCOMPARE(*document.object(a), beforeA);
    QCOMPARE(*document.object(b), beforeB);
    QCOMPARE(*document.object(c), beforeC);

    QVERIFY(alignment.apply(QStringLiteral("distribute.horizontal"), ids,
                            QStringLiteral("selection"), {}, 12));
    const auto ba = CanvasGeometry::documentBounds(*document.object(a));
    const auto bb = CanvasGeometry::documentBounds(*document.object(b));
    const auto bc = CanvasGeometry::documentBounds(*document.object(c));
    QVERIFY2(qAbs(bb.left() - ba.right() - 12) < 1e-7,
             qPrintable(QStringLiteral("first gap: %1, a=%2, b=%3")
                        .arg(bb.left() - ba.right()).arg(ba.left()).arg(bb.left())));
    QVERIFY2(qAbs(bc.left() - bb.right() - 12) < 1e-7,
             qPrintable(QStringLiteral("second gap: %1, b=%2, c=%3")
                        .arg(bc.left() - bb.right()).arg(bb.right()).arg(bc.left())));
    QVERIFY(document.undo());
    QCOMPARE(*document.object(a), beforeA);
    QCOMPARE(*document.object(b), beforeB);
    QCOMPARE(*document.object(c), beforeC);

    QVERIFY(document.setObjectLocked(c, true));
    QVERIFY(!alignment.apply(QStringLiteral("align.top"), ids,
                             QStringLiteral("selection")));
    QVERIFY(!alignment.lastError().isEmpty());
    QCOMPARE(*document.object(a), beforeA);
}

QTEST_MAIN(tst_canvas_alignment_model)
#include "tst_canvas_alignment_model.moc"
