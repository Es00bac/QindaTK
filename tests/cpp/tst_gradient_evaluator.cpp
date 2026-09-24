// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtTest>
#include <QFile>
#include <cmath>
#include <limits>

#include "gradient_evaluator.h"

using namespace QindaTK;

namespace {
GradientStop stop(const QString &id, double position, const ColorProfile &profile,
                  double red, double green, double blue, double alpha = 1)
{
    return {id, position, {red, green, blue, alpha, profile.id(), profile.transferId()}, profile};
}
GradientDefinition gradient(const ColorProfile &profile)
{
    return {QStringLiteral("gradient"), {
        stop(QStringLiteral("a"), 0, profile, 0, 0, 0),
        stop(QStringLiteral("b"), 1, profile, 1, 1, 1)}};
}
ColorProfile fixture(const char *name)
{
    QFile file(QString::fromUtf8(QINDATK_COLOR_FIXTURES) + QLatin1Char('/') + QString::fromLatin1(name));
    return file.open(QIODevice::ReadOnly) ? ColorProfile::fromIcc(file.readAll()) : ColorProfile{};
}
bool close(double actual, double expected, double tolerance = 2e-6)
{
    return std::abs(actual - expected) < tolerance;
}
}

class tst_gradient_evaluator : public QObject {
    Q_OBJECT
private slots:
    void lightMidpoint();
    void premultipliedAlpha();
    void transparentCanonicalization();
    void tinyAlpha();
    void crossProfile();
    void duplicatesAndEdges();
    void invalidInputs();
    void statelessCandidates();
};

void tst_gradient_evaluator::lightMidpoint()
{
    const auto srgb = ColorProfile::sRgb();
    const auto linear = ColorProfile::linearSRgb();
    ColorEngine engine;
    const auto data = gradient(srgb);
    auto result = GradientEvaluator::evaluate(data, .5, linear, engine);
    QVERIFY2(result.color.has_value(), qPrintable(result.error.message));
    QVERIFY(close(result.color->red, .5));
    QVERIFY(close(result.color->green, .5));
    QVERIFY(close(result.color->blue, .5));
    QCOMPARE(result.color->alpha, 1.0);
    QCOMPARE(result.color->profileId, linear.id());
    result = GradientEvaluator::evaluate(data, .5, srgb, engine);
    QVERIFY(result.color);
    // Ideal sRGB encoding of 0.5 light; ICC 16.16 coefficients add a few ppm.
    QVERIFY(close(result.color->red, .7353569830524495, 1e-5));
    QCOMPARE(result.color->profileId, srgb.id());
    QCOMPARE(result.color->transferId, srgb.transferId());
    QCOMPARE(result.precision, ColorPrecision::Float32);
    QCOMPARE(result.gamut, ColorGamutStatus::Unknown);
}

void tst_gradient_evaluator::premultipliedAlpha()
{
    const auto linear = ColorProfile::linearSRgb();
    auto data = gradient(linear);
    data.stops[0] = stop(QStringLiteral("a"), 0, linear, 1, 0, 0, .25);
    data.stops[1] = stop(QStringLiteral("b"), 1, linear, 0, 0, 1, .75);
    ColorEngine engine;
    const auto result = GradientEvaluator::evaluate(data, .5, linear, engine);
    QVERIFY(result.color);
    QCOMPARE(result.color->alpha, .5);
    QVERIFY(close(result.color->red, .25));
    QVERIFY(close(result.color->green, 0));
    QVERIFY(close(result.color->blue, .75));
    data.stops[0].color.alpha = 0;
    const auto transparentToBlue = GradientEvaluator::evaluate(data, .5, linear, engine);
    QVERIFY(transparentToBlue.color);
    QCOMPARE(transparentToBlue.color->alpha, .375);
    QVERIFY(close(transparentToBlue.color->red, 0));
    QVERIFY(close(transparentToBlue.color->blue, 1));
}

