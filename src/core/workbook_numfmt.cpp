// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_numfmt.h"

#include <QDate>
#include <QHash>
#include <QLocale>
#include <QTime>
#include <cmath>
#include <memory>
#include <vector>

namespace QindaTK::Calc {
namespace {
constexpr double MaxDateSerial = 2958466.0; // 10000-01-01

struct Part {
    enum class Type { Literal, Year, Month, Day, Hour, Minute, Second, AmPm };
    Type type = Type::Literal;
    int width = 0;
    QString text;
};

struct Parsed {
    enum class Kind { General, Text, Number, Date };
    Kind kind = Kind::General;
    QString problem;
    // Number
    QString prefix, suffix;
    int integerDigits = 1, decimals = 0, exponentDigits = 0;
    bool grouping = false, percent = false, scientific = false;
    // Date
    std::vector<Part> parts;
    bool twelveHour = false;
};

bool isLiteralChar(QChar c) { return QStringLiteral("$€£¥-+()/:,. ").contains(c); }
bool isDateChar(QChar c) { return QStringLiteral("ymdhsYMDHS").contains(c); }

Parsed parse(const QString &format)
{
    Parsed p;
    if (format.isEmpty() || format.compare(QLatin1StringView("General"), Qt::CaseInsensitive) == 0) return p;
    if (format == u"@") { p.kind = Parsed::Kind::Text; return p; }
    if (format.size() > MaxNumberFormatLength) {
        p.problem = QStringLiteral("A number format has at most %1 characters").arg(MaxNumberFormatLength);
        return p;
    }
    bool digitsSeen = false, dateSeen = false;
    QString literal;
    const auto flushLiteral = [&]() {
        if (literal.isEmpty()) return;
        p.parts.push_back({Part::Type::Literal, 0, literal});
        (digitsSeen ? p.suffix : p.prefix) += literal;
        literal.clear();
    };
    const qsizetype n = format.size();
    for (qsizetype i = 0; i < n && p.problem.isEmpty();) {
        const QChar c = format[i];
        if (c == u'"') {
            const qsizetype close = format.indexOf(QLatin1Char('"'), i + 1);
            if (close < 0) { p.problem = QStringLiteral("A quoted text in the format is not closed"); break; }
            literal += format.mid(i + 1, close - i - 1);
            i = close + 1;
        } else if (c == u'\\' && i + 1 < n) {
            literal += format[i + 1];
            i += 2;
        } else if (format.mid(i, 5).compare(QLatin1StringView("AM/PM"), Qt::CaseInsensitive) == 0) {
            flushLiteral();
            p.parts.push_back({Part::Type::AmPm, 5, {}});
            p.twelveHour = dateSeen = true;
            i += 5;
        } else if (c == u'#' || c == u'0' || (c == u'.' && i + 1 < n && format[i + 1] == u'0' && !dateSeen)) {
            if (digitsSeen || dateSeen) { p.problem = QStringLiteral("A number format has one digit pattern"); break; }
            flushLiteral();
            qsizetype j = i;
            p.integerDigits = 0;
            while (j < n && (format[j] == u'#' || format[j] == u'0'
                             || (format[j] == u',' && j + 1 < n && (format[j + 1] == u'#' || format[j + 1] == u'0')))) {
                if (format[j] == u'0') ++p.integerDigits;
                if (format[j] == u',') p.grouping = true;
                ++j;
            }
            if (j < n && format[j] == u'.') {
                ++j;
                while (j < n && format[j] == u'0') { ++p.decimals; ++j; }
                if (p.decimals > 15) { p.problem = QStringLiteral("A number format has at most 15 decimals"); break; }
            }
            if (j + 1 < n && (format[j] == u'E' || format[j] == u'e') && (format[j + 1] == u'+' || format[j + 1] == u'-')) {
                j += 2;
                while (j < n && format[j] == u'0') { ++p.exponentDigits; ++j; }
                if (p.exponentDigits == 0 || p.exponentDigits > 3) { p.problem = QStringLiteral("An exponent needs 1 to 3 zeros"); break; }
                p.scientific = true;
            }
            digitsSeen = true;
            i = j;
        } else if (c == u'%') {
            if (!digitsSeen) { p.problem = QStringLiteral("'%' follows a digit pattern"); break; }
            p.percent = true;
            literal += c;
            ++i;
        } else if (isDateChar(c)) {
            if (digitsSeen) { p.problem = QStringLiteral("A format is either a number or a date"); break; }
            flushLiteral();
            qsizetype j = i;
            while (j < n && format[j].toLower() == c.toLower()) ++j;
            const int width = int(j - i);
            const QChar k = c.toLower();
            const Part::Type type = k == u'y' ? Part::Type::Year : k == u'm' ? Part::Type::Month
                : k == u'd' ? Part::Type::Day : k == u'h' ? Part::Type::Hour : Part::Type::Second;
            if (width > (type == Part::Type::Month || type == Part::Type::Day || type == Part::Type::Year ? 4 : 2)) {
                p.problem = QStringLiteral("Too many '%1' in the date format").arg(c);
                break;
            }
            p.parts.push_back({type, width, {}});
            dateSeen = true;
            i = j;
        } else if (isLiteralChar(c)) {
            literal += c;
            ++i;
        } else {
            p.problem = QStringLiteral("Unsupported character '%1' in the number format").arg(c);
        }
    }
    flushLiteral();
    if (!p.problem.isEmpty()) return p;
    if (dateSeen) {
        p.kind = Parsed::Kind::Date;
        // "m" is minutes after an hour or before a second.
        int previous = -1;
        for (size_t i = 0; i < p.parts.size(); ++i) {
            Part &part = p.parts[i];
            if (part.type == Part::Type::Literal) continue;
            if (part.type == Part::Type::Month && part.width <= 2) {
                size_t next = i + 1;
                while (next < p.parts.size() && p.parts[next].type == Part::Type::Literal) ++next;
                if ((previous >= 0 && p.parts[size_t(previous)].type == Part::Type::Hour)
                    || (next < p.parts.size() && p.parts[next].type == Part::Type::Second))
                    part.type = Part::Type::Minute;
            }
            previous = int(i);
        }
        return p;
    }
    if (!digitsSeen) { p.problem = QStringLiteral("A number format needs a digit pattern such as 0 or a date part"); return p; }
    p.kind = Parsed::Kind::Number;
    return p;
}

std::shared_ptr<const Parsed> parsed(const QString &format)
{
    // AGENT-NOTE: per-thread cache; formats are few and immutable once parsed.
    thread_local QHash<QString, std::shared_ptr<const Parsed>> cache;
    if (const auto it = cache.constFind(format); it != cache.cend()) return *it;
    if (cache.size() >= 256) cache.clear();
    auto result = std::make_shared<const Parsed>(parse(format));
    cache.insert(format, result);
    return result;
}

QString grouped(const QString &digits)
{
    QString out;
    for (qsizetype i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += QLatin1Char(',');
        out += digits[i];
    }
    return out;
}

QString formatNumberPattern(const Parsed &p, double value)
{
    double x = p.percent ? value * 100.0 : value;
    QString body;
    bool negative = false;
    if (p.scientific) {
        int exponent = 0;
        double mantissa = 0.0;
        if (x != 0.0) {
            exponent = int(std::floor(std::log10(std::fabs(x))));
            mantissa = roundDecimal(x / std::pow(10.0, exponent), p.decimals);
            if (std::fabs(mantissa) >= 10.0) { mantissa /= 10.0; ++exponent; }
        }
        negative = mantissa < 0.0;
        QString digits = QString::number(std::abs(exponent));
        while (digits.size() < p.exponentDigits) digits.prepend(QLatin1Char('0'));
        body = QString::number(std::fabs(mantissa), 'f', p.decimals) + QLatin1Char('E')
            + (exponent < 0 ? QLatin1Char('-') : QLatin1Char('+')) + digits;
    } else {
        const double rounded = roundDecimal(x, p.decimals);
        negative = rounded < 0.0;
        const QString text = QString::number(std::fabs(rounded), 'f', p.decimals);
        const qsizetype dot = text.indexOf(QLatin1Char('.'));
        QString integer = dot < 0 ? text : text.left(dot);
        const QString fraction = dot < 0 ? QString() : text.mid(dot);
        if (integer == u"0" && p.integerDigits == 0) integer.clear();
        while (integer.size() < p.integerDigits) integer.prepend(QLatin1Char('0'));
        body = (p.grouping ? grouped(integer) : integer) + fraction;
    }
    return (negative ? QStringLiteral("-") : QString()) + p.prefix + body + p.suffix;
}

QString formatDate(const Parsed &p, double serial)
{
    if (!(serial >= 0.0 && serial < MaxDateSerial)) return QStringLiteral("######");
    qint64 seconds = std::llround(serial * 86400.0);
    const qint64 days = seconds / 86400;
    seconds %= 86400;
    const QDate date = QDate(1899, 12, 30).addDays(days);
    const int hour = int(seconds / 3600), minute = int(seconds / 60 % 60), second = int(seconds % 60);
    const QLocale c = QLocale::c();
    const auto padded = [](int number, int width) {
        return width >= 2 ? QStringLiteral("%1").arg(number, 2, 10, QLatin1Char('0')) : QString::number(number);
    };
    QString out;
    for (const Part &part : p.parts) {
        switch (part.type) {
        case Part::Type::Literal: out += part.text; break;
        case Part::Type::Year:
            out += part.width <= 2 ? padded(date.year() % 100, 2) : QString::number(date.year());
            break;
        case Part::Type::Month:
            out += part.width <= 2 ? padded(date.month(), part.width)
                 : c.monthName(date.month(), part.width == 3 ? QLocale::ShortFormat : QLocale::LongFormat);
            break;
        case Part::Type::Day:
            out += part.width <= 2 ? padded(date.day(), part.width)
                 : c.dayName(date.dayOfWeek(), part.width == 3 ? QLocale::ShortFormat : QLocale::LongFormat);
            break;
        case Part::Type::Hour: {
            const int shown = p.twelveHour ? (hour % 12 == 0 ? 12 : hour % 12) : hour;
            out += padded(shown, part.width);
            break;
        }
        case Part::Type::Minute: out += padded(minute, part.width); break;
        case Part::Type::Second: out += padded(second, part.width); break;
        case Part::Type::AmPm: out += hour < 12 ? QStringLiteral("AM") : QStringLiteral("PM"); break;
        }
    }
    return out;
}
} // namespace

double roundDecimal(double number, int digits)
{
    if (digits > 15) return number;
    if (digits < -308) return 0.0;
    const double scale = std::pow(10.0, std::abs(digits));
    double scaled = digits >= 0 ? number * scale : number / scale;
    // AGENT-NOTE: rounding the scaled value to 15 significant digits first
    // removes binary representation error (2.675 * 100 = 267.49999999999997)
    // so 2.675 rounds to 2.68, matching decimal half-away-from-zero.
    if (std::isfinite(scaled)) scaled = QString::number(scaled, 'g', 15).toDouble();
    const double rounded = std::round(scaled);
    return digits >= 0 ? rounded / scale : rounded * scale;
}

QString numberFormatProblem(const QString &format) { return parsed(format)->problem; }

QString formatValue(const Value &value, const QString &format)
{
    if (!value.isNumber() || format.isEmpty()) return displayText(value);
    const auto p = parsed(format);
    if (!p->problem.isEmpty()) return displayText(value);
    switch (p->kind) {
    case Parsed::Kind::Number: return formatNumberPattern(*p, value.number);
    case Parsed::Kind::Date: return formatDate(*p, value.number);
    default: return displayText(value);
    }
}

} // namespace QindaTK::Calc
