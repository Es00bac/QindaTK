// SPDX-License-Identifier: LGPL-3.0-or-later
#include <QtTest>
#include <QFile>
#include <QtEndian>
#include <cmath>
#include <limits>

#include "color_engine.h"

using namespace QindaTK;

namespace {
QByteArray fixture(const char *name)
{
    QFile file(QString::fromUtf8(QINDATK_COLOR_FIXTURES) + QLatin1Char('/')
               + QString::fromLatin1(name));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
ColorTransformRequest requestFor(const ColorProfile &source, const ColorProfile &target,
                                 double gray = 0.5, double alpha = 1.0)
{
    ColorTransformRequest request;
    request.source = source;
    request.context.document = source;
    request.context.working = target;
    request.context.generation = 42;
    request.value = {gray, gray, gray, alpha, source.id(), source.transferId()};
    return request;
}
// Independent piecewise sRGB equations; no production-engine oracle.
double decode(double value)
{
    return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4);
}
// ICC encodes these parameters as signed 16.16 fixed point. These exact
// coefficients are recorded in the fixture README, independently of LCMS.
constexpr double g = 157286.0 / 65536;
constexpr double a = 62119.0 / 65536;
constexpr double b = 3417.0 / 65536;
constexpr double c = 5072.0 / 65536;
constexpr double knee = 2651.0 / 65536;
double iccDecode(double value)
{
    return value < knee ? c * value : std::pow(a * value + b, g);
}
double iccEncode(double value)
{
    return value < std::pow(a * knee + b, g) ? value / c
                                            : (std::pow(value, 1.0 / g) - b) / a;
}
}

class tst_color_engine : public QObject {
    Q_OBJECT
private slots:
    void profileOwnership();
    void malformedAndUnsupportedProfiles();
    void knownAnswers();
    void builtinProfiles();
    void errors();
    void destinationsAndCache();
};

void tst_color_engine::profileOwnership()
{
    auto bytes = fixture("srgb.icc");
    QVERIFY(!bytes.isEmpty());
    const auto original = bytes;
    OperationError error;
    const auto profile = ColorProfile::fromIcc(QByteArray::fromRawData(bytes.data(), bytes.size()), &error);
    QVERIFY2(profile.isValid(), qPrintable(error.message));
    QVERIFY(!error);
    QVERIFY(profile.id().startsWith(QStringLiteral("icc-sha256:")));
    QCOMPARE(profile.id(), ColorProfile::fromIcc(original).id());
    bytes.fill('\0');
    QCOMPARE(profile.iccBytes(), original);
    QCOMPARE(profile.transferId(), QStringLiteral("icc"));
    QVERIFY(!profile.description().isEmpty());
}

void tst_color_engine::malformedAndUnsupportedProfiles()
{
    const auto bytes = fixture("srgb.icc");
    QList<QByteArray> bad{QByteArray{}, QByteArrayLiteral("not ICC"), bytes.left(bytes.size() - 1)};
    auto invalidTag = bytes;
    qToBigEndian<quint32>(0xfffffff0u, invalidTag.data() + 136);
    bad.append(invalidTag);
    auto badHeader = bytes;
    badHeader[36] = 'x';
    bad.append(badHeader);
    for (const auto &value : bad) {
        OperationError error;
        QVERIFY(!ColorProfile::fromIcc(value, &error).isValid());
        QCOMPARE(error.code, ErrorCode::Validation);
    }
    OperationError error;
    QVERIFY(!ColorProfile::fromIcc(fixture("gray.icc"), &error).isValid());
    QCOMPARE(error.code, ErrorCode::Unsupported);
}

void tst_color_engine::knownAnswers()
{
    const auto srgb = ColorProfile::fromIcc(fixture("srgb.icc"));
    const auto linear = ColorProfile::fromIcc(fixture("linear-srgb.icc"));
    QVERIFY(srgb.isValid());
    QVERIFY(linear.isValid());
    ColorEngine engine;
    for (double alpha : {0.0, 0.25, 0.1234567891011, 1.0}) {
        for (double channel : {-.1, 0.0, .0031307, .0031308, .0031309,
                               .0404499, .04045, .0404501, .5, 1.0, 1.2}) {
            for (bool reverse : {false, true}) {
                const auto request = requestFor(reverse ? linear : srgb, reverse ? srgb : linear,
                                                channel, alpha);
                const auto result = engine.convert(request);
                QVERIFY2(result.value.has_value(), qPrintable(result.error.message));
                QVERIFY(!result.error);
                const double expected = reverse ? iccEncode(channel) : iccDecode(channel);
                QVERIFY2(std::abs(result.value->red - expected) < 2e-6,
                         qPrintable(QStringLiteral("input=%1 reverse=%2 actual=%3 expected=%4")
                             .arg(channel, 0, 'g', 12).arg(reverse)
                             .arg(result.value->red, 0, 'g', 12).arg(expected, 0, 'g', 12)));
                QVERIFY(std::abs(result.value->green - expected) < 2e-6);
                QVERIFY(std::abs(result.value->blue - expected) < 2e-6);
                QCOMPARE(result.value->alpha, alpha);
                QCOMPARE(result.generation, quint64(42));
                QCOMPARE(result.precision, ColorPrecision::Float32);
                QCOMPARE(result.gamut, ColorGamutStatus::Unknown);
                QCOMPARE(result.outsideNominalRange, expected < 0 || expected > 1);
            }
        }
    }
    auto colored = requestFor(srgb, linear, 0.0, 0.0);
    colored.value.red = 1.0;
    colored.value.green = .5;
    const auto result = engine.convert(colored);
    QVERIFY(result.value);
    QVERIFY(std::abs(result.value->red - 1) < 2e-6);
    QVERIFY(std::abs(result.value->green - .21404114048223255) < 1e-5);
    QVERIFY(std::abs(result.value->blue) < 2e-6);
}

