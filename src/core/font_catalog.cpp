// SPDX-License-Identifier: LGPL-3.0-or-later
#include "font_catalog.h"
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QRawFont>
#include <QSet>
#include <algorithm>

namespace QindaTK {
namespace {
bool within(const QByteArray &data, qsizetype at, qsizetype count)
{
    return at >= 0 && count >= 0 && at <= data.size() && count <= data.size() - at;
}

quint16 u16(const QByteArray &data, qsizetype at)
{
    const auto *bytes = reinterpret_cast<const uchar *>(data.constData() + at);
    return (quint16(bytes[0]) << 8) | bytes[1];
}

quint32 u32(const QByteArray &data, qsizetype at)
{
    return (quint32(u16(data, at)) << 16) | u16(data, at + 2);
}

QString tagAt(const QByteArray &data, qsizetype at)
{
    if (!within(data, at, 4)) return {};
    const QByteArray tag = data.mid(at, 4);
    for (const char byte : tag)
        if (byte < 0x20 || byte > 0x7e) return {};
    return QString::fromLatin1(tag);
}

double fixed(const QByteArray &data, qsizetype at)
{
    return double(qint32(u32(data, at))) / 65536.0;
}

// AGENT-CONTRACT: fvar supplies the axis bounds consumed by FontFeaturesEditor;
// never infer variable axes from the family or style name.
bool readAxes(const QByteArray &table, QVariantList *axes)
{
    if (table.isEmpty()) return true; // Static face: known empty axis set.
    if (!within(table, 0, 16) || u16(table, 0) != 1) return false;
    const qsizetype offset = u16(table, 4);
    const qsizetype count = u16(table, 8);
    const qsizetype stride = u16(table, 10);
    if (stride < 20 || count > 256 || !within(table, offset, count * stride)) return false;
    QSet<QString> seen;
    for (qsizetype i = 0; i < count; ++i) {
        const qsizetype at = offset + i * stride;
        const QString tag = tagAt(table, at);
        const double minimum = fixed(table, at + 4);
        const double fallback = fixed(table, at + 8);
        const double maximum = fixed(table, at + 12);
        if (tag.isEmpty() || seen.contains(tag) || minimum > fallback || fallback > maximum)
            return false;
        seen.insert(tag);
        axes->append(QVariantMap{{QStringLiteral("tag"), tag},
                                 {QStringLiteral("name"), tag},
                                 {QStringLiteral("minimum"), minimum},
                                 {QStringLiteral("default"), fallback},
                                 {QStringLiteral("maximum"), maximum},
                                 {QStringLiteral("supported"), true}});
    }
    return true;
}

bool addLayoutFeatures(const QByteArray &table, QSet<QString> *tags)
{
    if (table.isEmpty()) return true;
    if (!within(table, 0, 10) || u16(table, 0) != 1) return false;
    const qsizetype offset = u16(table, 6);
    if (!within(table, offset, 2)) return false;
    const qsizetype count = u16(table, offset);
    if (count > 4096 || !within(table, offset + 2, count * 6)) return false;
    for (qsizetype i = 0; i < count; ++i) {
        const qsizetype record = offset + 2 + i * 6;
        const QString tag = tagAt(table, record);
        const qsizetype feature = offset + u16(table, record + 4);
        if (tag.isEmpty() || !within(table, feature, 4)) return false;
        tags->insert(tag);
    }
    return true;
}
} // namespace

FontCatalog::FontCatalog(QObject *parent) : QObject(parent)
{
    refresh();
    if (qGuiApp)
        connect(qGuiApp, &QGuiApplication::fontDatabaseChanged,
                this, &FontCatalog::refresh);
}

void FontCatalog::refresh()
{
    m_fallbackFamily = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    QVariantList found;
    const auto names = QFontDatabase::families();
    found.reserve(names.size());
    for (const auto &family : names) {
        if (QFontDatabase::isPrivateFamily(family)) continue;
        found.append(QVariantMap{{QStringLiteral("family"), family},
                                 {QStringLiteral("installed"), true},
                                 {QStringLiteral("styles"), QFontDatabase::styles(family)},
                                 {QStringLiteral("fallbackFamily"), m_fallbackFamily}});
    }
    m_families = found;
    emit changed();
}

QVariantMap FontCatalog::resolveFamily(const QString &requested) const
{
    const QString name = requested.trimmed();
    for (const auto &entry : m_families) {
        const auto map = entry.toMap();
        if (map.value(QStringLiteral("family")).toString().compare(name, Qt::CaseInsensitive) == 0)
            return map;
    }
    return {{QStringLiteral("family"), name},
            {QStringLiteral("installed"), false},
            {QStringLiteral("styles"), QStringList{}},
            {QStringLiteral("fallbackFamily"), m_fallbackFamily}};
}

QVariantMap FontCatalog::faceMetadata(const QString &family, const QString &style) const
{
    const auto resolved = resolveFamily(family);
    const bool installed = resolved.value(QStringLiteral("installed")).toBool();
    const auto styles = resolved.value(QStringLiteral("styles")).toStringList();
    const bool styleAvailable = style.isEmpty() || styles.contains(style, Qt::CaseInsensitive);
    QVariantList axes;
    QVariantList features;
    bool axisKnown = false;
    bool featuresKnown = false;
    bool coverageKnown = false;
    if (installed && styleAvailable) {
        const QFont font = QFontDatabase::font(resolved.value(QStringLiteral("family")).toString(),
                                               style.isEmpty() ? styles.value(0) : style, 12);
        const QRawFont raw = QRawFont::fromFont(font);
        if (raw.isValid()) {
            coverageKnown = true;
            axisKnown = readAxes(raw.fontTable("fvar"), &axes);
            QSet<QString> tags;
            featuresKnown = addLayoutFeatures(raw.fontTable("GSUB"), &tags)
                && addLayoutFeatures(raw.fontTable("GPOS"), &tags);
            if (featuresKnown) {
                QStringList sorted = tags.values();
                sorted.sort();
                for (const auto &tag : sorted)
                    features.append(QVariantMap{{QStringLiteral("tag"), tag},
                                                {QStringLiteral("name"), tag},
                                                {QStringLiteral("supported"), true}});
            }
        }
    }
    return {{QStringLiteral("family"), resolved.value(QStringLiteral("family"))},
            {QStringLiteral("style"), style},
            {QStringLiteral("available"), installed && styleAvailable},
            {QStringLiteral("installed"), installed},
            {QStringLiteral("styles"), styles},
            {QStringLiteral("fallbackFamily"), m_fallbackFamily},
            {QStringLiteral("axisMetadataAvailable"), axisKnown},
            {QStringLiteral("featureMetadataAvailable"), featuresKnown},
            {QStringLiteral("coverageMetadataAvailable"), coverageKnown},
            {QStringLiteral("axes"), axes},
            {QStringLiteral("features"), features},
            {QStringLiteral("metadataSource"), QStringLiteral("OpenType tables via QRawFont")}};
}

QVariantMap FontCatalog::checkGlyphCoverage(const QString &family, const QString &style,
                                            const QString &sampleText) const
{
    // AGENT-CONTRACT: this checks the selected face's cmap only. The editor
    // must not present it as shaped-text or fallback-font coverage.
    const auto unavailable = [](const QString &reason) {
        return QVariantMap{{QStringLiteral("available"), false},
                           {QStringLiteral("complete"), false},
                           {QStringLiteral("unsupportedCodepoints"), QStringList{}},
                           {QStringLiteral("unsupportedCount"), 0},
                           {QStringLiteral("checkedCount"), 0},
                           {QStringLiteral("reason"), reason}};
    };
    if (sampleText.size() > 4096)
        return unavailable(QStringLiteral("Sample exceeds 4096 UTF-16 code units"));
    const auto resolved = resolveFamily(family);
    const auto styles = resolved.value(QStringLiteral("styles")).toStringList();
    if (!resolved.value(QStringLiteral("installed")).toBool()
        || (!style.isEmpty() && !styles.contains(style, Qt::CaseInsensitive)))
        return unavailable(QStringLiteral("Selected font face is not installed"));
    const QFont font = QFontDatabase::font(resolved.value(QStringLiteral("family")).toString(),
                                           style.isEmpty() ? styles.value(0) : style, 12);
    const QRawFont raw = QRawFont::fromFont(font);
    if (!raw.isValid()) return unavailable(QStringLiteral("Selected font face could not be loaded"));

    QStringList unsupported;
    QSet<uint> seen;
    int checked = 0;
    for (qsizetype i = 0; i < sampleText.size(); ++i) {
        const ushort first = sampleText.at(i).unicode();
        uint codepoint = first;
        if (first >= 0xd800 && first <= 0xdbff) {
            if (i + 1 >= sampleText.size())
                return unavailable(QStringLiteral("Sample contains an unpaired surrogate"));
            const ushort second = sampleText.at(++i).unicode();
            if (second < 0xdc00 || second > 0xdfff)
                return unavailable(QStringLiteral("Sample contains an unpaired surrogate"));
            codepoint = 0x10000u + (uint(first - 0xd800) << 10) + uint(second - 0xdc00);
        } else if (first >= 0xdc00 && first <= 0xdfff) {
            return unavailable(QStringLiteral("Sample contains an unpaired surrogate"));
        }
        ++checked;
        if (seen.contains(codepoint) || raw.supportsCharacter(codepoint)) continue;
        seen.insert(codepoint);
        unsupported.append(QStringLiteral("U+%1")
                               .arg(codepoint, codepoint <= 0xffff ? 4 : 6, 16, QLatin1Char('0'))
                               .toUpper());
    }
    return {{QStringLiteral("available"), true},
            {QStringLiteral("complete"), unsupported.isEmpty()},
            {QStringLiteral("unsupportedCodepoints"), unsupported},
            {QStringLiteral("unsupportedCount"), unsupported.size()},
            {QStringLiteral("checkedCount"), checked},
            {QStringLiteral("reason"), QString{}}};
}

QVariantList FontCatalog::search(const QString &query, int limit) const
{
    QVariantList matches;
    if (limit <= 0) return matches;
    const int bound = std::min(limit, 500);
    for (const auto &entry : m_families) {
        if (!entry.toMap().value(QStringLiteral("family")).toString()
                 .contains(query.trimmed(), Qt::CaseInsensitive)) continue;
        matches.append(entry);
        if (matches.size() >= bound) break;
    }
    return matches;
}

} // namespace QindaTK
