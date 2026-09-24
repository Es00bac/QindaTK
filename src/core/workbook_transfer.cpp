// SPDX-License-Identifier: LGPL-3.0-or-later
#include "workbook_p.h"

#include <QJsonArray>
#include <QJsonObject>

// Range transfer (docs/workbook.md#clipboard): a copied rectangle as the
// payload of a TransferItemKind::Range item, pasteable into this or another
// workbook, plus its tab-separated display text for other applications.
namespace QindaTK {
using namespace Calc;
namespace {
constexpr auto RangeSchema = "qindatk.workbook.range";
constexpr int RangeVersion = 1;
// AGENT-NOTE: TransferPayloadCodec accepts at most 100,000 JSON nodes per
// item; a stored cell costs three (row, column, input), so a clipboard range
// carries at most this many non-blank cells.
constexpr int MaxTransferCells = 30000;
constexpr auto MissingSheet = "\x1f""missing";

QString tsvField(QString text)
{
    // Tabs and line breaks would split the field; other applications get spaces.
    text.replace(QLatin1Char('\t'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    return text;
}
} // namespace

QVariantMap Workbook::rangeTransfer(const QString &sheetId, int firstRow, int firstColumn, int lastRow,
                                    int lastColumn) const
{
    const Sheet *s = d->sheet(sheetId);
    const int r1 = std::min(firstRow, lastRow), r2 = std::max(firstRow, lastRow);
    const int c1 = std::min(firstColumn, lastColumn), c2 = std::max(firstColumn, lastColumn);
    const auto refuse = [](const QString &message) { return QVariantMap{{"ok", false}, {"message", message}}; };
    if (!s || r1 < 0 || c1 < 0 || r2 >= s->rows || c2 >= s->columns) return refuse(QStringLiteral("Copy range is outside the sheet"));
    if (qint64(r2 - r1 + 1) * (c2 - c1 + 1) > MaxTransactionCells)
        return refuse(QStringLiteral("Copy exceeds %1 cells").arg(MaxTransactionCells));
    QJsonArray cells;
    int stored = 0;
    d->visitCells(*s, r1, c1, r2, c2, [&](quint64 position, const Value &) {
        const auto it = s->cells.find(position);
        cells.append(keyRow(position) - r1);
        cells.append(keyColumn(position) - c1);
        cells.append(d->inputText(*s, it->second.content));
        return ++stored <= MaxTransferCells;
    });
    if (stored > MaxTransferCells)
        return refuse(QStringLiteral("A copied range holds at most %1 filled cells").arg(MaxTransferCells));
    const Area area{r1, c1, r2, c2};
    QJsonArray formats, merges;
    for (const FormatLayer &layer : s->features->formats) {
        if (!layer.area.intersects(area)) continue;
        const Area clip{std::max(layer.area.firstRow, r1) - r1, std::max(layer.area.firstColumn, c1) - c1,
                        std::min(layer.area.lastRow, r2) - r1, std::min(layer.area.lastColumn, c2) - c1};
        formats.append(QJsonObject{{QStringLiteral("range"), areaText(clip)},
                                   {QStringLiteral("style"), QJsonObject::fromVariantMap(styleToMap(layer.style))}});
    }
    for (const Area &m : s->features->merges)
        if (area.contains(m)) merges.append(areaText({m.firstRow - r1, m.firstColumn - c1, m.lastRow - r1, m.lastColumn - c1}));
    QJsonObject sheetIds;
    for (const auto &other : d->sheets) sheetIds.insert(other->name, other->id);
    QStringList lines;
    for (int r = r1; r <= r2; ++r) {
        QStringList fields;
        for (int c = c1; c <= c2; ++c) {
            const Cell *cell = d->findCell(*s, r, c);
            fields.append(cell ? tsvField(d->displayAt(*s, r, c, cell->value)) : QString());
        }
        lines.append(fields.join(QLatin1Char('\t')));
    }
    const QJsonObject range{{QStringLiteral("schema"), QLatin1StringView(RangeSchema)},
                            {QStringLiteral("version"), RangeVersion},
                            {QStringLiteral("documentId"), d->documentId},
                            {QStringLiteral("sheetId"), s->id},
                            {QStringLiteral("firstRow"), r1}, {QStringLiteral("firstColumn"), c1},
                            {QStringLiteral("rows"), r2 - r1 + 1}, {QStringLiteral("columns"), c2 - c1 + 1},
                            {QStringLiteral("sheets"), sheetIds}, {QStringLiteral("cells"), cells},
                            {QStringLiteral("formats"), formats}, {QStringLiteral("merges"), merges}};
    return {{"ok", true}, {"range", range.toVariantMap()}, {"text", lines.join(QLatin1Char('\n')) + QLatin1Char('\n')}};
}

// AGENT-CONTRACT (acceptance Transfer fixture): a pasted range is one
// "Paste cells" step. Formulas are re-read in this workbook: a paste into the
// document it came from resolves sheets by their source IDs (renames since
// the copy do not matter); a paste into another document resolves them by
// name and an unknown sheet becomes #REF!. Relative references then move by
// the paste offset, like copyCells. Cells go through the paste policy and
// validation; formats and merged areas of the range replace the target's.
bool Workbook::pasteTransfer(const QString &sheetId, int row, int column, const QVariantMap &range, const QString &policy)
{
    const QJsonObject r = QJsonObject::fromVariantMap(range);
    if (r.value(QStringLiteral("schema")).toString() != QLatin1StringView(RangeSchema)
        || r.value(QStringLiteral("version")).toInt() != RangeVersion)
        return d->fail(QStringLiteral("The clipboard holds no workbook range"));
    const int rows = r.value(QStringLiteral("rows")).toInt(), columns = r.value(QStringLiteral("columns")).toInt();
    const int sourceRow = r.value(QStringLiteral("firstRow")).toInt(), sourceColumn = r.value(QStringLiteral("firstColumn")).toInt();
    Sheet *target = d->sheet(sheetId);
    if (!target) return d->fail(QStringLiteral("Unknown sheet"));
    if (rows < 1 || columns < 1 || qint64(rows) * columns > MaxTransactionCells || row < 0 || column < 0
        || qint64(row) + rows > target->rows || qint64(column) + columns > target->columns)
        return d->fail(QStringLiteral("Paste extends outside the sheet"));
    const bool sameDocument = r.value(QStringLiteral("documentId")).toString() == d->documentId;
    const QJsonObject sourceSheets = r.value(QStringLiteral("sheets")).toObject();
    const SheetIdLookup lookup = [&](const QString &name) {
        QString id;
        if (sameDocument) {
            for (auto it = sourceSheets.begin(); it != sourceSheets.end(); ++it)
                if (it.key().compare(name, Qt::CaseInsensitive) == 0) { id = it.value().toString(); break; }
            if (!d->byId.contains(id)) id.clear();
        } else {
            id = d->idForName(name);
        }
        return id.isEmpty() ? QString::fromLatin1(MissingSheet) : id;
    };
    const SheetNameLookup names = [&](const QString &id) { return d->nameForId(id); };
    // Every position of the target takes the range's input ("" clears it).
    std::vector<WorkbookPrivate::InputEntry> entries;
    entries.reserve(size_t(rows) * size_t(columns));
    QHash<quint64, QString> inputs;
    const QJsonArray cells = r.value(QStringLiteral("cells")).toArray();
    if (cells.size() % 3 != 0) return d->fail(QStringLiteral("The clipboard range is damaged"));
    for (qsizetype i = 0; i < cells.size(); i += 3) {
        const int dr = cells.at(i).toInt(-1), dc = cells.at(i + 1).toInt(-1);
        if (dr < 0 || dc < 0 || dr >= rows || dc >= columns || !cells.at(i + 2).isString())
            return d->fail(QStringLiteral("The clipboard range is damaged"));
        QString input = cells.at(i + 2).toString();
        if (input.startsWith(QLatin1Char('='))) {
            const ParseResult parsed = parseFormula(input, lookup, 8 * MaxFormulaLength);
            if (!parsed.ok()) return d->fail(QStringLiteral("Copied formula at %1: %2")
                                                 .arg(cellAddress(sourceRow + dr, sourceColumn + dc), parsed.error.message));
            FormulaPtr formula = invalidateSheet(parsed.formula, QString::fromLatin1(MissingSheet)).formula;
            formula = offsetFormula(formula, row - sourceRow, column - sourceColumn);
            input = renderFormula(*formula, names);
        }
        inputs.insert(positionKey(dr, dc), input);
    }
    for (int dr = 0; dr < rows; ++dr)
        for (int dc = 0; dc < columns; ++dc)
            entries.push_back({row + dr, column + dc, inputs.value(positionKey(dr, dc))});
    // One step: the cells (policy and validation), then formats and merges.
    const bool own = !d->txn;
    if (own && !d->begin(QStringLiteral("Paste cells"))) return false;
    bool ok = d->applyInputs(QStringLiteral("Paste cells"), sheetId, entries, policy);
    if (ok) {
        auto next = std::make_shared<SheetFeatures>(*target->features);
        const Area to{row, column, row + rows - 1, column + columns - 1};
        std::vector<FormatLayer> kept;
        for (const FormatLayer &layer : next->formats)
            for (const Area &piece : subtractArea(layer.area, to)) kept.push_back({piece, layer.style});
        next->formats = std::move(kept);
        for (const QJsonValue &item : r.value(QStringLiteral("formats")).toArray()) {
            FormatLayer layer;
            QString error;
            const QJsonObject object = item.toObject();
            if (!parseArea(object.value(QStringLiteral("range")).toString(), &layer.area)
                || !styleFromMap(object.value(QStringLiteral("style")).toObject().toVariantMap(), &layer.style, &error)
                || layer.area.lastRow >= rows || layer.area.lastColumn >= columns) continue;
            layer.area = {layer.area.firstRow + row, layer.area.firstColumn + column,
                          layer.area.lastRow + row, layer.area.lastColumn + column};
            next->formats.push_back(layer);
        }
        std::erase_if(next->merges, [&](const Area &m) { return m.intersects(to); });
        for (const QJsonValue &item : r.value(QStringLiteral("merges")).toArray()) {
            Area m;
            if (!parseArea(item.toString(), &m) || m.cellCount() < 2 || m.lastRow >= rows || m.lastColumn >= columns) continue;
            next->merges.push_back({m.firstRow + row, m.firstColumn + column, m.lastRow + row, m.lastColumn + column});
        }
        QString error;
        if (next->formats.size() > size_t(MaxFormatLayers)) ok = d->fail(QStringLiteral("A sheet holds at most %1 format areas").arg(MaxFormatLayers));
        else if (!d->sheetEditable(*target, &error) || !d->recordFeatures(*target, std::move(next), &error)) ok = d->fail(error);
    }
    if (!own) return ok;
    if (!ok) { d->cancel(); return false; }
    return d->commit();
}

} // namespace QindaTK
