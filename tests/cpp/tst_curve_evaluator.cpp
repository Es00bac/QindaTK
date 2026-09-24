// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtTest>
#include <cmath>
#include <limits>

#include "curve_evaluator.h"

using namespace QindaTK;

namespace {
CurveDefinition line()
{
    CurveDefinition curve;
    curve.id = QStringLiteral("curve");
    CurveKnot left, right;
    left.id = QStringLiteral("left");
    right.id = QStringLiteral("right");
    right.x = right.y = 1;
    curve.knots = {left, right};
    return curve;
}
double value(const CurveDefinition &curve, double x)
{
    const auto result = CurveEvaluator::evaluateNormalized(curve, x);
    return result.sample ? result.sample->normalizedY : std::numeric_limits<double>::quiet_NaN();
}
}

class tst_curve_evaluator : public QObject {
    Q_OBJECT
private slots:
    void linearAndTypedDomains();
    void stepBoundaries();
    void analyticCubic();
    void edgePolicies();
    void endpointTangents();
    void invalidDefinitions();
    void invalidQueries();
};

void tst_curve_evaluator::linearAndTypedDomains()
{
    auto curve = line();
    for (double x : {0.0, .125, .5, .875, 1.0}) QCOMPARE(value(curve, x), x);
    curve.xDomain = {UnitDimension::Duration, QStringLiteral("s"), 2, 6};
    curve.yDomain = {UnitDimension::Gain, QStringLiteral("dB"), -60, 0};
    auto result = CurveEvaluator::evaluate(curve, {4, UnitDimension::Duration, QStringLiteral("s")});
    QVERIFY2(result.sample.has_value(), qPrintable(result.error.message));
    QCOMPARE(result.sample->normalizedX, .5);
    QCOMPARE(result.sample->normalizedY, .5);
    QCOMPARE(result.sample->value.value, -30.0);
    QCOMPARE(result.sample->value.dimension, UnitDimension::Gain);
    QCOMPARE(result.sample->value.unitId, QStringLiteral("dB"));
    curve.knots[0].interpolation = CurveInterpolation::CubicBezier;
    // Missing handles default to the line; stored host data is not repaired.
    QVERIFY(std::abs(value(curve, .37) - .37) < 1e-12);
    QVERIFY(!curve.knots[0].outHandle);
    QVERIFY(!curve.knots[1].inHandle);
}

void tst_curve_evaluator::stepBoundaries()
{
    auto curve = line();
    CurveKnot middle;
    middle.id = QStringLiteral("middle");
    middle.x = .5;
    middle.y = .75;
    middle.interpolation = CurveInterpolation::Step;
    curve.knots.insert(1, middle);
    curve.knots[0].interpolation = CurveInterpolation::Step;
    QCOMPARE(value(curve, std::nextafter(.5, 0.0)), 0.0);
    QCOMPARE(value(curve, .5), .75);
    QCOMPARE(value(curve, std::nextafter(.5, 1.0)), .75);
    QCOMPARE(value(curve, std::nextafter(1.0, 0.0)), .75);
    QCOMPARE(value(curve, 1), 1.0);
}

void tst_curve_evaluator::analyticCubic()
{
    auto curve = line();
    curve.knots[0].interpolation = CurveInterpolation::CubicBezier;
    curve.knots[0].outHandle = QPointF(1.0 / 3, 0);
    curve.knots[1].inHandle = QPointF(2.0 / 3, 1);
    // x(t)=t, y(t)=3t^2-2t^3: exact smoothstep values.
    for (double x : {0.0, .01, .125, .25, .5, .75, .99, 1.0})
        QVERIFY(std::abs(value(curve, x) - x * x * (3 - 2 * x)) < 1e-12);
    // x(t)=t^3, y(t)=t, including a vertical endpoint tangent.
    curve.knots[0].outHandle = QPointF(0, 1.0 / 3);
    curve.knots[1].inHandle = QPointF(0, 2.0 / 3);
    for (double x : {0.0, 1e-15, .001, .125, .5, 1.0})
        QVERIFY(std::abs(value(curve, x) - std::cbrt(x)) < 1e-12);
    // A nonuniform segment still solves x in that segment's local domain.
    curve.knots[0].x = .25;
    curve.knots[1].x = .75;
    curve.knots[0].outHandle->setX(.25);
    curve.knots[1].inHandle->setX(.25);
    QVERIFY(std::abs(value(curve, .3125) - .5) < 1e-12);
}

