// SPDX-License-Identifier: LGPL-3.0-or-later
#include "command_registry.h"
#include "notebook_document_p.h"
#include "resource_resolver.h"
#include "undo_service.h"
#include "script_runtime.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QRegularExpression>
#include <cmath>

namespace QindaTK {
using namespace Notebook;

namespace {
constexpr auto LinkScheme = "notebook:";

QString excerptAround(const QString &text, int offset, int length)
{
    const int from = qMax(0, offset - 40);
    const int to = qMin(int(text.size()), offset + length + 40);
    QString result = text.mid(from, to - from).simplified();
    if (from > 0) result.prepend(QChar(0x2026));
    if (to < text.size()) result.append(QChar(0x2026));
    return result;
}

// Every href a block carries: inline link runs and an embed's target.
QStringList hrefsOf(const Block &block)
{
    QStringList result;
    for (const QJsonValue &run : block.data.value(QStringLiteral("runs")).toArray()) {
        const QString href = run.toObject().value(QStringLiteral("href")).toString();
        if (href.startsWith(QLatin1String(LinkScheme)) && !result.contains(href)) result.append(href);
    }
    const QString target = block.data.value(QStringLiteral("targetId")).toString();
    if (block.type == QLatin1String("embed") && target.startsWith(QLatin1String(LinkScheme)) && !result.contains(target))
        result.append(target);
    return result;
}

bool numeric(const QString &text, double *value)
{
    bool ok = false;
    *value = text.trimmed().toDouble(&ok);
    return ok && std::isfinite(*value);
}
} // namespace

QVariantList NotebookDocument::search(const QString &query, const QVariantMap &options) const
{
    const QString needle = query.trimmed();
    if (needle.isEmpty()) return {};
    QString pattern = QRegularExpression::escape(needle);
    if (options.value(QStringLiteral("wholeWords")).toBool()) pattern = QStringLiteral("\\b%1\\b").arg(pattern);
    const QRegularExpression expression(pattern, options.value(QStringLiteral("caseSensitive")).toBool()
        ? QRegularExpression::UseUnicodePropertiesOption
        : QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    const int limit = qBound(1, options.value(QStringLiteral("limit"), 1000).toInt(), 100000);
    QVariantList hits;
    auto scan = [&](const Page &page, const QString &blockId, const QString &field, const QString &text) {
        for (auto it = expression.globalMatch(text); it.hasNext() && hits.size() < limit;) {
            const auto match = it.next();
            hits.append(QVariantMap{{"pageId", page.id}, {"blockId", blockId}, {"field", field},
                                    {"offset", int(match.capturedStart())}, {"length", int(match.capturedLength())},
                                    {"excerpt", excerptAround(text, int(match.capturedStart()), int(match.capturedLength()))}});
        }
    };
    for (const Page &page : m_state.pages) {
        scan(page, {}, QStringLiteral("title"), page.title);
        for (const QString &tag : page.tags) scan(page, {}, QStringLiteral("tag"), tag);
        for (const Block &block : page.blocks)
            scan(page, block.id, block.type == QLatin1String("table") ? QStringLiteral("table") : QStringLiteral("text"),
                 NotebookText::plainText(block));
        if (hits.size() >= limit) break;
    }
    return hits;
}

QStringList NotebookDocument::searchPageIds(const QString &query, const QVariantMap &options) const
{
    QStringList result;
    for (const QVariant &hit : search(query, options)) {
        const QString pageId = hit.toMap().value(QStringLiteral("pageId")).toString();
        if (!result.contains(pageId)) result.append(pageId);
    }
    return result;
}

QString NotebookDocument::linkTo(const QString &pageId, const QString &blockId) const
{
    return QLatin1String(LinkScheme) + pageId + (blockId.isEmpty() ? QString() : QLatin1Char('#') + blockId);
}

QVariantMap NotebookDocument::resolveLink(const QString &href) const
{
    if (!href.startsWith(QLatin1String(LinkScheme)))
        return {{"valid", false}, {"reason", QStringLiteral("not-notebook")}};
    const QString target = href.mid(int(qstrlen(LinkScheme)));
    const QString pageId = target.section(u'#', 0, 0);
    const QString blockId = target.section(u'#', 1);
    if (pageIndex(pageId) < 0)
        return {{"valid", false}, {"pageId", pageId}, {"blockId", blockId}, {"reason", QStringLiteral("missing-page")}};
    if (!blockId.isEmpty() && pageOfBlock(blockId) != pageId)
        return {{"valid", false}, {"pageId", pageId}, {"blockId", blockId}, {"reason", QStringLiteral("missing-block")}};
    return {{"valid", true}, {"pageId", pageId}, {"blockId", blockId}, {"reason", QString()}};
}

QVariantList NotebookDocument::backlinks(const QString &pageId) const
{
    QVariantList result;
    for (const Page &page : m_state.pages)
        for (const Block &block : page.blocks)
            for (const QString &href : hrefsOf(block))
                if (resolveLink(href).value(QStringLiteral("pageId")).toString() == pageId)
                    result.append(QVariantMap{{"pageId", page.id}, {"blockId", block.id}, {"href", href}});
    return result;
}

QVariantList NotebookDocument::brokenLinks() const
{
    QVariantList result;
    for (const Page &page : m_state.pages)
        for (const Block &block : page.blocks)
            for (const QString &href : hrefsOf(block)) {
                const QVariantMap resolved = resolveLink(href);
                if (!resolved.value(QStringLiteral("valid")).toBool())
                    result.append(QVariantMap{{"pageId", page.id}, {"blockId", block.id}, {"href", href},
                                              {"reason", resolved.value(QStringLiteral("reason"))}});
            }
    return result;
}

QStringList NotebookDocument::resourceIds() const
{
    QStringList result;
    for (const Page &page : m_state.pages)
        for (const Block &block : page.blocks) {
            const QString id = block.data.value(QStringLiteral("resourceId")).toString();
            if (!id.isEmpty() && !result.contains(id)) result.append(id);
        }
    return result;
}

QVariantList NotebookDocument::assetStatus(ResourceResolver *resolver) const
{
    // resources() reads cached entries only: no file access, no resolver error.
    QHash<QString, QVariantMap> known;
    if (resolver)
        for (const QVariant &entry : resolver->resources())
            known.insert(entry.toMap().value(QStringLiteral("resourceId")).toString(), entry.toMap());
    QVariantList result;
    for (const Page &page : m_state.pages)
        for (const Block &block : page.blocks) {
            const QString id = block.data.value(QStringLiteral("resourceId")).toString();
            if (id.isEmpty()) continue;
            const QString availability = known.contains(id)
                ? known.value(id).value(QStringLiteral("availability")).toString() : QStringLiteral("unregistered");
            result.append(QVariantMap{{"pageId", page.id}, {"blockId", block.id}, {"resourceId", id},
                                      {"uri", known.value(id).value(QStringLiteral("uri"))},
                                      {"availability", availability},
                                      {"missing", availability != QLatin1String("ready")}});
        }
    return result;
}

bool NotebookDocument::evaluateBlock(const QString &blockId, ScriptRuntime *runtime)
{
    const Location at = locate(blockId);
    if (!at.valid()) return fail(QStringLiteral("Unknown block %1").arg(blockId));
    const Page &page = m_state.pages.at(at.page);
    const Block &block = page.blocks.at(at.block);
    const QString language = block.data.value(QStringLiteral("language")).toString().toLower();
    if (block.type != QLatin1String("code") || (language != QLatin1String("javascript") && language != QLatin1String("js")))
        return fail(QStringLiteral("Only JavaScript code blocks evaluate"));
    if (!runtime) return fail(QStringLiteral("No script runtime"));
    if (runtime->running() || m_evaluationRuntime) return fail(QStringLiteral("An evaluation is already running"));
    // AGENT-CONTRACT (docs/scripting.md): the script sees a JSON copy of this
    // page as app.document; it never receives the NotebookDocument object.
    QVariantList blocks;
    for (const Block &item : page.blocks) {
        QVariantMap record{{"id", item.id}, {"type", item.type}, {"text", NotebookText::plainText(item)}};
        if (item.type == QLatin1String("table")) {
            record.insert(QStringLiteral("columns"), item.data.value(QStringLiteral("columns")).toVariant());
            record.insert(QStringLiteral("rows"), item.data.value(QStringLiteral("rows")).toVariant());
        }
        blocks.append(record);
    }
    runtime->setDocumentSnapshot({{"notebookId", m_documentId}, {"title", m_state.title}, {"pageId", page.id},
                                  {"pageTitle", page.title}, {"blockId", blockId}, {"blocks", blocks}});
    const QString source = block.data.value(QStringLiteral("text")).toString();
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Sha256).toHex());
    m_evaluationRuntime = runtime;
    m_evaluationConnection = connect(runtime, &ScriptRuntime::finished, this, [this, blockId, hash](bool success) {
        disconnect(m_evaluationConnection);
        ScriptRuntime *finished = m_evaluationRuntime;
        m_evaluationRuntime.clear();
        if (!finished) return;
        const Location where = locate(blockId);
        if (!where.valid()) {
            emit blockEvaluated(blockId, false);
            return;
        }
        State next = m_state;
        QJsonObject &data = next.pages[where.page].blocks[where.block].data;
        data.insert(QStringLiteral("status"), success ? QStringLiteral("completed") : QStringLiteral("failed"));
        data.insert(QStringLiteral("output"), finished->output().left(4096));
        data.insert(QStringLiteral("error"), finished->errorString().left(4096));
        QJsonValue result = QJsonValue::fromVariant(finished->lastResult());
        const QByteArray encoded = QJsonDocument(QJsonArray{result}).toJson(QJsonDocument::Compact);
        if (encoded.size() > 64 * 1024) result = QStringLiteral("[result exceeds 64 KiB]");
        data.insert(QStringLiteral("result"), result);
        data.insert(QStringLiteral("evaluatedHash"), hash);
        commit(next, tr("Evaluate code block"));
        emit blockEvaluated(blockId, success);
    });
    if (!runtime->execute(source)) {
        // execute() reports synchronous failures through finished(false);
        // anything else never started.
        if (m_evaluationRuntime) {
            disconnect(m_evaluationConnection);
            m_evaluationRuntime.clear();
        }
        return fail(runtime->errorString().isEmpty() ? QStringLiteral("Evaluation did not start") : runtime->errorString());
    }
    clearError();
    return true;
}

