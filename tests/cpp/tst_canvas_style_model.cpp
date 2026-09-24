// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtTest>
#include "canvas_style_model.h"
#include "canvas_document_format.h"

using namespace QindaTK;

class tst_canvas_style_model : public QObject {
    Q_OBJECT
private slots:
    void atomicStyleEditUndoAndReopen();
};

void tst_canvas_style_model::atomicStyleEditUndoAndReopen()
{
    CanvasDocument document;
    const auto first = document.addRect(0, 0, 30, 30, QColor(Qt::red));
    const auto second = document.addEllipse(50, 0, 20, 20, QColor(Qt::blue));
    const CanvasObject before = *document.object(first);
    CanvasStyleModel styles;
    styles.setDocument(&document);
    const QVariantList ids{QString::number(first), QString::number(second)};
    const QVariantMap change{{QStringLiteral("fill"), QColor("#78aacc")},
                             {QStringLiteral("stroke"), QColor("#ff9900")},
                             {QStringLiteral("strokeWidth"), 3.0}};
    QVERIFY(styles.apply(ids, change));
    QCOMPARE(document.object(first)->fill, QColor("#78aacc"));
    QCOMPARE(document.object(second)->strokeWidth, 3.0);
    OperationError error;
    const QByteArray bytes = CanvasDocumentFormat::encode(document.state(), &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error.message));
    const auto reopened = CanvasDocumentFormat::decode(bytes, {}, &error);
    QVERIFY2(reopened.has_value(), qPrintable(error.message));
    QCOMPARE(reopened->objects.value(first).fill, QColor("#78aacc"));
    QCOMPARE(reopened->objects.value(second).stroke, QColor("#ff9900"));
    QVERIFY(document.undo());
    QCOMPARE(*document.object(first), before);
    QVERIFY(document.setObjectLocked(second, true));
    QVERIFY(!styles.apply(ids, {{QStringLiteral("fill"), QColor(Qt::green)}}));
    QCOMPARE(*document.object(first), before);
    QVERIFY(!styles.apply({QString::number(first)},
                          {{QStringLiteral("dash"), QVariantList{2, 2}}}));
}

QTEST_MAIN(tst_canvas_style_model)
#include "tst_canvas_style_model.moc"
