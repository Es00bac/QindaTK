// SPDX-License-Identifier: LGPL-3.0-or-later
#include "notebook_document_p.h"
#include "rich_text_graphemes.h"
#include "rich_text_spans.h"

#include <QJsonDocument>
#include <QSet>
#include <QUuid>

namespace QindaTK::NotebookTree {
using Notebook::Block;

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QVector<int> depths(const QVector<Block> &blocks)
{
    QHash<QString, int> byId;
    QVector<int> result(blocks.size(), 0);
    for (int i = 0; i < blocks.size(); ++i) {
        const Block &block = blocks.at(i);
        result[i] = block.parentId.isEmpty() ? 0 : byId.value(block.parentId, -1) + 1;
        byId.insert(block.id, result[i]);
    }
    return result;
}

int subtreeEnd(const QVector<Block> &blocks, int index)
{
    if (index < 0 || index >= blocks.size()) return index;
    const QVector<int> depth = depths(blocks);
    int end = index + 1;
    while (end < blocks.size() && depth.at(end) > depth.at(index)) ++end;
    return end;
}

QVector<int> children(const QVector<Block> &blocks, const QString &parentId)
{
    QVector<int> result;
    for (int i = 0; i < blocks.size(); ++i)
        if (blocks.at(i).parentId == parentId) result.append(i);
    return result;
}

int indexOf(const QVector<Block> &blocks, const QString &id)
{
    for (int i = 0; i < blocks.size(); ++i)
        if (blocks.at(i).id == id) return i;
    return -1;
}

int insertionPoint(const QVector<Block> &blocks, const QString &parentId, int index)
{
    if (!parentId.isEmpty() && indexOf(blocks, parentId) < 0) return -1;
    const QVector<int> kids = children(blocks, parentId);
    if (index >= 0 && index < kids.size()) return kids.at(index);
    return parentId.isEmpty() ? int(blocks.size()) : subtreeEnd(blocks, indexOf(blocks, parentId));
}

void repair(QVector<Block> *blocks, const QVector<Block> &original)
{
    QHash<QString, QString> parentOf;
    for (const Block &block : original) parentOf.insert(block.id, block.parentId);
    for (const Block &block : std::as_const(*blocks))
        if (!parentOf.contains(block.id)) parentOf.insert(block.id, block.parentId);
    QVector<QString> stack;
    for (Block &block : *blocks) {
        QString parent = block.parentId;
        QSet<QString> seen;
        while (!parent.isEmpty() && !stack.contains(parent) && !seen.contains(parent)) {
            seen.insert(parent);
            parent = parentOf.value(parent);
        }
        if (!stack.contains(parent)) parent.clear(); // cycle or unknown chain
        block.parentId = parent;
        if (parent.isEmpty()) stack.clear();
        else while (stack.last() != parent) stack.removeLast();
        stack.append(block.id);
    }
}

QString validate(const QVector<Block> &blocks)
{
    QVector<QString> stack;
    for (const Block &block : blocks) {
        if (block.parentId.isEmpty()) {
            stack.clear();
        } else {
            if (!stack.contains(block.parentId))
                return QStringLiteral("Block %1 does not follow its parent %2").arg(block.id, block.parentId);
            while (stack.last() != block.parentId) stack.removeLast();
        }
        stack.append(block.id);
    }
    return {};
}

} // namespace QindaTK::NotebookTree

namespace QindaTK::NotebookText {
using RichText::Span;

namespace {
const QStringList &formatKeys()
{
    static const QStringList keys{QStringLiteral("bold"), QStringLiteral("italic"),
        QStringLiteral("underline"), QStringLiteral("strike"), QStringLiteral("code"),
        QStringLiteral("href")};
    return keys;
}
int intern(QVector<QJsonObject> *formats, const QJsonObject &format)
{
    const int found = int(formats->indexOf(format));
    if (found >= 0) return found;
    formats->append(format);
    return int(formats->size() - 1);
}
QJsonObject patched(QJsonObject format, const QJsonObject &patch)
{
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (!formatKeys().contains(it.key())) continue;
        const bool clear = it.value().isBool() ? !it.value().toBool()
            : it.value().toString().isEmpty();
        if (clear) format.remove(it.key());
        else format.insert(it.key(), it.key() == QLatin1String("href")
            ? QJsonValue(it.value().toString()) : QJsonValue(true));
    }
    return format;
}
} // namespace

bool isTextType(const QString &type)
{
    return hasRuns(type) || type == QLatin1String("code");
}
bool hasRuns(const QString &type)
{
    return type == QLatin1String("text") || type == QLatin1String("heading")
        || type == QLatin1String("quote") || type == QLatin1String("list")
        || type == QLatin1String("todo");
}
bool isKnownType(const QString &type)
{
    static const QSet<QString> other{QStringLiteral("table"), QStringLiteral("chart"),
        QStringLiteral("image"), QStringLiteral("embed"), QStringLiteral("ink"),
        QStringLiteral("divider")};
    return isTextType(type) || other.contains(type);
}

