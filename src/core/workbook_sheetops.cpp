// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_p.h"

#include <algorithm>
#include <cmath>

namespace QindaTK {
using namespace Calc;
namespace {
constexpr int MaxCopiedLayers = 2048;
bool areaInside(const Sheet &s, int firstRow, int firstColumn, int lastRow, int lastColumn, Area *area)
{
    *area = {std::min(firstRow, lastRow), std::min(firstColumn, lastColumn), std::max(firstRow, lastRow),
             std::max(firstColumn, lastColumn)};
    return area->firstRow >= 0 && area->firstColumn >= 0 && area->lastRow < s.rows && area->lastColumn < s.columns;
}
QVariantMap areaMap(const Area &a)
{
    return {{"firstRow", a.firstRow}, {"firstColumn", a.firstColumn}, {"lastRow", a.lastRow},
            {"lastColumn", a.lastColumn}, {"range", areaText(a)}};
}
// Formats of `features` with `cut` removed (layers split around it).
void removeFormats(SheetFeatures &features, const Area &cut)
{
    std::vector<FormatLayer> kept;
    for (const FormatLayer &layer : features.formats)
        for (const Area &piece : subtractArea(layer.area, cut)) kept.push_back({piece, layer.style});
    features.formats = std::move(kept);
}
} // namespace

RuleLookups WorkbookPrivate::lookups(const Sheet &s) const
{
    RuleLookups l;
    l.sheetIdForName = [this](const QString &name) { return idForName(name); };
    l.sheetNameForId = [this](const QString &id) { return nameForId(id); };
    l.names = [this](const QString &name) {
        const NamedRange *entry = findName(*names, name);
        return entry ? entry->name : QString();
    };
    l.rows = s.rows;
    l.columns = s.columns;
    return l;
}
bool WorkbookPrivate::sheetEditable(const Sheet &s, QString *error) const
{
    if (!s.features->properties.protectedSheet) return true;
    if (error) *error = QStringLiteral("Sheet '%1' is protected").arg(s.name);
    return false;
}
bool WorkbookPrivate::recordFeatures(Sheet &s, FeaturesPtr features, QString *error)
{
    Mutation m;
    m.kind = Mutation::Kind::SetFeatures;
    m.sheetId = s.id;
    m.features = std::move(features);
    return record(std::move(m), error);
}

// Paste and fill copy formats with the cells: the target area takes the
// source block's layers, tiled from (originRow, originColumn), replacing its
// own; copies also carry merges.
bool WorkbookPrivate::copyFormats(const Sheet &source, const Area &block, Sheet &target, const Area &to,
                                  int originRow, int originColumn, bool withMerges, QString *error)
{
    const SheetFeatures &from = *source.features;
    const SheetFeatures &onto = *target.features;
    const auto touches = [](const auto &areas, const Area &a) {
        return std::any_of(areas.begin(), areas.end(), [&](const auto &x) { return x.area.intersects(a); });
    };
    const bool sourceMerges = withMerges && std::any_of(from.merges.begin(), from.merges.end(),
                                                        [&](const Area &m) { return block.contains(m); });
    const bool targetMerges = withMerges && std::any_of(onto.merges.begin(), onto.merges.end(),
                                                        [&](const Area &m) { return m.intersects(to); });
    if (!touches(from.formats, block) && !touches(onto.formats, to) && !sourceMerges && !targetMerges) return true;
    auto next = std::make_shared<SheetFeatures>(onto);
    removeFormats(*next, to);
    const int height = block.lastRow - block.firstRow + 1, width = block.lastColumn - block.firstColumn + 1;
    const auto tileStart = [](int targetFirst, int origin, int size) {
        return origin + int(std::floor(double(targetFirst - origin) / size)) * size;
    };
    int added = 0;
    for (const FormatLayer &layer : from.formats) {
        if (!layer.area.intersects(block)) continue;
        const Area clip{std::max(layer.area.firstRow, block.firstRow), std::max(layer.area.firstColumn, block.firstColumn),
                        std::min(layer.area.lastRow, block.lastRow), std::min(layer.area.lastColumn, block.lastColumn)};
        const bool fullHeight = clip.firstRow == block.firstRow && clip.lastRow == block.lastRow;
        const bool fullWidth = clip.firstColumn == block.firstColumn && clip.lastColumn == block.lastColumn;
        // A layer spanning the block's height covers every row tile at once (likewise width).
        for (int r = fullHeight ? to.firstRow : tileStart(to.firstRow, originRow, height); r <= to.lastRow;
             r += fullHeight ? int(to.lastRow - to.firstRow + 1) : height)
            for (int c = fullWidth ? to.firstColumn : tileStart(to.firstColumn, originColumn, width); c <= to.lastColumn;
                 c += fullWidth ? int(to.lastColumn - to.firstColumn + 1) : width) {
                const Area placed = fullHeight && fullWidth ? to
                    : Area{fullHeight ? to.firstRow : clip.firstRow + (r - block.firstRow),
                           fullWidth ? to.firstColumn : clip.firstColumn + (c - block.firstColumn),
                           fullHeight ? to.lastRow : clip.lastRow + (r - block.firstRow),
                           fullWidth ? to.lastColumn : clip.lastColumn + (c - block.firstColumn)};
                if (!placed.intersects(to)) continue;
                const Area inside{std::max(placed.firstRow, to.firstRow), std::max(placed.firstColumn, to.firstColumn),
                                  std::min(placed.lastRow, to.lastRow), std::min(placed.lastColumn, to.lastColumn)};
                if (++added > MaxCopiedLayers) {
                    *error = QStringLiteral("This would create more than %1 format areas; fill a smaller range")
                                 .arg(MaxCopiedLayers);
                    return false;
                }
                next->formats.push_back({inside, layer.style});
            }
    }
    if (next->formats.size() > size_t(MaxFormatLayers)) {
        *error = QStringLiteral("A sheet holds at most %1 format areas").arg(MaxFormatLayers);
        return false;
    }
    if (withMerges) {
        std::erase_if(next->merges, [&](const Area &m) { return m.intersects(to); });
        for (const Area &m : from.merges)
            if (block.contains(m))
                next->merges.push_back({m.firstRow - block.firstRow + to.firstRow, m.firstColumn - block.firstColumn + to.firstColumn,
                                        m.lastRow - block.firstRow + to.firstRow, m.lastColumn - block.firstColumn + to.firstColumn});
    }
    return recordFeatures(target, std::move(next), error);
}

bool Workbook::applyStyle(const QString &sheetId, int firstRow, int firstColumn, int lastRow, int lastColumn,
                          const QVariantMap &patch)
{
    return d->mutate(QStringLiteral("Format cells"), [&](QString *error, QVariantMap *) {
        Sheet *s = d->sheet(sheetId);
        Area area;
        if (!s) { *error = QStringLiteral("Unknown sheet"); return false; }
        if (!d->sheetEditable(*s, error)) return false;
        if (!areaInside(*s, firstRow, firstColumn, lastRow, lastColumn, &area)) {
            *error = QStringLiteral("Format range is outside the sheet");
            return false;
        }
        CellStyle style;
        if (!styleFromMap(patch, &style, error)) return false;
        if (style.isEmpty()) return true;
        auto next = std::make_shared<SheetFeatures>(*s->features);
        // Compaction: a layer inside the area whose fields the patch all replaces is dead.
        std::erase_if(next->formats, [&](const FormatLayer &layer) {
            return area.contains(layer.area) && (layer.style.mask & ~style.mask) == 0;
        });
        next->formats.push_back({area, style});
        if (next->formats.size() > size_t(MaxFormatLayers)) {
            *error = QStringLiteral("A sheet holds at most %1 format areas; clear formats first").arg(MaxFormatLayers);
            return false;
        }
        return d->recordFeatures(*s, std::move(next), error);
    });
}

bool Workbook::clearFormats(const QString &sheetId, int firstRow, int firstColumn, int lastRow, int lastColumn)
{
    return d->mutate(QStringLiteral("Clear formats"), [&](QString *error, QVariantMap *) {
        Sheet *s = d->sheet(sheetId);
        Area area;
        if (!s) { *error = QStringLiteral("Unknown sheet"); return false; }
        if (!d->sheetEditable(*s, error)) return false;
        if (!areaInside(*s, firstRow, firstColumn, lastRow, lastColumn, &area)) {
            *error = QStringLiteral("Clear range is outside the sheet");
            return false;
        }
        auto next = std::make_shared<SheetFeatures>(*s->features);
        removeFormats(*next, area);
        if (next->formats == s->features->formats) return true;
        return d->recordFeatures(*s, std::move(next), error);
    });
}

bool Workbook::mergeCells(const QString &sheetId, int firstRow, int firstColumn, int lastRow, int lastColumn)
{
    return d->mutate(QStringLiteral("Merge cells"), [&](QString *error, QVariantMap *) {
        Sheet *s = d->sheet(sheetId);
        Area area;
        if (!s) { *error = QStringLiteral("Unknown sheet"); return false; }
        if (!d->sheetEditable(*s, error)) return false;
        if (!areaInside(*s, firstRow, firstColumn, lastRow, lastColumn, &area) || area.cellCount() < 2) {
            *error = QStringLiteral("Merge at least two cells inside the sheet");
            return false;
        }
        auto next = std::make_shared<SheetFeatures>(*s->features);
        std::erase_if(next->merges, [&](const Area &m) { return m.intersects(area); });
        if (next->merges.size() >= size_t(MaxMergedAreas)) {
            *error = QStringLiteral("A sheet holds at most %1 merged areas").arg(MaxMergedAreas);
            return false;
        }
        next->merges.push_back(area);
        // Only the top-left input survives, as in other spreadsheets.
        Mutation clear;
        clear.sheetId = sheetId;
        auto it = s->cells.lower_bound(positionKey(area.firstRow, area.firstColumn));
        const auto end = s->cells.upper_bound(positionKey(area.lastRow, area.lastColumn));
        for (; it != end; ++it) {
            const int r = keyRow(it->first), c = keyColumn(it->first);
            if (area.contains(r, c) && !(r == area.firstRow && c == area.firstColumn))
                clear.cells.push_back({r, c, CellContent{}});
        }
        if (!clear.cells.empty() && !d->record(std::move(clear), error)) return false;
        return d->recordFeatures(*s, std::move(next), error);
    });
}

bool Workbook::unmergeCells(const QString &sheetId, int firstRow, int firstColumn, int lastRow, int lastColumn)
{
    return d->mutate(QStringLiteral("Unmerge cells"), [&](QString *error, QVariantMap *) {
        Sheet *s = d->sheet(sheetId);
        Area area;
        if (!s) { *error = QStringLiteral("Unknown sheet"); return false; }
        if (!d->sheetEditable(*s, error)) return false;
        if (!areaInside(*s, firstRow, firstColumn, lastRow, lastColumn, &area)) {
            *error = QStringLiteral("Unmerge range is outside the sheet");
            return false;
        }
        auto next = std::make_shared<SheetFeatures>(*s->features);
        if (std::erase_if(next->merges, [&](const Area &m) { return m.intersects(area); }) == 0) return true;
        return d->recordFeatures(*s, std::move(next), error);
    });
}

QVariantList Workbook::mergedAreas(const QString &sheetId, int firstRow, int firstColumn, int lastRow, int lastColumn) const
{
    const Sheet *s = d->sheet(sheetId);
    if (!s) return {};
    const Area window{std::min(firstRow, lastRow), std::min(firstColumn, lastColumn), std::max(firstRow, lastRow),
                      std::max(firstColumn, lastColumn)};
    QVariantList result;
    for (const Area &m : s->features->merges)
        if (m.intersects(window)) result.append(areaMap(m));
    return result;
}
QVariantMap Workbook::mergeAt(const QString &sheetId, int row, int column) const
{
    const Sheet *s = d->sheet(sheetId);
    if (!s) return {};
    for (const Area &m : s->features->merges)
        if (m.contains(row, column)) return areaMap(m);
    return {};
}

QVariantMap Workbook::sheetProperties(const QString &sheetId) const
{
    const Sheet *s = d->sheet(sheetId);
    if (!s) return {};
    const SheetProperties &p = s->features->properties;
    return {{"tabColor", p.tabColor}, {"hidden", p.hidden}, {"protected", p.protectedSheet},
            {"frozenRows", p.frozenRows}, {"frozenColumns", p.frozenColumns}};
}

bool Workbook::setSheetProperties(const QString &sheetId, const QVariantMap &changes)
{
    return d->mutate(QStringLiteral("Sheet properties"), [&](QString *error, QVariantMap *) {
        Sheet *s = d->sheet(sheetId);
        if (!s) { *error = QStringLiteral("Unknown sheet"); return false; }
        auto next = std::make_shared<SheetFeatures>(*s->features);
        SheetProperties &p = next->properties;
        for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
            const QString &key = it.key();
            const QVariant &value = it.value();
            if (key == u"tabColor") {
                const QString color = value.toString().isEmpty() ? QString() : normalizedColor(value.toString());
                if (!value.toString().isEmpty() && color.isEmpty()) { *error = QStringLiteral("tabColor must be #rrggbb"); return false; }
                p.tabColor = color;
            } else if (key == u"hidden" || key == u"protected") {
                if (value.metaType().id() != QMetaType::Bool) { *error = QStringLiteral("'%1' must be true or false").arg(key); return false; }
                (key == u"hidden" ? p.hidden : p.protectedSheet) = value.toBool();
            } else if (key == u"frozenRows" || key == u"frozenColumns") {
                bool ok = false;
                const int count = value.toInt(&ok);
                const int limit = key == u"frozenRows" ? s->rows : s->columns;
                if (!ok || count < 0 || count >= limit) { *error = QStringLiteral("'%1' must be 0 to %2").arg(key).arg(limit - 1); return false; }
                (key == u"frozenRows" ? p.frozenRows : p.frozenColumns) = count;
            } else {
                *error = QStringLiteral("Unknown sheet property '%1'").arg(key);
                return false;
            }
        }
        if (p.hidden && std::none_of(d->sheets.begin(), d->sheets.end(), [&](const auto &other) {
                return other.get() != s && !other->features->properties.hidden; })) {
            *error = QStringLiteral("A workbook keeps at least one visible sheet");
            return false;
        }
        if (p == s->features->properties) return true;
        return d->recordFeatures(*s, std::move(next), error);
    });
}