void tst_gradient_evaluator::transparentCanonicalization()
{
    const auto linear = ColorProfile::linearSRgb();
    auto data = gradient(linear);
    data.stops[0] = stop(QStringLiteral("a"), 0, linear, 1, .5, .2, -0.0);
    data.stops[1] = stop(QStringLiteral("b"), 1, linear, .2, .5, 1, 0);
    ColorEngine engine;
    for (double x : {-1.0, 0.0, .5, 1.0, 2.0}) {
        const auto result = GradientEvaluator::evaluate(data, x, ColorProfile::sRgb(), engine);
        QVERIFY(result.color);
        QCOMPARE(result.color->red, 0.0);
        QCOMPARE(result.color->green, 0.0);
        QCOMPARE(result.color->blue, 0.0);
        QCOMPARE(result.color->alpha, 0.0);
        QVERIFY(!std::signbit(result.color->alpha));
    }
    QCOMPARE(data.stops[0].color.red, 1.0); // stored hidden RGB remains untouched
    ColorTransformRequest direct;
    direct.value = data.stops[0].color;
    direct.source = linear;
    direct.context.output = linear;
    direct.operation = ColorOperation::Output;
    const auto converted = engine.convert(direct);
    QVERIFY(converted.value);
    QCOMPARE(converted.value->red, 1.0); // ColorEngine alone preserves hidden RGB
}

void tst_gradient_evaluator::tinyAlpha()
{
    const auto linear = ColorProfile::linearSRgb();
    auto data = gradient(linear);
    const double alpha = std::numeric_limits<double>::denorm_min();
    data.stops[0] = stop(QStringLiteral("a"), 0, linear, .25, .5, .75, alpha);
    data.stops[1] = stop(QStringLiteral("b"), 1, linear, .25, .5, .75, alpha);
    ColorEngine engine;
    const auto result = GradientEvaluator::evaluate(data, .5, linear, engine);
    QVERIFY(result.color);
    QCOMPARE(result.color->alpha, alpha);
    QVERIFY(close(result.color->red, .25));
    QVERIFY(close(result.color->green, .5));
    QVERIFY(close(result.color->blue, .75));
}

void tst_gradient_evaluator::crossProfile()
{
    const auto srgb = fixture("srgb.icc");
    const auto linear = fixture("linear-srgb.icc");
    QVERIFY(srgb.isValid());
    QVERIFY(linear.isValid());
    auto data = gradient(srgb);
    data.stops[0] = stop(QStringLiteral("a"), 0, srgb, .5, .5, .5);
    data.stops[1] = stop(QStringLiteral("b"), 1, linear, .5, .5, .5);
    ColorEngine engine;
    const auto result = GradientEvaluator::evaluate(data, .5, linear, engine);
    QVERIFY2(result.color.has_value(), qPrintable(result.error.message));
    // (ideal sRGB decode(0.5) + 0.5 linear)/2, independent of ColorEngine.
    constexpr double expected = (.21404114048223255 + .5) / 2;
    QVERIFY(close(result.color->red, expected, 1e-5));
    QVERIFY(close(result.color->green, expected, 1e-5));
    QVERIFY(close(result.color->blue, expected, 1e-5));
    QCOMPARE(result.color->profileId, linear.id());
}

void tst_gradient_evaluator::duplicatesAndEdges()
{
    const auto linear = ColorProfile::linearSRgb();
    GradientDefinition data{QStringLiteral("gradient"), {
        stop(QStringLiteral("a"), 0, linear, 1, 0, 0),
        stop(QStringLiteral("b"), .5, linear, 0, 1, 0),
        stop(QStringLiteral("c"), .5, linear, 0, 0, 1),
        stop(QStringLiteral("d"), 1, linear, 1, 1, 1)}};
    ColorEngine engine;
    auto result = GradientEvaluator::evaluate(data, .25, linear, engine);
    QVERIFY(result.color);
    QVERIFY(close(result.color->red, .5));
    QVERIFY(close(result.color->green, .5));
    QVERIFY(close(result.color->blue, 0));
    result = GradientEvaluator::evaluate(data, .5, linear, engine);
    QVERIFY(result.color);
    QVERIFY(close(result.color->red, 0));
    QVERIFY(close(result.color->green, 0));
    QVERIFY(close(result.color->blue, 1));
    result = GradientEvaluator::evaluate(data, .75, linear, engine);
    QVERIFY(result.color);
    QVERIFY(close(result.color->red, .5));
    QVERIFY(close(result.color->green, .5));
    QVERIFY(close(result.color->blue, 1));
    data.stops.removeLast();
    for (auto &entry : data.stops) entry.position = .5;
    for (double x : {-1.0, std::nextafter(.5, 0.0), .5, std::nextafter(.5, 1.0), 2.0}) {
        result = GradientEvaluator::evaluate(data, x, linear, engine);
        QVERIFY(result.color);
        QVERIFY(close(result.color->red, x < .5 ? 1 : 0));
        QVERIFY(close(result.color->blue, x < .5 ? 0 : 1));
    }
    data.stops.resize(1);
    result = GradientEvaluator::evaluate(data, 1e100, linear, engine);
    QVERIFY(result.color);
    QVERIFY(close(result.color->red, 1));
}