Runs read(const QJsonObject &data)
{
    Runs runs;
    runs.text = data.value(QStringLiteral("text")).toString();
    const QJsonArray array = data.value(QStringLiteral("runs")).toArray();
    int total = 0;
    for (const QJsonValue &value : array) {
        QJsonObject format = value.toObject();
        const int length = format.take(QStringLiteral("length")).toInt();
        if (length <= 0) { total = -1; break; }
        runs.spans.append({length, intern(&runs.formats, format)});
        total += length;
    }
    if (total != runs.text.size()) {
        runs.spans.clear();
        runs.formats = {QJsonObject{}};
        if (!runs.text.isEmpty()) runs.spans.append({int(runs.text.size()), 0});
    }
    runs.spans = RichText::Spans::normalized(runs.spans);
    return runs;
}

void write(QJsonObject *data, const Runs &runs)
{
    data->insert(QStringLiteral("text"), runs.text);
    QJsonArray array;
    bool formatted = false;
    for (const Span &span : RichText::Spans::normalized(runs.spans)) {
        QJsonObject run = runs.formats.value(span.format);
        formatted = formatted || !run.isEmpty();
        run.insert(QStringLiteral("length"), span.length);
        array.append(run);
    }
    if (formatted) data->insert(QStringLiteral("runs"), array);
    else data->remove(QStringLiteral("runs"));
}

Runs slice(const Runs &runs, int offset, int length)
{
    Runs result;
    result.formats = runs.formats;
    offset = qBound(0, offset, int(runs.text.size()));
    length = qBound(0, length, int(runs.text.size()) - offset);
    result.text = runs.text.mid(offset, length);
    result.spans = RichText::Spans::slice(runs.spans, offset, length);
    return result;
}

Runs concat(const Runs &first, const Runs &second)
{
    Runs result = first;
    for (const Span &span : second.spans)
        result.spans.append({span.length, intern(&result.formats, second.formats.value(span.format))});
    result.text += second.text;
    result.spans = RichText::Spans::normalized(result.spans);
    return result;
}

Runs replace(const Runs &runs, int offset, int removed, const QString &text)
{
    Runs result = runs;
    offset = qBound(0, offset, int(runs.text.size()));
    removed = qBound(0, removed, int(runs.text.size()) - offset);
    const int format = RichText::Spans::formatAt(runs.spans, offset > 0 ? offset - 1 : 0);
    QVector<Span> inserted;
    if (!text.isEmpty()) inserted.append({int(text.size()), format});
    result.spans = RichText::Spans::replaced(runs.spans, offset, removed, inserted);
    result.text.replace(offset, removed, text);
    return result;
}

Runs format(const Runs &runs, int offset, int length, const QJsonObject &patch)
{
    Runs result = runs;
    const int total = int(runs.text.size());
    offset = qBound(0, offset, total);
    length = qBound(0, length, total - offset);
    QVector<Span> spans = RichText::Spans::slice(runs.spans, 0, offset);
    for (const Span &span : RichText::Spans::slice(runs.spans, offset, length))
        spans.append({span.length, intern(&result.formats, patched(runs.formats.value(span.format), patch))});
    spans += RichText::Spans::slice(runs.spans, offset + length, total - offset - length);
    result.spans = RichText::Spans::normalized(spans);
    return result;
}

QString validateRuns(const QJsonObject &data)
{
    if (!data.contains(QStringLiteral("runs"))) return {};
    if (!data.value(QStringLiteral("runs")).isArray()) return QStringLiteral("runs must be an array");
    qint64 total = 0;
    for (const QJsonValue &value : data.value(QStringLiteral("runs")).toArray()) {
        const QJsonObject run = value.toObject();
        const int length = run.value(QStringLiteral("length")).toInt();
        if (length <= 0) return QStringLiteral("Each run needs a positive length");
        for (auto it = run.begin(); it != run.end(); ++it)
            if (it.key() != QLatin1String("length") && !formatKeys().contains(it.key()))
                return QStringLiteral("Unknown run attribute %1").arg(it.key());
        total += length;
    }
    if (total != data.value(QStringLiteral("text")).toString().size())
        return QStringLiteral("Run lengths do not cover the block text");
    return {};
}

int snap(const QString &text, int offset)
{
    offset = qBound(0, offset, int(text.size()));
    const RichText::GraphemeBoundaries boundaries(text);
    return boundaries.isBoundary(offset) ? offset : boundaries.previous(offset);
}

