// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtTest>
#include "canvas_style_model.h"
#include "canvas_document_format.h"
#include "canvas_render.h"

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
    QVERIFY(document.setObjectLocked(second, false));

    ColorPickerModel picker;
    QVERIFY(picker.previewSrgb(QColor(128,128,128)));
    QVERIFY(picker.commitEdit());
    QVERIFY(styles.applyPicker({QString::number(first)}, &picker));
    QVERIFY(!document.object(first)->managedFill.isEmpty());
    GradientEditorModel gradient;
    QVERIFY(styles.applyGradient({QString::number(second)}, &gradient));
    QVERIFY(!document.object(second)->managedFill.isEmpty());
    const QByteArray typed = CanvasDocumentFormat::encode(document.state(), &error);
    const auto restored = CanvasDocumentFormat::decode(typed, {}, &error);
    QVERIFY2(restored.has_value(), qPrintable(error.message));
    QCOMPARE(restored->objects.value(first).managedFill, document.object(first)->managedFill);
    QCOMPARE(restored->objects.value(second).managedFill, document.object(second)->managedFill);
    const QImage rendered = CanvasPainter::renderTile(document, QRectF(0,0,128,128), 128, {});
    QVERIFY(std::abs(rendered.pixelColor(15,15).red() - 128) <= 2);
    QVERIFY(rendered.pixelColor(51,10).red() < rendered.pixelColor(67,10).red());
    QVERIFY(document.undo());
    QVERIFY(document.object(second)->managedFill.isEmpty());
}

QTEST_MAIN(tst_canvas_style_model)
#include "tst_canvas_style_model.moc"
