// SPDX-License-Identifier: LGPL-3.0-or-later
#include "print_ticket.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QUrl>
#include <cmath>

namespace QindaTK::Print {
namespace {

constexpr double PointsPerMillimetre = 72.0 / 25.4;

QString tr(const char *text) { return QCoreApplication::translate("QindaTK::Print", text); }

bool fail(OperationError *error, ErrorCode code, const QString &field, const QString &message)
{
    if (error) *error = {code, message, field, QStringLiteral("print")};
    return false;
}

bool readText(const QVariantMap &map, const QString &key, const QStringList &allowed, QString *out,
              OperationError *error)
{
    if (!map.contains(key)) return true;
    const QString value = map.value(key).toString();
    if (value.isEmpty() || (!allowed.isEmpty() && !allowed.contains(value)))
        return fail(error, ErrorCode::InvalidArgument, key, tr("Unknown value \"%1\" for %2").arg(value, key));
    *out = value;
    return true;
}

bool readBool(const QVariantMap &map, const QString &key, bool *out)
{
    if (map.contains(key)) *out = map.value(key).toBool();
    return true;
}

QVariantList options(const QString &key, const QStringList &values)
{
    QVariantList list;
    for (const QString &value : values)
        list.append(QVariantMap{{QStringLiteral("id"), value}, {QStringLiteral("label"), optionLabel(key, value)}});
    return list;
}

} // namespace

QVector<Paper> standardPapers()
{
    const auto mm = [](double w, double h) { return QSizeF(w * PointsPerMillimetre, h * PointsPerMillimetre); };
    return {{QStringLiteral("document"), tr("Document page size"), {}},
            {QStringLiteral("a3"), QStringLiteral("A3"), mm(297, 420)},
            {QStringLiteral("a4"), QStringLiteral("A4"), mm(210, 297)},
            {QStringLiteral("a5"), QStringLiteral("A5"), mm(148, 210)},
            {QStringLiteral("letter"), tr("Letter"), QSizeF(612, 792)},
            {QStringLiteral("legal"), tr("Legal"), QSizeF(612, 1008)},
            {QStringLiteral("tabloid"), tr("Tabloid"), QSizeF(792, 1224)}};
}

Paper resolvePaper(const Capabilities &destination, const QString &id)
{
    for (const Paper &paper : destination.papers)
        if (paper.id == id) return paper;
    const QVector<Paper> standard = standardPapers();
    for (const Paper &paper : standard)
        if (paper.id == id) return paper;
    return standard.first();
}

std::optional<QVector<PageRange>> parsePageRanges(const QString &text, OperationError *error)
{
    static const QRegularExpression part(QStringLiteral("^\\s*(\\d{1,6})\\s*(?:(-)\\s*(\\d{1,6})?)?\\s*$"));
    QVector<PageRange> ranges;
    if (text.trimmed().isEmpty()) return ranges;
    for (const QString &piece : text.split(u',')) {
        const QRegularExpressionMatch match = part.match(piece);
        if (!match.hasMatch()) {
            fail(error, ErrorCode::InvalidArgument, QStringLiteral("pages"),
                 tr("Write page ranges like 1-3, 5, 8-"));
            return std::nullopt;
        }
        PageRange range;
        range.first = match.captured(1).toInt();
        range.last = match.captured(2).isEmpty() ? range.first
                   : match.captured(3).isEmpty() ? -1 : match.captured(3).toInt();
        if (range.first < 1 || (range.last != -1 && range.last < range.first)) {
            fail(error, ErrorCode::InvalidArgument, QStringLiteral("pages"),
                 tr("Page ranges start at page 1 and run forward"));
            return std::nullopt;
        }
        ranges.append(range);
    }
    return ranges;
}

QString formatPageRanges(const QVector<PageRange> &ranges)
{
    QStringList parts;
    for (const PageRange &range : ranges)
        parts.append(range.last == range.first ? QString::number(range.first)
                     : range.last == -1 ? QStringLiteral("%1-").arg(range.first)
                                        : QStringLiteral("%1-%2").arg(range.first).arg(range.last));
    return parts.join(QStringLiteral(", "));
}

QVector<int> selectedPages(const QVector<PageRange> &ranges, int pageCount)
{
    QVector<int> pages;
    if (ranges.isEmpty()) {
        for (int page = 0; page < pageCount; ++page) pages.append(page);
        return pages;
    }
    for (const PageRange &range : ranges) {
        const int last = range.last == -1 ? pageCount : qMin(range.last, pageCount);
        for (int page = range.first; page <= last; ++page) pages.append(page - 1);
    }
    return pages;
}

std::optional<Ticket> ticketFromMap(const QVariantMap &map, const Ticket &base, OperationError *error)
{
    Ticket t = base;
    if (map.contains(QStringLiteral("printerId"))) t.printerId = map.value(QStringLiteral("printerId")).toString();
    if (map.contains(QStringLiteral("outputPath"))) t.outputPath = map.value(QStringLiteral("outputPath")).toString();
    if (map.contains(QStringLiteral("paperId"))) t.paperId = map.value(QStringLiteral("paperId")).toString();
    const bool ok = readText(map, QStringLiteral("orientation"),
                             {QStringLiteral("auto"), QStringLiteral("portrait"), QStringLiteral("landscape")},
                             &t.orientation, error)
        && readText(map, QStringLiteral("scaleMode"),
                    {QStringLiteral("fit"), QStringLiteral("actual"), QStringLiteral("percent")}, &t.scaleMode, error)
        && readText(map, QStringLiteral("duplex"),
                    {QStringLiteral("simplex"), QStringLiteral("long-edge"), QStringLiteral("short-edge")},
                    &t.duplex, error)
        && readText(map, QStringLiteral("colorMode"), {QStringLiteral("color"), QStringLiteral("grayscale")},
                    &t.colorMode, error)
        && readBool(map, QStringLiteral("collate"), &t.collate)
        && readBool(map, QStringLiteral("includeBleed"), &t.includeBleed)
        && readBool(map, QStringLiteral("cropMarks"), &t.cropMarks);
    if (!ok) return std::nullopt;
    if (t.paperId.isEmpty() || t.printerId.isEmpty()) {
        fail(error, ErrorCode::InvalidArgument, t.paperId.isEmpty() ? QStringLiteral("paperId") : QStringLiteral("printerId"),
             tr("Choose a destination and a paper"));
        return std::nullopt;
    }
    if (map.contains(QStringLiteral("scalePercent"))) {
        bool valid = false;
        const double percent = map.value(QStringLiteral("scalePercent")).toDouble(&valid);
        if (!valid || !std::isfinite(percent) || percent < 1 || percent > 1000) {
            fail(error, ErrorCode::InvalidArgument, QStringLiteral("scalePercent"), tr("Scale must be 1–1000 %"));
            return std::nullopt;
        }
        t.scalePercent = percent;
    }
    if (map.contains(QStringLiteral("copies"))) {
        bool valid = false;
        const double copies = map.value(QStringLiteral("copies")).toDouble(&valid);
        if (!valid || copies != std::floor(copies) || copies < 1 || copies > 9999) {
            fail(error, ErrorCode::InvalidArgument, QStringLiteral("copies"), tr("Copies must be a whole number from 1"));
            return std::nullopt;
        }
        t.copies = int(copies);
    }
    if (map.contains(QStringLiteral("pages"))) {
        const auto ranges = parsePageRanges(map.value(QStringLiteral("pages")).toString(), error);
        if (!ranges) return std::nullopt;
        t.ranges = *ranges;
    } else if (map.contains(QStringLiteral("pageRange"))) {
        // PrintPreview's single inclusive range {startPage, endPage}.
        const QVariantMap range = map.value(QStringLiteral("pageRange")).toMap();
        const int first = range.value(QStringLiteral("startPage")).toInt();
        const int last = range.value(QStringLiteral("endPage")).toInt();
        if (first < 1 || last < first) {
            fail(error, ErrorCode::InvalidArgument, QStringLiteral("pages"), tr("Page ranges start at page 1 and run forward"));
            return std::nullopt;
        }
        t.ranges = {{first, last}};
    }
    return t;
}

QVariantMap ticketToMap(const Ticket &t, int pageCount)
{
    const PageRange whole{1, qMax(1, pageCount)};
    const PageRange range = t.ranges.size() == 1 ? t.ranges.first() : whole;
    return {{QStringLiteral("printerId"), t.printerId},
            {QStringLiteral("outputPath"), t.outputPath},
            {QStringLiteral("pages"), formatPageRanges(t.ranges)},
            {QStringLiteral("pageRange"), QVariantMap{{QStringLiteral("startPage"), range.first},
                                                      {QStringLiteral("endPage"), range.last == -1 ? whole.last : range.last}}},
            {QStringLiteral("paperId"), t.paperId},
            {QStringLiteral("orientation"), t.orientation},
            {QStringLiteral("scaleMode"), t.scaleMode},
            {QStringLiteral("scalePercent"), t.scalePercent},
            {QStringLiteral("copies"), t.copies},
            {QStringLiteral("collate"), t.collate},
            {QStringLiteral("duplex"), t.duplex},
            {QStringLiteral("colorMode"), t.colorMode},
            {QStringLiteral("includeBleed"), t.includeBleed},
            {QStringLiteral("cropMarks"), t.cropMarks}};
}

QVector<ValidationIssue> validateTicket(const Ticket &t, const Capabilities &d, int pageCount)
{
    QVector<ValidationIssue> issues;
    const auto add = [&](const QString &field, ErrorCode code, const QString &message) {
        issues.append({field, message, code});
    };
    if (!d.available)
        add(QStringLiteral("printerId"), ErrorCode::Unavailable,
            tr("%1 is not available: %2").arg(d.label, d.status.isEmpty() ? tr("no response") : d.status));
    // At most one issue per field: preflight derives issue IDs from it.
    bool pagesChecked = false;
    for (const PageRange &range : t.ranges) {
        const int last = range.last == -1 ? range.first : range.last;
        if (pageCount < 1 || last > pageCount) {
            add(QStringLiteral("pages"), ErrorCode::InvalidArgument,
                tr("Page %1 is outside the document's pages 1–%2").arg(last).arg(pageCount));
            pagesChecked = true;
            break;
        }
    }
    if (!pagesChecked && selectedPages(t.ranges, pageCount).isEmpty())
        add(QStringLiteral("pages"), ErrorCode::InvalidArgument, tr("No pages are selected"));
    bool paper = false;
    for (const Paper &p : d.papers) paper = paper || p.id == t.paperId;
    if (!paper) add(QStringLiteral("paperId"), ErrorCode::Unsupported, tr("%1 does not offer this paper").arg(d.label));
    const auto check = [&](const QString &field, const QStringList &allowed, const QString &value) {
        if (!allowed.contains(value))
            add(field, ErrorCode::Unsupported, tr("%1 does not support %2").arg(d.label, optionLabel(field, value)));
    };
    check(QStringLiteral("orientation"), d.orientations, t.orientation);
    check(QStringLiteral("scaleMode"), d.scaleModes, t.scaleMode);
    check(QStringLiteral("duplex"), d.duplexModes, t.duplex);
    check(QStringLiteral("colorMode"), d.colorModes, t.colorMode);
    if (t.copies > d.maxCopies)
        add(QStringLiteral("copies"), ErrorCode::Unsupported, tr("%1 prints at most %2 copies").arg(d.label).arg(d.maxCopies));
    if (t.copies > 1 && t.collate && !d.collate)
        add(QStringLiteral("collate"), ErrorCode::Unsupported, tr("%1 cannot collate copies").arg(d.label));
    if ((t.includeBleed || t.cropMarks) && !d.bleed)
        add(t.includeBleed ? QStringLiteral("includeBleed") : QStringLiteral("cropMarks"), ErrorCode::Unsupported,
            tr("%1 cannot print bleed or crop marks").arg(d.label));
    if (d.kind == QLatin1String("file")) {
        const QUrl url(t.outputPath);
        if (t.outputPath.isEmpty())
            add(QStringLiteral("outputPath"), ErrorCode::InvalidArgument, tr("Choose a file to write"));
        else if (!url.scheme().isEmpty() && !url.isLocalFile())
            add(QStringLiteral("outputPath"), ErrorCode::Unsupported, tr("File output needs a local path"));
        else if (const QFileInfo file(url.isLocalFile() ? url.toLocalFile() : t.outputPath); !file.absoluteDir().exists())
            add(QStringLiteral("outputPath"), ErrorCode::InvalidArgument,
                tr("The folder %1 does not exist").arg(QDir::toNativeSeparators(file.absolutePath())));
    }
    return issues;
}

QVariantMap capabilitiesToMap(const Capabilities &d)
{
    QVariantList papers;
    for (const Paper &paper : d.papers)
        papers.append(QVariantMap{{QStringLiteral("id"), paper.id}, {QStringLiteral("label"), paper.label},
                                  {QStringLiteral("widthPt"), paper.sizePt.width()},
                                  {QStringLiteral("heightPt"), paper.sizePt.height()}});
    return {{QStringLiteral("printerId"), d.printerId},
            {QStringLiteral("label"), d.label},
            {QStringLiteral("kind"), d.kind},
            {QStringLiteral("available"), d.available},
            {QStringLiteral("status"), d.status},
            {QStringLiteral("paperOptions"), papers},
            {QStringLiteral("orientationOptions"), options(QStringLiteral("orientation"), d.orientations)},
            {QStringLiteral("scaleOptions"), options(QStringLiteral("scaleMode"), d.scaleModes)},
            {QStringLiteral("duplexOptions"), options(QStringLiteral("duplex"), d.duplexModes)},
            {QStringLiteral("colorOptions"), options(QStringLiteral("colorMode"), d.colorModes)},
            {QStringLiteral("maxCopies"), d.maxCopies},
            {QStringLiteral("collate"), d.collate},
            {QStringLiteral("bleed"), d.bleed},
            {QStringLiteral("printableMarginsPt"), QVariantMap{
                 {QStringLiteral("top"), d.printableMarginsPt.top()}, {QStringLiteral("right"), d.printableMarginsPt.right()},
                 {QStringLiteral("bottom"), d.printableMarginsPt.bottom()}, {QStringLiteral("left"), d.printableMarginsPt.left()}}},
            {QStringLiteral("unavailableOptions"), d.unavailable}};
}

QString optionLabel(const QString &key, const QString &value)
{
    static const QHash<QString, const char *> labels{
        {QStringLiteral("orientation/auto"), QT_TRANSLATE_NOOP("QindaTK::Print", "Match page")},
        {QStringLiteral("orientation/portrait"), QT_TRANSLATE_NOOP("QindaTK::Print", "Portrait")},
        {QStringLiteral("orientation/landscape"), QT_TRANSLATE_NOOP("QindaTK::Print", "Landscape")},
        {QStringLiteral("scaleMode/fit"), QT_TRANSLATE_NOOP("QindaTK::Print", "Fit to paper")},
        {QStringLiteral("scaleMode/actual"), QT_TRANSLATE_NOOP("QindaTK::Print", "Actual size")},
        {QStringLiteral("scaleMode/percent"), QT_TRANSLATE_NOOP("QindaTK::Print", "Custom %")},
        {QStringLiteral("duplex/simplex"), QT_TRANSLATE_NOOP("QindaTK::Print", "Single-sided")},
        {QStringLiteral("duplex/long-edge"), QT_TRANSLATE_NOOP("QindaTK::Print", "Two-sided · long edge")},
        {QStringLiteral("duplex/short-edge"), QT_TRANSLATE_NOOP("QindaTK::Print", "Two-sided · short edge")},
        {QStringLiteral("colorMode/color"), QT_TRANSLATE_NOOP("QindaTK::Print", "Color")},
        {QStringLiteral("colorMode/grayscale"), QT_TRANSLATE_NOOP("QindaTK::Print", "Grayscale")}};
    const char *label = labels.value(key + u'/' + value);
    return label ? tr(label) : value;
}

QVariant supportedValue(const Capabilities &d, const QString &field)
{
    const auto first = [](const QStringList &values) { return values.isEmpty() ? QVariant() : QVariant(values.first()); };
    if (field == QLatin1String("paperId")) return d.papers.isEmpty() ? QVariant() : QVariant(d.papers.first().id);
    if (field == QLatin1String("orientation")) return first(d.orientations);
    if (field == QLatin1String("scaleMode")) return first(d.scaleModes);
    if (field == QLatin1String("duplex")) return first(d.duplexModes);
    if (field == QLatin1String("colorMode")) return first(d.colorModes);
    if (field == QLatin1String("copies")) return d.maxCopies;
    if (field == QLatin1String("collate") || field == QLatin1String("includeBleed")
        || field == QLatin1String("cropMarks"))
        return false;
    if (field == QLatin1String("pages")) return QString();
    return {};
}

} // namespace QindaTK::Print
