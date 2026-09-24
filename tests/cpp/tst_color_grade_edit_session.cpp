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
};

QTEST_GUILESS_MAIN(ColorGradeEditSessionTest)
#include "tst_color_grade_edit_session.moc"
