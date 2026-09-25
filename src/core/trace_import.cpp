// SPDX-License-Identifier: LGPL-3.0-or-later
#include "trace_index.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
constexpr qint64 MaxFileBytes = 64 * 1024 * 1024;
// |ts| in microseconds; keeps ts × 1000 exact in a double and inside qint64.
constexpr double MaxChromeMicros = 9.0e12;

bool chromeNs(const QVariant &value, qint64 *ns)
{
    bool ok = false;
    const double micros = value.toDouble(&ok);
    if (!ok || !std::isfinite(micros) || std::abs(micros) > MaxChromeMicros) return false;
    *ns = qRound64(micros * 1000.0);
    return true;
}

struct Pending {
    QString kind;
    QString name;
    QString category;
    QString thread;
    qint64 start = 0;
    qint64 end = 0;
    int order = 0;
};
}

// AGENT-NOTE (D-164): Chrome Trace Event JSON is what Chrome, Perfetto and
// Qt Creator's trace tools read and write, so it is the one foreign format
// imported here. Its "ts"/"dur" are microseconds; they become a "chrome"
// clock domain of 1 ns per tick, rounded to the nearest nanosecond. Parents
// come from time containment on the same thread, which is how those tools
// draw nesting; cross-thread parents need this index's own format.
TraceIndex::Analysis TraceIndex::chromeTrace(const QVariantList &events, const CancelFlag &cancel)
{
    Analysis failed;
    if (events.size() > 4 * 50000) {
        failed.error = QStringLiteral("Trace input exceeds 50,000 records");
        return failed;
    }
    QHash<QString, QString> threadNames;
    for (const QVariant &raw : events) {
        const QVariantMap e = raw.toMap();
        if (e.value(QStringLiteral("ph")).toString() == QStringLiteral("M")
            && e.value(QStringLiteral("name")).toString() == QStringLiteral("thread_name")) {
            const QString key = e.value(QStringLiteral("pid")).toString() + QLatin1Char(':')
                                + e.value(QStringLiteral("tid")).toString();
            threadNames.insert(key, e.value(QStringLiteral("args")).toMap()
                                        .value(QStringLiteral("name")).toString());
        }
    }
    QVector<Pending> pending;
    QHash<QString, QVector<int>> open;  // B events waiting for their E, per thread
    int rejected = 0;
    int order = 0;
    for (const QVariant &raw : events) {
        if ((order & 1023) == 0 && cancel->load()) { failed.cancelled = true; return failed; }
        const QVariantMap e = raw.toMap();
        const QString ph = e.value(QStringLiteral("ph")).toString();
        ++order;
        if (ph == QStringLiteral("M")) continue;
        const QString key = e.value(QStringLiteral("pid")).toString() + QLatin1Char(':')
                            + e.value(QStringLiteral("tid")).toString();
        const QString label = threadNames.value(key);
        Pending p;
        p.name = e.value(QStringLiteral("name")).toString();
        p.category = e.value(QStringLiteral("cat")).toString();
        p.thread = label.isEmpty() ? key : QStringLiteral("%1 (%2)").arg(label, key);
        p.order = order;
        qint64 dur = 0;
        if (!chromeNs(e.value(QStringLiteral("ts")), &p.start)) { ++rejected; continue; }
        if (ph == QStringLiteral("X")) {
            if (!chromeNs(e.value(QStringLiteral("dur")), &dur) || dur < 0) { ++rejected; continue; }
            p.kind = QStringLiteral("span");
            p.end = p.start + dur;
            pending.append(p);
        } else if (ph == QStringLiteral("B")) {
            p.kind = QStringLiteral("span");
            p.end = p.start;
            open[key].append(pending.size());
            pending.append(p);
        } else if (ph == QStringLiteral("E")) {
            QVector<int> &stack = open[key];
            if (stack.isEmpty() || p.start < pending[stack.last()].start) { ++rejected; continue; }
            pending[stack.takeLast()].end = p.start;
        } else if (ph == QStringLiteral("i") || ph == QStringLiteral("I")
                   || ph == QStringLiteral("R")) {
            p.kind = QStringLiteral("event");
            p.end = p.start;
            pending.append(p);
        } else {
            ++rejected;  // counters, async and flow phases are not spans or events
        }
    }
    QSet<int> unmatched;
    for (auto it = open.cbegin(); it != open.cend(); ++it)
        for (int i : it.value()) unmatched.insert(i);
    rejected += unmatched.size();

    // Nesting by containment, per thread: earlier start first, longer first.
    QHash<QString, QVector<int>> byThread;
    for (int i = 0; i < pending.size(); ++i)
        if (!unmatched.contains(i)) byThread[pending[i].thread].append(i);
    QVector<QString> parents(pending.size());
    for (auto it = byThread.begin(); it != byThread.end(); ++it) {
        QVector<int> &rows = it.value();
        std::sort(rows.begin(), rows.end(), [&](int a, int b) {
            const Pending &l = pending[a], &r = pending[b];
            if (l.start != r.start) return l.start < r.start;
            if (l.end != r.end) return l.end > r.end;
            if (l.kind != r.kind) return l.kind == QStringLiteral("span");
            return l.order < r.order;
        });
        QVector<int> stack;
        for (int row : std::as_const(rows)) {
            const Pending &p = pending[row];
            while (!stack.isEmpty() && pending[stack.last()].end < p.end) stack.removeLast();
            if (!stack.isEmpty()) parents[row] = QStringLiteral("e%1").arg(pending[stack.last()].order);
            if (p.kind == QStringLiteral("span")) stack.append(row);
        }
    }
    QVariantList records;
    records.reserve(pending.size() - unmatched.size());
    for (int i = 0; i < pending.size(); ++i) {
        if (unmatched.contains(i)) continue;
        const Pending &p = pending[i];
        records.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("e%1").arg(p.order)},
            {QStringLiteral("kind"), p.kind}, {QStringLiteral("name"), p.name},
            {QStringLiteral("category"), p.category}, {QStringLiteral("threadId"), p.thread},
            {QStringLiteral("parentId"), parents[i]},
            {QStringLiteral("clockDomain"), QStringLiteral("chrome")},
            {QStringLiteral("startTicks"), QString::number(p.start)},
            {QStringLiteral("endTicks"), QString::number(p.end)}});
    }
    QHash<QString, ClockDomain> domains;
    domains.insert(QStringLiteral("chrome"), ClockDomain{QStringLiteral("chrome"), 1, 0});
    Analysis result = analyze(records, domains, cancel);
    result.rejected += rejected;
    result.domains = domains;
    result.replaceDomains = true;
    return result;
}

