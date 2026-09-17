// SPDX-License-Identifier: LGPL-3.0-or-later
#include "svg_path.h"

#include <QtTest>

using namespace QindaTK;

class TstSvgPath final : public QObject {
    Q_OBJECT
private slots:
    void square()
    {
        bool ok = false;
        const QPainterPath path = svgPathToPainterPath(u"M0 0 L10 0 L10 10 L0 10 Z", &ok);
        QVERIFY(ok);
        QCOMPARE(path.boundingRect(), QRectF(0, 0, 10, 10));
    }
    void relativeAndImplicitCommands()
    {
        bool ok = false;
        const QPainterPath path = svgPathToPainterPath(u"m2 2 h4 v4 h-4z m10,0 l1 1 2 2", &ok);
        QVERIFY(ok);
        QCOMPARE(path.boundingRect().left(), 2.0);
        QCOMPARE(path.boundingRect().right(), 15.0);
    }
    void arcHalfCircle()
    {
        bool ok = false;
        const QPainterPath path = svgPathToPainterPath(u"M0 0 A5 5 0 0 1 10 0", &ok);
        QVERIFY(ok);
        const QRectF bounds = path.boundingRect();
        QVERIFY(qAbs(bounds.width() - 10) < 0.05);
        QVERIFY(qAbs(bounds.height() - 5) < 0.05);
        QVERIFY(bounds.top() < -4.9);
    }
    void squeezedArcFlags()
    {
        bool ok = false;
        const QPainterPath path = svgPathToPainterPath(u"M12 2a10 10 0 1010 10", &ok);
        QVERIFY(ok);
        QVERIFY(!path.isEmpty());
    }
    void lucideCircleRoundTrip()
    {
        bool ok = false;
        const QPainterPath path = svgPathToPainterPath(u"M2 12 a10 10 0 1 0 20 0 a10 10 0 1 0 -20 0", &ok);
        QVERIFY(ok);
        const QRectF bounds = path.boundingRect();
        QVERIFY(qAbs(bounds.width() - 20) < 0.05);
        QVERIFY(qAbs(bounds.height() - 20) < 0.05);
    }
    void cubicAndSmooth()
    {
        bool ok = false;
        const QPainterPath path = svgPathToPainterPath(u"M0 0 C 0 5 10 5 10 0 S 20 -5 20 0 Q 25 5 30 0 T 40 0", &ok);
        QVERIFY(ok);
        QVERIFY(qAbs(path.currentPosition().x() - 40) < 0.001);
    }
    void badInput()
    {
        bool ok = true;
        svgPathToPainterPath(u"M0 0 L", &ok);
        QVERIFY(!ok);
        ok = true;
        svgPathToPainterPath(u"X1 2", &ok);
        QVERIFY(!ok);
    }
};

QTEST_GUILESS_MAIN(TstSvgPath)
#include "tst_svg_path.moc"