void tst_color_engine::builtinProfiles()
{
    const auto srgb = ColorProfile::sRgb();
    const auto linear = ColorProfile::linearSRgb();
    QVERIFY(srgb.isValid());
    QVERIFY(linear.isValid());
    ColorEngine engine;
    auto result = engine.convert(requestFor(srgb, linear));
    QVERIFY2(result.value.has_value(), qPrintable(result.error.message));
    QVERIFY2(std::abs(result.value->red - .21404114048223255) < 1e-5,
             qPrintable(QString::number(result.value->red, 'g', 12)));
    result = engine.convert(requestFor(linear, srgb));
    QVERIFY(result.value);
    QVERIFY(std::abs(result.value->red - .7353569830524495) < 1e-5);
}

void tst_color_engine::errors()
{
    const auto source = ColorProfile::sRgb();
    const auto target = ColorProfile::linearSRgb();
    ColorEngine engine;
    for (double alpha : {-0.01, 1.01, std::numeric_limits<double>::quiet_NaN()}) {
        const auto result = engine.convert(requestFor(source, target, .5, alpha));
        QVERIFY(!result.value);
        QCOMPARE(result.error.code, ErrorCode::InvalidArgument);
    }
    for (double rgb : {std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::max()}) {
        const auto result = engine.convert(requestFor(source, target, rgb));
        QVERIFY(!result.value);
        QCOMPARE(result.error.code, ErrorCode::InvalidArgument);
    }
    auto request = requestFor(source, target);
    request.value.transferId = QStringLiteral("linear");
    auto result = engine.convert(request);
    QVERIFY(!result.value);
    QCOMPARE(result.error.code, ErrorCode::Validation);
    request = requestFor(source, target);
    request.value.profileId = target.id();
    QCOMPARE(engine.convert(request).error.code, ErrorCode::Validation);
    request = requestFor(source, target);
    request.operation = ColorOperation::ProofPreview;
    QCOMPARE(engine.convert(request).error.code, ErrorCode::Unsupported);
    request.operation = ColorOperation::Output;
    QCOMPARE(engine.convert(request).error.code, ErrorCode::Unavailable);
    request = requestFor(source, target);
    request.context.intent = ColorIntent::AbsoluteColorimetric;
    QCOMPARE(engine.convert(request).error.code, ErrorCode::Unsupported);
    request.context.intent = ColorIntent::RelativeColorimetric;
    request.context.blackPointCompensation = true;
    QCOMPARE(engine.convert(request).error.code, ErrorCode::Unsupported);
}

void tst_color_engine::destinationsAndCache()
{
    const auto source = ColorProfile::sRgb();
    const auto target = ColorProfile::linearSRgb();
    ColorEngine engine(1);
    auto request = requestFor(source, target);
    request.context.display = source;
    request.context.output = target;
    for (auto operation : {ColorOperation::Working, ColorOperation::Display,
                           ColorOperation::Output, ColorOperation::Display}) {
        request.operation = operation;
        const auto result = engine.convert(request);
        QVERIFY2(result.value.has_value(), qPrintable(result.error.message));
        QCOMPARE(result.operation, operation);
        const bool identity = operation == ColorOperation::Display;
        QVERIFY(std::abs(result.value->red - (identity ? .5 : decode(.5))) < 1e-5);
        QCOMPARE(result.value->profileId, identity ? source.id() : target.id());
        QCOMPARE(engine.cachedTransformCount(), 1);
    }
    request.context.generation = 43;
    QCOMPARE(engine.convert(request).generation, quint64(43));
    QCOMPARE(engine.cachedTransformCount(), 1);
}

QTEST_GUILESS_MAIN(tst_color_engine)
#include "tst_color_engine.moc"