TraceIndex::Analysis TraceIndex::analyzeJson(const QByteArray &bytes, const CancelFlag &cancel)
{
    Analysis failed;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        failed.error = QStringLiteral("Trace JSON is invalid: %1").arg(parseError.errorString());
        return failed;
    }
    if (cancel->load()) { failed.cancelled = true; return failed; }
    if (document.isArray()) return chromeTrace(document.array().toVariantList(), cancel);
    const QJsonObject root = document.object();
    if (root.contains(QStringLiteral("traceEvents")))
        return chromeTrace(root.value(QStringLiteral("traceEvents")).toArray().toVariantList(), cancel);
    // AGENT-CONTRACT (WP-087): this branch reads exportJson()'s shape
    // (trace_index.cpp); a field added there must be read here.
    QHash<QString, ClockDomain> domains;
    if (!parseDomains(root.value(QStringLiteral("clockDomains")).toArray().toVariantList(), &domains)
        || !root.value(QStringLiteral("records")).isArray()) {
        failed.error = QStringLiteral("Trace JSON is neither a trace export nor Chrome trace events");
        return failed;
    }
    Analysis result = analyze(root.value(QStringLiteral("records")).toArray().toVariantList(),
                              domains, cancel);
    // Records the exporter already dropped stay counted.
    result.truncated += root.value(QStringLiteral("truncatedCount")).toInt();
    result.rejected += root.value(QStringLiteral("rejectedCount")).toInt();
    result.domains = domains;
    result.replaceDomains = true;
    return result;
}

TraceIndex::Analysis TraceIndex::analyzeFile(const QString &path, const CancelFlag &cancel)
{
    Analysis failed;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        failed.error = QStringLiteral("Cannot read trace file");
        return failed;
    }
    if (file.size() > MaxFileBytes) {
        failed.error = QStringLiteral("Trace file exceeds 64 MiB");
        return failed;
    }
    const QByteArray bytes = file.read(MaxFileBytes + 1);
    if (bytes.size() > MaxFileBytes) {
        failed.error = QStringLiteral("Trace file exceeds 64 MiB");
        return failed;
    }
    return analyzeJson(bytes, cancel);
}

QString TraceIndex::ingestJsonAsync(const QString &json)
{
    if (json.size() > MaxFileBytes) {
        m_error = QStringLiteral("Trace JSON exceeds 64 MiB"); emit changed(); return {};
    }
    const QByteArray bytes = json.toUtf8();
    return startJob([bytes](const CancelFlag &cancel) { return analyzeJson(bytes, cancel); });
}

QString TraceIndex::ingestFileAsync(const QString &pathOrUrl)
{
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    if (path.isEmpty() || (!url.scheme().isEmpty() && !url.isLocalFile())) {
        m_error = QStringLiteral("Trace file must be a local path"); emit changed(); return {};
    }
    return startJob([path](const CancelFlag &cancel) { return analyzeFile(path, cancel); });
}

} // namespace QindaTK