QVariantMap NotebookDocument::chartSeries(const QString &blockId) const
{
    const Location at = locate(blockId);
    auto invalid = [](const QString &message) { return QVariantMap{{"valid", false}, {"error", message}, {"points", QVariantList{}}}; };
    if (!at.valid()) return invalid(QStringLiteral("Unknown block"));
    const Block &chart = m_state.pages.at(at.page).blocks.at(at.block);
    if (chart.type != QLatin1String("chart")) return invalid(QStringLiteral("Not a chart block"));
    const Location sourceAt = locate(chart.data.value(QStringLiteral("sourceBlockId")).toString());
    if (!sourceAt.valid()) return invalid(tr("The chart's source table is missing"));
    const Block &source = m_state.pages.at(sourceAt.page).blocks.at(sourceAt.block);
    if (source.type != QLatin1String("table")) return invalid(tr("The chart's source is not a table"));
    const QJsonArray columns = source.data.value(QStringLiteral("columns")).toArray();
    const int xColumn = chart.data.value(QStringLiteral("xColumn")).toInt();
    const int yColumn = chart.data.value(QStringLiteral("yColumn")).toInt();
    if (xColumn < 0 || yColumn < 0 || xColumn >= columns.size() || yColumn >= columns.size())
        return invalid(tr("The chart's columns are outside the table"));
    const QJsonArray rows = source.data.value(QStringLiteral("rows")).toArray();
    const QJsonArray rowIds = source.data.value(QStringLiteral("rowIds")).toArray();
    bool categorical = false;
    for (const QJsonValue &row : rows) {
        double value = 0;
        if (!numeric(row.toArray().at(xColumn).toString(), &value)) categorical = true;
    }
    QVariantList points;
    double xMin = 0, xMax = 0, yMin = 0, yMax = 0;
    bool any = false;
    for (int r = 0; r < rows.size(); ++r) {
        const QJsonArray cells = rows.at(r).toArray();
        double x = r, y = 0;
        if (!categorical) numeric(cells.at(xColumn).toString(), &x);
        QVariantMap point{{"pointId", rowIds.at(r).toString(QString::number(r))}, {"x", x}};
        if (categorical) point.insert(QStringLiteral("category"), cells.at(xColumn).toString());
        if (!numeric(cells.at(yColumn).toString(), &y)) {
            point.insert(QStringLiteral("gap"), true);
        } else {
            xMin = any ? qMin(xMin, x) : x;
            xMax = any ? qMax(xMax, x) : x;
            yMin = any ? qMin(yMin, y) : y;
            yMax = any ? qMax(yMax, y) : y;
            any = true;
        }
        point.insert(QStringLiteral("y"), y);
        points.append(point);
    }
    yMin = qMin(0.0, yMin);
    if (yMax <= yMin) yMax = yMin + 1;
    if (xMax <= xMin) xMax = xMin + 1;
    return {{"valid", any}, {"error", any ? QString() : tr("The source table has no numeric values")},
            {"kind", chart.data.value(QStringLiteral("kind")).toString()}, {"seriesId", blockId},
            {"name", columns.at(yColumn).toString()}, {"xName", columns.at(xColumn).toString()},
            {"categorical", categorical}, {"points", points},
            {"xMin", xMin}, {"xMax", xMax}, {"yMin", yMin}, {"yMax", yMax}};
}

} // namespace QindaTK
