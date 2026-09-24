// SPDX-License-Identifier: LGPL-3.0-or-later
#include "color_grade_processor.h"

#include <QColorSpace>
#include <QtTest>

using namespace QindaTK;

class ColorGradeProcessorTest final : public QObject {
    Q_OBJECT
private slots:
    void wheelMathAndAlphaAreDeterministic()
    {
        ColorContextSnapshot context;
        context.document = ColorProfile::linearSRgb();
        context.working = ColorProfile::linearSRgb();
        context.display = ColorProfile::linearSRgb();
        context.output = ColorProfile::linearSRgb();
        context.generation = 42;
        QImage source(1, 1, QImage::Format_RGBA8888);
        source.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
        uchar *pixel = source.scanLine(0);
        pixel[0] = 64; pixel[1] = 128; pixel[2] = 192; pixel[3] = 37;

        ColorGradeParameters parameters;
        parameters.lift = QPointF(0.2, 0.0);
        const auto result = ColorGradeProcessor().process(source, parameters, context);
        QVERIFY2(result, qPrintable(result.error.message));
        QCOMPARE(result.generation, quint64(42));
        const uchar *graded = result.image.constScanLine(0);
        QCOMPARE(graded[0], uchar(70));
        QCOMPARE(graded[1], uchar(122));
        QCOMPARE(graded[2], uchar(192));
        QCOMPARE(graded[3], uchar(37));
        QCOMPARE(result.image.colorSpace(), QColorSpace(QColorSpace::SRgbLinear));
    }

    void taggedSrgbConvertsThroughLinearWorkingSpace()
    {
        ColorContextSnapshot context;
        context.document = ColorProfile::sRgb();
        context.working = ColorProfile::linearSRgb();
        context.display = ColorProfile::sRgb();
        context.output = ColorProfile::linearSRgb();
        QImage source(1, 1, QImage::Format_RGBA8888);
        source.setColorSpace(QColorSpace(QColorSpace::SRgb));
        uchar *pixel = source.scanLine(0);
        pixel[0] = 128; pixel[1] = 128; pixel[2] = 128; pixel[3] = 211;
        ColorGradeParameters parameters;
        parameters.exposureEv = 1.0;

        const auto result = ColorGradeProcessor().process(source, parameters, context);
        QVERIFY2(result, qPrintable(result.error.message));
        const int linearByte = result.image.constScanLine(0)[0];
        QVERIFY2(linearByte >= 108 && linearByte <= 111, qPrintable(QString::number(linearByte)));
        QCOMPARE(result.image.constScanLine(0)[3], uchar(211));
    }

    void invalidAndUnsupportedRequestsFailWithoutAnImage()
    {
        ColorContextSnapshot context;
        context.working = ColorProfile::linearSRgb();
        context.output = ColorProfile::sRgb();
        QImage source(1, 1, QImage::Format_RGBA8888);
        source.setColorSpace(QColorSpace(QColorSpace::SRgb));
        ColorGradeParameters parameters;
        parameters.exposureEv = 9.0;
        auto result = ColorGradeProcessor().process(source, parameters, context);
        QVERIFY(result.error);
        QVERIFY(result.image.isNull());
        parameters.exposureEv = 0.0;
        context.working = ColorProfile::sRgb();
        result = ColorGradeProcessor().process(source, parameters, context);
        QCOMPARE(result.error.code, ErrorCode::Unsupported);
        QVERIFY(result.image.isNull());
    }
};

QTEST_GUILESS_MAIN(ColorGradeProcessorTest)
#include "tst_color_grade_processor.moc"
