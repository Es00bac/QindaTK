// SPDX-License-Identifier: LGPL-3.0-or-later
#include "color_grade_edit_session.h"

#include <QColorSpace>
#include <QtTest>

using namespace QindaTK;

class ColorGradeEditSessionTest final : public QObject {
    Q_OBJECT
private slots:
    void previewCancelCommitUndoRedoTracksProcessedPixels()
    {
        ColorGradeEditSession session;
        QImage source(2, 1, QImage::Format_RGBA8888);
        source.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
        uchar *pixels = source.scanLine(0);
        for (int x = 0; x < 2; ++x) {
            pixels[x * 4] = 70; pixels[x * 4 + 1] = 100;
            pixels[x * 4 + 2] = 130; pixels[x * 4 + 3] = 211;
        }
        ColorContextSnapshot context;
        context.document = ColorProfile::linearSRgb();
        context.working = ColorProfile::linearSRgb();
        context.display = ColorProfile::linearSRgb();
        context.output = ColorProfile::linearSRgb();
        context.generation = 9;
        session.setFrame(QStringLiteral("clip-a"), QStringLiteral("frame-12"), source, context);
        const QImage baseline = session.processedImage();
        QVERIFY(!baseline.isNull());

        const QVariantMap candidate{
            {QStringLiteral("exposureEV"), 0.0},
            {QStringLiteral("lift"), QVariantMap{{QStringLiteral("x"), 0.5}, {QStringLiteral("y"), 0.0}}},
            {QStringLiteral("gamma"), QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}}},
            {QStringLiteral("gain"), QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}}}};
        QVERIFY(session.beginGesture(QStringLiteral("drag-1")));
        QVERIFY(session.preview(QStringLiteral("drag-1"), candidate));
        const QImage preview = session.processedImage();
        QVERIFY(preview != baseline);
        QCOMPARE(preview.constScanLine(0)[3], uchar(211));
        QVERIFY(session.cancelGesture(QStringLiteral("drag-1")));
        QCOMPARE(session.processedImage(), baseline);
        QVERIFY(!session.canUndo());

        QVERIFY(session.beginGesture(QStringLiteral("drag-2")));
        QVERIFY(session.preview(QStringLiteral("drag-2"), candidate));
        QVERIFY(session.finishGesture(QStringLiteral("drag-2")));
        const QImage committed = session.processedImage();
        QVERIFY(committed != baseline);
        QVERIFY(session.canUndo());
        QVERIFY(session.undo());
        QCOMPARE(session.processedImage(), baseline);
        QVERIFY(session.canRedo());
        QVERIFY(session.redo());
        QCOMPARE(session.processedImage(), committed);

        session.setFrame(QStringLiteral("clip-b"), QStringLiteral("frame-1"), source, context);
        QVERIFY(!session.canUndo()); // history from another target cannot affect this frame
        QCOMPARE(session.parameters().value(QStringLiteral("exposureEV")).toDouble(), 0.0);

        ColorContextSnapshot unavailable = context;
        unavailable.generation = 10;
        unavailable.output = {};
        session.setColorContext(unavailable);
        QVERIFY(session.beginGesture(QStringLiteral("bad-context")));
        QVERIFY(!session.preview(QStringLiteral("bad-context"), candidate));
        QVERIFY(!session.editing());
        QVERIFY(!session.canUndo()); // rejected processing never enters history
    }

    // VID-08: a wheel drag and numeric entry with the same x/y build the same
    // parameters and pixels; a drag is one undo step whatever its previews.
    void wheelAndNumericEntryBuildTheSameGrade()
    {
        QImage source(4, 1, QImage::Format_RGBA8888);
        source.setColorSpace(QColorSpace(QColorSpace::SRgb));
        const uchar values[4][3] = {{12, 40, 90}, {128, 128, 128}, {200, 150, 30}, {250, 250, 250}};
        for (int x = 0; x < 4; ++x) {
            uchar *pixel = source.scanLine(0) + x * 4;
            pixel[0] = values[x][0]; pixel[1] = values[x][1]; pixel[2] = values[x][2]; pixel[3] = 255;
        }
        ColorContextSnapshot context;
        context.document = ColorProfile::sRgb();
        context.working = ColorProfile::linearSRgb();
        context.display = ColorProfile::sRgb();
        context.output = ColorProfile::sRgb();
        ColorGradeEditSession wheel, numeric;
        wheel.setFrame(QStringLiteral("clip"), QStringLiteral("f1"), source, context);
        numeric.setFrame(QStringLiteral("clip"), QStringLiteral("f1"), source, context);

        QVERIFY(wheel.previewWheel(QStringLiteral("drag"), QStringLiteral("gain"), 0.1, 0.0));
        QVERIFY(wheel.previewWheel(QStringLiteral("drag"), QStringLiteral("gain"), 0.25, -0.1));
        QCOMPARE(wheel.parameters().value("gain").toMap().value("x").toDouble(), 0.25);
        QCOMPARE(wheel.committedParameters().value("gain").toMap().value("x").toDouble(), 0.0);
        QVERIFY(wheel.finishWheel(QStringLiteral("drag"), QStringLiteral("gain"), 0.3, -0.2));
        QVERIFY(numeric.setWheelValue(QStringLiteral("gain"), 0.3, -0.2));
        QCOMPARE(wheel.parameters(), numeric.parameters());
        QCOMPARE(wheel.processedImage(), numeric.processedImage());
        QVERIFY(wheel.processedImage() != source.convertToFormat(QImage::Format_RGBA8888));
        const QVariantMap gain = wheel.wheels().at(2).toMap();
        QCOMPARE(gain.value("id").toString(), QStringLiteral("gain"));
        QCOMPARE(gain.value("x").toDouble(), 0.3);
        QCOMPARE(gain.value("minimumX").toDouble(), -1.0);
        QVERIFY(wheel.undo());
        QVERIFY(!wheel.canUndo()); // the whole drag was one step
        QCOMPARE(wheel.parameters().value("gain").toMap().value("x").toDouble(), 0.0);

        QVERIFY(numeric.setExposure(-0.5));
        QCOMPARE(numeric.exposure(), -0.5);
        QVERIFY(numeric.resetWheel(QStringLiteral("gain")));
        QCOMPARE(numeric.parameters().value("gain").toMap().value("x").toDouble(), 0.0);
        QCOMPARE(numeric.exposure(), -0.5);
        QVERIFY(!numeric.setWheelValue(QStringLiteral("gain"), 1.5, 0.0)); // outside [-1, 1]
        QVERIFY(!numeric.setWheelValue(QStringLiteral("hue"), 0.0, 0.0));
        QCOMPARE(numeric.parameters().value("gain").toMap().value("x").toDouble(), 0.0);
        QVERIFY(numeric.gradeSpace().contains(QStringLiteral("never add luma")));
    }
};

QTEST_GUILESS_MAIN(ColorGradeEditSessionTest)
#include "tst_color_grade_edit_session.moc"