namespace {
bool setSize(WorkbookPrivate *d, const QString &label, const QString &sheetId, int index, double size, bool rows)
{
    return d->mutate(label, [&](QString *error, QVariantMap *) {
        Sheet *s = d->sheet(sheetId);
        if (!s) { *error = QStringLiteral("Unknown sheet"); return false; }
        if (!d->sheetEditable(*s, error)) return false;
        if (index < 0 || index >= (rows ? s->rows : s->columns) || !std::isfinite(size) || size > 100000) {
            *error = QStringLiteral("Invalid size or index");
            return false;
        }
        auto next = std::make_shared<SheetFeatures>(*s->features);
        auto &sizes = rows ? next->properties.rowHeights : next->properties.columnWidths;
        if (size <= 0) sizes.erase(index);
        else sizes[index] = std::max(1.0, size);
        if (next->properties == s->features->properties) return true;
        return d->recordFeatures(*s, std::move(next), error);
    });
}
QVariantMap sizeMap(const std::map<qint32, double> &sizes)
{
    QVariantMap map;
    for (const auto &[index, size] : sizes) map.insert(QString::number(index), size);
    return map;
}
} // namespace
bool Workbook::setColumnWidth(const QString &sheetId, int column, double width)
{ return setSize(d.get(), QStringLiteral("Resize column"), sheetId, column, width, false); }
bool Workbook::setRowHeight(const QString &sheetId, int row, double height)
{ return setSize(d.get(), QStringLiteral("Resize row"), sheetId, row, height, true); }
QVariantMap Workbook::columnWidths(const QString &sheetId) const
{ const Sheet *s = d->sheet(sheetId); return s ? sizeMap(s->features->properties.columnWidths) : QVariantMap(); }
QVariantMap Workbook::rowHeights(const QString &sheetId) const
{ const Sheet *s = d->sheet(sheetId); return s ? sizeMap(s->features->properties.rowHeights) : QVariantMap(); }

