// SPDX-License-Identifier: LGPL-3.0-or-later
#include "density.h"
#include "theme.h"

#include <QMetaProperty>
#include <QtTest>

#include <limits>

using namespace QindaTK;

class TstTheme final : public QObject {
    Q_OBJECT
private slots:
    void init()
    {
        Density::instance()->setMode(Density::Compact);
        Density::instance()->setScaleFonts(false);
        Density::instance()->setTextScale(1.0);
        Theme::instance()->applyPreset(QStringLiteral("sloom-dark"));
    }
    void roleTableCoversEveryProperty()
    {
        const QMetaObject &meta = ThemeColors::staticMetaObject;
        QStringList properties;
        for (int i = meta.propertyOffset(); i < meta.propertyCount(); ++i) {
            if (meta.property(i).userType() == QMetaType::QColor) {
                properties.append(QString::fromLatin1(meta.property(i).name()));
            }
        }
        QStringList roles = ThemeColors::roleNames();
        properties.sort();
        roles.sort();
        QCOMPARE(roles, properties);
    }
    void sloomDarkPreset()
    {
        Theme *theme = Theme::instance();
        QCOMPARE(theme->preset(), QStringLiteral("sloom-dark"));
        QVERIFY(theme->dark());
        QCOMPARE(theme->color()->accent(), QColor(QStringLiteral("#22d3ee")));
        QCOMPARE(theme->color()->bg(), QColor(QStringLiteral("#0b0c10")));
        QVERIFY(theme->color()->chipBg() != theme->color()->panel());
        QCOMPARE(theme->space()->metric(QStringLiteral("md")), 8.0);
        QCOMPARE(theme->size()->metric(QStringLiteral("control")), 24.0);
        QCOMPARE(theme->opacity()->metric(QStringLiteral("hidden")), 0.0);
        QCOMPARE(theme->font()->caption(), 10.0);
        QCOMPARE(theme->font()->body(), 12.0);
    }
    void lightPresetIsLight()
    {
        Theme *theme = Theme::instance();
        QVERIFY(theme->applyPreset(QStringLiteral("sloom-light")));
        QVERIFY(!theme->dark());
        QVERIFY(!theme->applyPreset(QStringLiteral("nope")));
        QCOMPARE(theme->preset(), QStringLiteral("sloom-light"));
    }
    void highContrastPresetsKeepTextAndControlsDistinct()
    {
        Theme *theme = Theme::instance();
        const auto contrast = [theme](const QColor &a, const QColor &b) {
            const qreal la = theme->luminance(a);
            const qreal lb = theme->luminance(b);
            return (qMax(la, lb) + 0.05) / (qMin(la, lb) + 0.05);
        };
        for (const QString &id : {QStringLiteral("high-contrast-dark"),
                                  QStringLiteral("high-contrast-light")}) {
            QVERIFY(theme->applyPreset(id));
            const auto *c = theme->color();
            QVERIFY(contrast(c->text(), c->panel()) >= 4.5);
            QVERIFY(contrast(c->textMuted(), c->panel()) >= 4.5);
            QVERIFY(contrast(c->accentText(), c->panel()) >= 4.5);
            QVERIFY(contrast(c->accentContrast(), c->accent()) >= 4.5);
            QVERIFY(contrast(c->controlBorder(), c->controlBg()) >= 3.0);
            QVERIFY(contrast(c->inputBorder(), c->inputBg()) >= 3.0);
            QVERIFY(contrast(c->focus(), c->panel()) >= 3.0);
            QVERIFY(contrast(c->scrollThumb(), c->scrollTrack()) >= 3.0);
            QCOMPARE(theme->opacity()->metric(QStringLiteral("disabled")), 1.0);
        }
        QVERIFY(theme->applyPreset(QStringLiteral("sloom-dark")));
        QCOMPARE(theme->opacity()->metric(QStringLiteral("disabled")), 0.45);
    }
    void densityRescalesMetricsNotRadius()
    {
        Theme *theme = Theme::instance();
        QSignalSpy spy(theme, &Theme::changed);
        Density::instance()->setMode(Density::Comfortable);
        QVERIFY(spy.count() >= 1);
        QCOMPARE(theme->space()->metric(QStringLiteral("md")), 10.0);
        QCOMPARE(theme->size()->metric(QStringLiteral("control")), 29.0);
        QCOMPARE(theme->radius()->metric(QStringLiteral("md")), 6.0);
        QCOMPARE(theme->font()->body(), 12.0);
        Density::instance()->setScaleFonts(true);
        QCOMPARE(theme->font()->body(), 14.0);
        Density::instance()->setScaleFonts(false);
    }
    void independentTextScaleComposesWithDensityAndResets()
    {
        Theme *theme = Theme::instance();
        Density *density = Density::instance();
        density->setMode(Density::Comfortable);
        density->setTextScale(1.5);
        QCOMPARE(theme->font()->body(), 18.0);
        QCOMPARE(theme->size()->metric(QStringLiteral("control")), 29.0);

        density->setScaleFonts(true);
        QCOMPARE(theme->font()->body(), 22.0);
        QCOMPARE(theme->font()->caption(), 18.0);

        density->setTextScale(1.0);
        QCOMPARE(theme->font()->body(), 14.0);
        density->setScaleFonts(false);
        QCOMPARE(theme->font()->body(), 12.0);
        density->setMode(Density::Compact);
    }
    void textScaleClampsAndIgnoresNonFiniteValues()
    {
        Density *density = Density::instance();
        Theme *theme = Theme::instance();
        density->setTextScale(99.0);
        QCOMPARE(density->textScale(), 3.0);
        QCOMPARE(theme->font()->body(), 36.0);
        density->setTextScale(-2.0);
        QCOMPARE(density->textScale(), 0.5);
        QCOMPARE(theme->font()->body(), 6.0);
        density->setTextScale(std::numeric_limits<qreal>::quiet_NaN());
        QCOMPARE(density->textScale(), 0.5);
        density->setTextScale(std::numeric_limits<qreal>::infinity());
        QCOMPARE(density->textScale(), 0.5);
        density->setTextScale(1.0);
    }
    void textScaleKeepsJsonAndQstFontBases()
    {
        Theme *theme = Theme::instance();
        Density *density = Density::instance();
        density->setTextScale(1.5);
        QVERIFY(theme->loadJson(QStringLiteral(
            R"({"colors":{"bg":"#101010"},"font":{"body":13}})")));
        QCOMPARE(theme->font()->body(), 20.0);
        density->setTextScale(1.0);
        QCOMPARE(theme->font()->body(), 13.0);

        density->setTextScale(1.5);
        const QVariantMap tokens{
            {QStringLiteral("bg"), QVariantMap{{QStringLiteral("base"), QColor("#101010")}}},
            {QStringLiteral("fg"), QVariantMap{{QStringLiteral("default"), QColor("#eeeeee")}}},
            {QStringLiteral("accent"), QVariantMap{{QStringLiteral("default"), QColor("#3399ff")},
                                                    {QStringLiteral("fg"), QColor("#000000")}}},
            {QStringLiteral("type"), QVariantMap{{QStringLiteral("caption"), 9.0},
                                                  {QStringLiteral("body"), 10.5}}},
        };
        QVERIFY(theme->applyQst(tokens));
        QCOMPARE(theme->font()->body(), 21.0);
        density->setTextScale(1.0);
        QCOMPARE(theme->font()->body(), 14.0);
    }
    void applyRolesDerives()
    {
        Theme *theme = Theme::instance();
        theme->applyRoles({{QStringLiteral("accent"), QColor(QStringLiteral("#ff8800"))}});
        QCOMPARE(theme->color()->accent(), QColor(QStringLiteral("#ff8800")));
        QVERIFY(qAbs(theme->color()->focus().alphaF() - 0.65) < 0.001);
        QCOMPARE(theme->color()->focus().red(), 0xff);
        QCOMPARE(theme->preset(), QStringLiteral("custom"));
    }
    void applyQstMapsRoles()
    {
        Theme *theme = Theme::instance();
        const QVariantMap tokens{
            {QStringLiteral("bg"), QVariantMap{{QStringLiteral("base"), QColor(QStringLiteral("#101010"))},
                                               {QStringLiteral("raised"), QColor(QStringLiteral("#181818"))},
                                               {QStringLiteral("highest"), QColor(QStringLiteral("#202020"))}}},
            {QStringLiteral("fg"), QVariantMap{{QStringLiteral("default"), QColor(QStringLiteral("#eeeeee"))},
                                               {QStringLiteral("muted"), QColor(QStringLiteral("#999999"))}}},
            {QStringLiteral("accent"), QVariantMap{{QStringLiteral("default"), QColor(QStringLiteral("#3399ff"))},
                                                   {QStringLiteral("fg"), QColor(QStringLiteral("#000000"))}}},
            {QStringLiteral("type"), QVariantMap{{QStringLiteral("caption"), 9.0}, {QStringLiteral("body"), 10.5}}},
        };
        QVERIFY(theme->applyQst(tokens));
        QCOMPARE(theme->color()->bg(), QColor(QStringLiteral("#101010")));
        QCOMPARE(theme->color()->panel(), QColor(QStringLiteral("#202020")));
        QCOMPARE(theme->color()->accent(), QColor(QStringLiteral("#3399ff")));
        QCOMPARE(theme->font()->caption(), 12.0);
        QCOMPARE(theme->font()->body(), 14.0);
        QVERIFY(!theme->applyQst({}));
    }
    void loadsJsonTheme()
    {
        Theme *theme = Theme::instance();
        const QString json = QStringLiteral(R"({"name": "Test", "dark": false,
            "colors": {"bg": "#ffffff", "panel": "#f0f0f0", "accent": "#0000ff", "scrollThumb": "#123456"},
            "font": {"body": 13}, "space": {"md": 9}})");
        QVERIFY(theme->loadJson(json));
        QCOMPARE(theme->name(), QStringLiteral("Test"));
        QVERIFY(!theme->dark());
        QCOMPARE(theme->color()->scrollThumb(), QColor(QStringLiteral("#123456")));
        QCOMPARE(theme->font()->body(), 13.0);
        QCOMPARE(theme->space()->metric(QStringLiteral("md")), 9.0);
        QVERIFY(!theme->loadJson(QStringLiteral("{ nope")));
    }
    void colorMath()
    {
        Theme *theme = Theme::instance();
        QCOMPARE(theme->mix(QColor(0, 0, 0), QColor(255, 255, 255), 0.5).red(), 128);
        QVERIFY(qAbs(theme->alpha(QColor(Qt::red), 0.25).alphaF() - 0.25) < 0.001);
        QCOMPARE(theme->onColor(QColor(Qt::white)), QColor(QStringLiteral("#0b0c10")));
        QCOMPARE(theme->onColor(QColor(Qt::black)), QColor(QStringLiteral("#f3f7fb")));
    }
    void reducedMotionZeroesDurations()
    {
        Theme *theme = Theme::instance();
        theme->setReducedMotion(true);
        QCOMPARE(theme->motion()->metric(QStringLiteral("base")), 0.0);
        QCOMPARE(theme->motion()->metric(QStringLiteral("tooltipDelay")), 0.0);
        QCOMPARE(theme->motion()->metric(QStringLiteral("tooltipTimeout")), 6000.0);
        theme->setReducedMotion(false);
        QCOMPARE(theme->motion()->metric(QStringLiteral("base")), 140.0);
        QCOMPARE(theme->motion()->metric(QStringLiteral("tooltipDelay")), 600.0);
    }
};

QTEST_MAIN(TstTheme)
#include "tst_theme.moc"
