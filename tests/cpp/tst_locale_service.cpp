// SPDX-License-Identifier: LGPL-3.0-or-later
#include "edit_transaction.h"
#include "locale_service.h"
#include "property_document.h"
#include "undo_service.h"

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
        UndoService undo;
        QVERIFY(undo.addDocument(&document));
        EditTransaction edit;
        edit.setDocument(&document);
        edit.setUndoService(&undo);
        edit.setTargetIds({entity.toString()});
        edit.setPropertyId(QStringLiteral("amount"));
        edit.setLabel(QStringLiteral("Edit amount"));
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
        locale.setLocaleName(QStringLiteral("en_US"));
        if (QTimeZone::availableTimeZoneIds().contains(QByteArrayLiteral("America/Los_Angeles")))
            locale.setTimeZoneId(QStringLiteral("America/Los_Angeles"));
        const QDateTime utc(QDate(2024, 1, 1), QTime(12, 0), QTimeZone::utc());
        QVERIFY(locale.formatDateTime(utc, QStringLiteral("yyyy-MM-dd HH:mm")).contains(QStringLiteral("2024")));
        QCOMPARE(locale.formatUnit(2.5, QStringLiteral("kg"), 1), QStringLiteral("2.5 kg"));
    }

    void classifiesDstOverlapGapAndPreservesDateOnlyValues()
    {
        if (!QTimeZone::availableTimeZoneIds().contains(QByteArrayLiteral("America/New_York")))
            QSKIP("America/New_York time-zone data unavailable");
        LocaleService locale;
        locale.setLocaleName(QStringLiteral("en_US"));
        locale.setTimeZoneId(QStringLiteral("America/New_York"));
        const QString pattern = QStringLiteral("yyyy-MM-dd HH:mm");
        const auto overlap = locale.classifyLocalDateTime(QStringLiteral("2024-11-03 01:30"), pattern);
        QCOMPARE(overlap.value(QStringLiteral("status")).toString(), QStringLiteral("ambiguous"));
        const auto candidates = overlap.value(QStringLiteral("candidates")).toList();
        QCOMPARE(candidates.size(), 2);
        QVERIFY(candidates.at(0).toString() != candidates.at(1).toString());
        const auto gap = locale.classifyLocalDateTime(QStringLiteral("2024-03-10 02:30"), pattern);
        QCOMPARE(gap.value(QStringLiteral("status")).toString(), QStringLiteral("nonexistent"));
        QCOMPARE(gap.value(QStringLiteral("candidates")).toList().size(), 0);
        const auto normal = locale.classifyLocalDateTime(QStringLiteral("2024-03-11 02:30"), pattern);
        QCOMPARE(normal.value(QStringLiteral("status")).toString(), QStringLiteral("valid"));
        QCOMPARE(normal.value(QStringLiteral("candidates")).toList().size(), 1);
        QCOMPARE(locale.parseDate(locale.formatDate(QStringLiteral("2024-02-29"),
                                                   QStringLiteral("MMMM d, yyyy")),
                                  QStringLiteral("MMMM d, yyyy"))
                     .value(QStringLiteral("value")).toString(), QStringLiteral("2024-02-29"));
        QVERIFY(!locale.parseDate(QStringLiteral("February 29, 2023"),
                                  QStringLiteral("MMMM d, yyyy"))
                     .value(QStringLiteral("ok")).toBool());
    }

    void monthGridHasStableIsoDatesAndConfiguredBounds()
    {
        LocaleService locale;
        locale.setLocaleName(QStringLiteral("en_US"));
        const auto grid = locale.monthGrid(2024, 2, QStringLiteral("2024-02-10"),
                                           QStringLiteral("2024-02-20"));
        QVERIFY(grid.value(QStringLiteral("ok")).toBool());
        QCOMPARE(grid.value(QStringLiteral("firstDayOfWeek")).toInt(), 7);
        QCOMPARE(grid.value(QStringLiteral("weekdayLabels")).toList().size(), 7);
        const auto cells = grid.value(QStringLiteral("cells")).toList();
        QCOMPARE(cells.size(), 42);
        bool sawLeapDay = false;
        bool sawEnabled = false;
        for (const auto &cellValue : cells) {
            const auto cell = cellValue.toMap();
            const QString date = cell.value(QStringLiteral("date")).toString();
            if (date == QStringLiteral("2024-02-29")) {
                sawLeapDay = true;
                QVERIFY(cell.value(QStringLiteral("inMonth")).toBool());
                QVERIFY(!cell.value(QStringLiteral("enabled")).toBool());
            }
            if (date == QStringLiteral("2024-02-15"))
                sawEnabled = cell.value(QStringLiteral("enabled")).toBool();
        }
        QVERIFY(sawLeapDay && sawEnabled);
        QVERIFY(!locale.monthGrid(2023, 2, QStringLiteral("bad"))
                     .value(QStringLiteral("ok")).toBool());
        const auto next = locale.shiftMonth(2024, 12, 1);
        QVERIFY(next.value(QStringLiteral("ok")).toBool());
        QCOMPARE(next.value(QStringLiteral("year")).toInt(), 2025);
        QCOMPARE(next.value(QStringLiteral("month")).toInt(), 1);
        const auto previous = locale.shiftMonth(2025, 1, -1);
        QCOMPARE(previous.value(QStringLiteral("year")).toInt(), 2024);
        QCOMPARE(previous.value(QStringLiteral("month")).toInt(), 12);
        const auto fields = locale.calendarFields(QStringLiteral("2024-02-29"));
        QVERIFY(fields.value(QStringLiteral("ok")).toBool());
        QCOMPARE(fields.value(QStringLiteral("year")).toInt(), 2024);
        QCOMPARE(fields.value(QStringLiteral("month")).toInt(), 2);
        QCOMPARE(fields.value(QStringLiteral("day")).toInt(), 29);
        QVERIFY(QDate::fromString(locale.todayDateIso(), Qt::ISODate).isValid());
    }
};

QTEST_GUILESS_MAIN(TstLocaleService)
#include "tst_locale_service.moc"