QVariantList Workbook::names() const
{
    QVariantList result;
    for (const NamedRange &entry : *d->names) {
        QVariantMap map = areaMap(entry.area);
        map.insert({{"name", entry.name}, {"sheetId", entry.sheetId}, {"valid", !entry.sheetId.isEmpty()},
                    {"reference", entry.sheetId.isEmpty() ? QStringLiteral("#REF!")
                                  : quoteSheetName(d->nameForId(entry.sheetId)) + QLatin1Char('!') + areaText(entry.area)}});
        result.append(map);
    }
    return result;
}
QString Workbook::nameProblem(const QString &name) const { return Calc::nameProblem(name); }

bool Workbook::defineName(const QString &name, const QString &sheetId, int firstRow, int firstColumn, int lastRow,
                          int lastColumn)
{
    return d->mutate(QStringLiteral("Define name %1").arg(name), [&](QString *error, QVariantMap *) {
        const Sheet *s = d->sheet(sheetId);
        Area area;
        if (!s) { *error = QStringLiteral("Unknown sheet"); return false; }
        if (const QString problem = Calc::nameProblem(name); !problem.isEmpty()) { *error = problem; return false; }
        if (!areaInside(*s, firstRow, firstColumn, lastRow, lastColumn, &area)) {
            *error = QStringLiteral("The named range is outside the sheet");
            return false;
        }
        auto next = std::make_shared<NameTable>(*d->names);
        const QString key = name.toCaseFolded();
        const auto it = std::lower_bound(next->begin(), next->end(), key,
                                         [](const NamedRange &entry, const QString &k) { return entry.key < k; });
        if (it != next->end() && it->key == key) *it = {name, key, sheetId, area};
        else if (next->size() >= size_t(MaxNames)) { *error = QStringLiteral("A workbook holds at most %1 names").arg(MaxNames); return false; }
        else next->insert(it, {name, key, sheetId, area});
        Mutation m;
        m.kind = Mutation::Kind::SetNames;
        m.names = std::move(next);
        return d->record(std::move(m), error);
    });
}

bool Workbook::removeName(const QString &name)
{
    return d->mutate(QStringLiteral("Remove name %1").arg(name), [&](QString *error, QVariantMap *) {
        auto next = std::make_shared<NameTable>(*d->names);
        const QString key = name.toCaseFolded();
        if (std::erase_if(*next, [&](const NamedRange &entry) { return entry.key == key; }) == 0) {
            *error = QStringLiteral("No name '%1'").arg(name);
            return false;
        }
        Mutation m;
        m.kind = Mutation::Kind::SetNames;
        m.names = std::move(next);
        return d->record(std::move(m), error);
    });
}

} // namespace QindaTK