QJsonObject normalized(const QString &type, const QJsonObject &data, QString *problem)
{
    QJsonObject result;
    if (isTextType(type)) result.insert(QStringLiteral("text"), QString());
    if (type == QLatin1String("heading")) result.insert(QStringLiteral("level"), 2);
    if (type == QLatin1String("list")) result.insert(QStringLiteral("ordered"), false);
    if (type == QLatin1String("todo")) result.insert(QStringLiteral("checked"), false);
    if (type == QLatin1String("code")) result.insert(QStringLiteral("language"), QStringLiteral("javascript"));
    if (type == QLatin1String("table")) {
        result.insert(QStringLiteral("columns"), QJsonArray{QStringLiteral("Column 1"), QStringLiteral("Column 2")});
        result.insert(QStringLiteral("rows"), QJsonArray{QJsonArray{QString(), QString()}});
    }
    if (type == QLatin1String("chart")) {
        result.insert(QStringLiteral("sourceBlockId"), QString());
        result.insert(QStringLiteral("kind"), QStringLiteral("line"));
        result.insert(QStringLiteral("xColumn"), 0);
        result.insert(QStringLiteral("yColumn"), 1);
    }
    if (type == QLatin1String("image")) {
        result.insert(QStringLiteral("resourceId"), QString());
        result.insert(QStringLiteral("caption"), QString());
    }
    if (type == QLatin1String("embed")) {
        result.insert(QStringLiteral("targetId"), QString());
        result.insert(QStringLiteral("title"), QString());
    }
    if (type == QLatin1String("ink")) result.insert(QStringLiteral("strokes"), QJsonArray{});
    for (auto it = data.begin(); it != data.end(); ++it) result.insert(it.key(), it.value());

    auto reject = [problem](const QString &message) {
        if (problem) *problem = message;
        return QJsonObject{};
    };
    if (problem) problem->clear();
    if (isTextType(type)) {
        if (result.value(QStringLiteral("text")).toString().size() > Notebook::Limits::MaxTextLength)
            return reject(QStringLiteral("Block text exceeds 1 Mi characters"));
        if (hasRuns(type)) {
            const QString runs = validateRuns(result);
            if (!runs.isEmpty()) return reject(runs);
        } else {
            result.remove(QStringLiteral("runs"));
        }
    }
    if (type == QLatin1String("heading"))
        result.insert(QStringLiteral("level"), qBound(1, result.value(QStringLiteral("level")).toInt(), 6));
    if (type == QLatin1String("table")) {
        QJsonArray columns;
        for (const QJsonValue &column : result.value(QStringLiteral("columns")).toArray())
            columns.append(column.toVariant().toString());
        if (columns.isEmpty() || columns.size() > 256) return reject(QStringLiteral("A table needs 1 to 256 columns"));
        const QJsonArray sourceRows = result.value(QStringLiteral("rows")).toArray();
        const QJsonArray sourceIds = result.value(QStringLiteral("rowIds")).toArray();
        if (sourceRows.size() > 10000) return reject(QStringLiteral("A table holds at most 10,000 rows"));
        QJsonArray rows, rowIds;
        QSet<QString> seen;
        for (int r = 0; r < sourceRows.size(); ++r) {
            const QJsonArray cells = sourceRows.at(r).toArray();
            QJsonArray row;
            for (int c = 0; c < columns.size(); ++c) row.append(cells.at(c).toVariant().toString());
            rows.append(row);
            QString id = sourceIds.at(r).toString();
            if (id.isEmpty() || seen.contains(id)) id = NotebookTree::newId();
            seen.insert(id);
            rowIds.append(id);
        }
        result.insert(QStringLiteral("columns"), columns);
        result.insert(QStringLiteral("rows"), rows);
        result.insert(QStringLiteral("rowIds"), rowIds);
    }
    if (QJsonDocument(result).toJson(QJsonDocument::Compact).size() > 8 * 1024 * 1024)
        return reject(QStringLiteral("Block data exceeds 8 MiB"));
    return result;
}

QString plainText(const Notebook::Block &block)
{
    if (isTextType(block.type)) return block.data.value(QStringLiteral("text")).toString();
    if (block.type == QLatin1String("table")) {
        QStringList lines;
        QStringList header;
        for (const QJsonValue &column : block.data.value(QStringLiteral("columns")).toArray())
            header.append(column.toString());
        lines.append(header.join(u'\t'));
        for (const QJsonValue &row : block.data.value(QStringLiteral("rows")).toArray()) {
            QStringList cells;
            for (const QJsonValue &cell : row.toArray()) cells.append(cell.toString());
            lines.append(cells.join(u'\t'));
        }
        return lines.join(u'\n');
    }
    if (block.type == QLatin1String("image")) return block.data.value(QStringLiteral("caption")).toString();
    if (block.type == QLatin1String("embed") || block.type == QLatin1String("chart"))
        return block.data.value(QStringLiteral("title")).toString();
    return {};
}

} // namespace QindaTK::NotebookText