void tst_gradient_evaluator::invalidInputs()
{
    const auto linear = ColorProfile::linearSRgb();
    auto data = gradient(linear);
    ColorEngine engine;
    data.stops[1].color.transferId = QStringLiteral("srgb");
    auto result = GradientEvaluator::evaluate(data, 0, linear, engine);
    QVERIFY(!result.color); // invalid unused stop metadata is still rejected
    QCOMPARE(result.error.code, ErrorCode::Validation);
    QVERIFY(result.error.fieldPath.startsWith(QStringLiteral("stops[1]")));
    data = gradient(linear);
    data.stops[1].id = data.stops[0].id;
    QCOMPARE(GradientEvaluator::validate(data).code, ErrorCode::Validation);
    data = gradient(linear); data.stops[0].position = 1; data.stops[1].position = 0;
    QCOMPARE(GradientEvaluator::validate(data).code, ErrorCode::Validation);
    data = gradient(linear); data.stops[0].color.alpha = 1.1;
    QCOMPARE(GradientEvaluator::validate(data).code, ErrorCode::InvalidArgument);
    data = gradient(linear); data.stops[0].profile = {};
    QCOMPARE(GradientEvaluator::validate(data).code, ErrorCode::Unavailable);
    data = gradient(linear); data.stops[0].color.red = std::numeric_limits<double>::infinity();
    QCOMPARE(GradientEvaluator::validate(data).code, ErrorCode::InvalidArgument);
    data = gradient(linear); data.interpolation = static_cast<GradientInterpolation>(99);
    QCOMPARE(GradientEvaluator::validate(data).code, ErrorCode::Unsupported);
    data = gradient(linear); data.alphaPolicy = static_cast<GradientAlphaPolicy>(99);
    QCOMPARE(GradientEvaluator::validate(data).code, ErrorCode::Unsupported);
    data = gradient(linear);
    QCOMPARE(GradientEvaluator::evaluate(data, .5, {}, engine).error.code, ErrorCode::Unavailable);
    QCOMPARE(GradientEvaluator::evaluate(data, std::numeric_limits<double>::quiet_NaN(), linear, engine).error.code,
             ErrorCode::InvalidArgument);
    data.stops.clear();
    QCOMPARE(GradientEvaluator::validate(data).code, ErrorCode::Validation);
}

void tst_gradient_evaluator::statelessCandidates()
{
    const auto linear = ColorProfile::linearSRgb();
    const auto original = gradient(linear);
    auto candidate = original;
    candidate.stops[1].position = .5;
    ColorEngine engine;
    const auto first = GradientEvaluator::evaluate(original, .25, linear, engine);
    const auto preview = GradientEvaluator::evaluate(candidate, .25, linear, engine);
    const auto restored = GradientEvaluator::evaluate(original, .25, linear, engine);
    QVERIFY(first.color && preview.color && restored.color);
    QVERIFY(close(first.color->red, .25));
    QVERIFY(close(preview.color->red, .5));
    QCOMPARE(first.color->red, restored.color->red);
    QCOMPARE(original.stops[1].position, 1.0);
}

QTEST_GUILESS_MAIN(tst_gradient_evaluator)
#include "tst_gradient_evaluator.moc"
