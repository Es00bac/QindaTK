// SPDX-License-Identifier: LGPL-3.0-or-later
#include "edit_transaction.h"
#include "locale_service.h"
#include "property_document.h"

#include <QCalendar>
#include <QTimeZone>
#include <QtTest>

using namespace QindaTK;

class TstLocaleService final : public QObject {
    Q_OBJECT
private slots:
    void localeSwitchPreservesPendingCanonicalEdit()
    {
        LocaleService locale;
        PropertyDocument document;
        const EntityId entity = EntityId::create();
        QVERIFY(document.defineProperty({QStringLiteral("amount"), QMetaType::fromType<double>(),
                                         12.5, QStringLiteral("unit"), true, false}));
        QVERIFY(document.addEntity(entity, {{QStringLiteral("amount"), 12.5}}));
        EditTransaction edit;
        edit.setDocument(&document);
        edit.setTargetIds({entity.toString()});
        edit.setPropertyId(QStringLiteral("amount"));
        QVERIFY(edit.begin());
        QVERIFY(edit.update(1234.5));
        QCOMPARE(document.committedValue(entity, QStringLiteral("amount")).toDouble(), 12.5);
        QCOMPARE(document.value(entity, QStringLiteral("amount")).toDouble(), 1234.5);

        locale.setLocaleName(QStringLiteral("de_DE"));
        QCOMPARE(locale.localeName(), QStringLiteral("de_DE"));
        QCOMPARE(document.committedValue(entity, QStringLiteral("amount")).toDouble(), 12.5);
        QCOMPARE(document.value(entity, QStringLiteral("amount")).toDouble(), 1234.5);
        QVERIFY(edit.active());
        QVERIFY(locale.formatNumber(1234.5, 1).contains(QLocale(QStringLiteral("de_DE")).decimalPoint()));
        QCOMPARE(locale.parseNumber(locale.formatNumber(1234.5, 1)).value(QStringLiteral("value")).toDouble(),
                 1234.5);
        edit.cancel();
        QCOMPARE(document.value(entity, QStringLiteral("amount")).toDouble(), 12.5);
    }

    void contentBidiAndChromeDirectionAreIndependent()
    {
        LocaleService locale;
        locale.setChromeDirection(QStringLiteral("rtl"));
        QCOMPARE(locale.chromeDirection(), QStringLiteral("rtl"));
        QCOMPARE(locale.contentDirection(QStringLiteral("مرحبا 123")), QStringLiteral("rtl"));
        QCOMPARE(locale.contentDirection(QStringLiteral("abc مرحبا")), QStringLiteral("ltr"));
        QCOMPARE(locale.contentDirection(QStringLiteral("123 مرحبا")), QStringLiteral("rtl"));
        const QString before = locale.chromeDirection();
        locale.setChromeDirection(QStringLiteral("sideways"));
        QCOMPARE(locale.chromeDirection(), before);
    }

    void invalidParseAndUnavailableChoicesAreRejected()
    {
        LocaleService locale;
        const auto badNumber = locale.parseNumber(QStringLiteral("not a number"));
        QVERIFY(!badNumber.value(QStringLiteral("ok")).toBool());
        const auto badDate = locale.parseDateTime(QStringLiteral("31/99/9999"), QStringLiteral("dd/MM/yyyy"));
        QVERIFY(!badDate.value(QStringLiteral("ok")).toBool());
        const QString oldZone = locale.timeZoneId();
        locale.setTimeZoneId(QStringLiteral("Not/A_Real_Zone"));
        QCOMPARE(locale.timeZoneId(), oldZone);
        const QString oldCalendar = locale.calendarId();
        locale.setCalendarId(QStringLiteral("not-a-calendar"));
        QCOMPARE(locale.calendarId(), oldCalendar);
        QVERIFY(!QCalendar::availableCalendars().isEmpty());
        QVERIFY(!QTimeZone::availableTimeZoneIds().isEmpty());
    }

    void timezoneDateFormattingAndUnitFormatting()
    {
        LocaleService locale;
        if (QTimeZone::availableTimeZoneIds().contains(QByteArrayLiteral("America/Los_Angeles")))
            locale.setTimeZoneId(QStringLiteral("America/Los_Angeles"));
        const QDateTime utc(QDate(2024, 1, 1), QTime(12, 0), QTimeZone::utc());
        QVERIFY(locale.formatDateTime(utc, QStringLiteral("yyyy-MM-dd HH:mm")).contains(QStringLiteral("2024")));
        QCOMPARE(locale.formatUnit(2.5, QStringLiteral("kg"), 1), QStringLiteral("2.5 kg"));
    }
};

QTEST_GUILESS_MAIN(TstLocaleService)
#include "tst_locale_service.moc"