void tst_curve_evaluator::edgePolicies()
{
    auto curve = line();
    curve.knots[0].x = .25;
    curve.knots[1].x = .75;
    QCOMPARE(value(curve, -100), 0.0);
    QCOMPARE(value(curve, 100), 1.0);
    curve.before = curve.after = CurveExtrapolation::Linear;
    QCOMPARE(value(curve, 0), -.5);
    QCOMPARE(value(curve, 1), 1.5);
    curve.before = curve.after = CurveExtrapolation::Repeat;
    QCOMPARE(value(curve, .125), .75);
    QCOMPARE(value(curve, .875), .25);
    QCOMPARE(value(curve, .75), 1.0); // actual endpoint is preserved
    QCOMPARE(value(curve, 1.25), 0.0); // subsequent seam wraps to first
    QCOMPARE(value(curve, -.25), 0.0);
    const auto result = CurveEvaluator::evaluateNormalized(curve, .875);
    QCOMPARE(result.sample->normalizedX, .875); // original query, not .375
    curve.before = curve.after = CurveExtrapolation::Linear;
    curve.knots.resize(1);
    curve.knots[0].y = .6;
    QCOMPARE(value(curve, -100), .6);
    QCOMPARE(value(curve, 100), .6);
}

void tst_curve_evaluator::endpointTangents()
{
    auto curve = line();
    curve.before = curve.after = CurveExtrapolation::Linear;
    curve.knots[0].interpolation = CurveInterpolation::CubicBezier;
    curve.knots[0].outHandle = QPointF(.25, .5); // slope 2 at start
    curve.knots[1].inHandle = QPointF(.5, .75); // slope .5 at end
    QCOMPARE(value(curve, -.25), -.5);
    QCOMPARE(value(curve, 1.25), 1.125);
    curve.knots[0].outHandle = QPointF(0, 0); // zero first derivative
    QCOMPARE(value(curve, -.5), -.75); // limiting slope uses next control
    curve.knots[0].outHandle = QPointF(0, .1); // vertical start tangent
    QCOMPARE(CurveEvaluator::validate(curve).code, ErrorCode::Validation);
    curve.before = CurveExtrapolation::Hold;
    QVERIFY(!CurveEvaluator::validate(curve));
}

void tst_curve_evaluator::invalidDefinitions()
{
    QVector<CurveDefinition> bad;
    auto curve = line(); curve.id.clear(); bad << curve;
    curve = line(); curve.knots.clear(); bad << curve;
    curve = line(); curve.knots[1].id = curve.knots[0].id; bad << curve;
    curve = line(); curve.knots[1].x = 0; bad << curve;
    curve = line(); curve.knots[1].x = 1e-13; bad << curve;
    curve = line(); curve.knots[0].y = std::numeric_limits<double>::quiet_NaN(); bad << curve;
    curve = line(); curve.knots[0].outHandle = QPointF(-.1, .3); bad << curve;
    curve = line(); curve.knots[1].inHandle = QPointF(.5, 1.01); bad << curve;
    curve = line(); curve.knots[0].interpolation = CurveInterpolation::CubicBezier;
    curve.knots[0].outHandle = QPointF(.8, .2);
    curve.knots[1].inHandle = QPointF(.2, .8); bad << curve;
    curve = line(); curve.xDomain.maximum = curve.xDomain.minimum; bad << curve;
    curve = line(); curve.yDomain.unitId.clear(); bad << curve;
    curve = line(); curve.knots.resize(1); curve.after = CurveExtrapolation::Repeat; bad << curve;
    for (const auto &invalid : bad) {
        const auto error = CurveEvaluator::validate(invalid);
        QCOMPARE(error.code, ErrorCode::Validation);
        QVERIFY(!error.fieldPath.isEmpty());
        const auto result = CurveEvaluator::evaluateNormalized(invalid, .5);
        QVERIFY(!result.sample);
        QCOMPARE(result.error.code, ErrorCode::Validation);
    }
}

void tst_curve_evaluator::invalidQueries()
{
    auto curve = line();
    auto result = CurveEvaluator::evaluateNormalized(curve, std::numeric_limits<double>::infinity());
    QVERIFY(!result.sample);
    QCOMPARE(result.error.code, ErrorCode::InvalidArgument);
    result = CurveEvaluator::evaluate(curve, {.5, UnitDimension::Duration, QStringLiteral("s")});
    QCOMPARE(result.error.code, ErrorCode::InvalidArgument);
    result = CurveEvaluator::evaluate(curve, {.5, UnitDimension::Scalar, QStringLiteral("percent")});
    QCOMPARE(result.error.code, ErrorCode::InvalidArgument);
    curve.after = CurveExtrapolation::Linear;
    curve.yDomain.maximum = 1e100;
    result = CurveEvaluator::evaluateNormalized(curve, 1e300);
    QVERIFY(!result.sample);
    QCOMPARE(result.error.code, ErrorCode::InvalidArgument);
}

QTEST_GUILESS_MAIN(tst_curve_evaluator)
#include "tst_curve_evaluator.moc"
