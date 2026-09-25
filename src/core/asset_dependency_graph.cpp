// SPDX-License-Identifier: LGPL-3.0-or-later
// Removal, pruning, rebuild validity and the render-queue adapter.
#include "asset_dependency_index.h"
#include "document_resource_links.h"
#include <QDir>
#include <QFileInfo>
#include <QQueue>
#include <QUrl>
#include <algorithm>
#include <utility>

namespace QindaTK {

// AGENT-NOTE: only entries a scan created (`discovered`) are pruned on their
// own, and only once they are missing, have no inputs and nothing uses them:
// a deleted file or the placeholder of a removed reference. Host-registered
// assets stay until the host calls removeAsset or pruneMissing. Without this
// a long session fills MaxAssets with stale entries.
void AssetDependencyIndex::pruneOrphans(const QSet<QString> &candidates)
{
    if (candidates.isEmpty()) return;
    if (m_quiet > 0) { m_pruneCandidates.unite(candidates); return; }
    QSet<QString> used;
    for (auto it = m_assets.cbegin(); it != m_assets.cend(); ++it)
        for (auto edge = it->dependencies.cbegin(); edge != it->dependencies.cend(); ++edge)
            used.insert(edge.key());
    for (const QString &id : candidates) {
        const auto found = m_assets.constFind(id);
        if (found == m_assets.cend() || !found->discovered || found->available
            || !found->dependencies.isEmpty() || used.contains(id))
            continue;
        m_assets.remove(id);
    }
}

bool AssetDependencyIndex::removeAsset(const QString &id)
{
    const auto found = m_assets.find(id);
    if (found == m_assets.end()) return fail(QStringLiteral("Unknown asset"));
    QSet<QString> dropped;
    for (auto it = found->dependencies.cbegin(); it != found->dependencies.cend(); ++it)
        dropped.insert(it.key());
    m_edgeCount -= found->dependencies.size();
    found->dependencies.clear();
    ++found->scanToken;  // an in-flight scan of it must not bring edges back
    bool used = false;
    for (auto it = m_assets.cbegin(); it != m_assets.cend() && !used; ++it)
        used = it->dependencies.contains(id);
    if (used) {
        found->available = false;
    } else {
        m_assets.erase(found);
    }
    m_error.clear();
    pruneOrphans(dropped);
    notify();
    return true;
}

int AssetDependencyIndex::pruneMissing()
{
    int removed = 0;
    for (bool again = true; again;) {
        again = false;
        QSet<QString> used;
        for (auto it = m_assets.cbegin(); it != m_assets.cend(); ++it)
            for (auto edge = it->dependencies.cbegin(); edge != it->dependencies.cend(); ++edge)
                used.insert(edge.key());
        for (auto it = m_assets.begin(); it != m_assets.end();) {
            if (it->available || used.contains(it.key())) { ++it; continue; }
            m_edgeCount -= it->dependencies.size();
            again = again || !it->dependencies.isEmpty();  // its inputs may now be unused
            it = m_assets.erase(it);
            ++removed;
        }
    }
    if (removed > 0) { m_error.clear(); notify(); }
    return removed;
}

QVariantList AssetDependencyIndex::rebuildBlockers(const QString &id) const
{
    QVariantList result;
    const auto found = m_assets.constFind(id);
    if (found == m_assets.cend()) return result;
    QSet<QString> closure{id};
    QQueue<QString> queue;
    queue.enqueue(id);
    while (!queue.isEmpty()) {
        const QString current = queue.dequeue();
        for (const QString &target : m_assets.value(current).dependencies.keys()) {
            if (closure.contains(target)) continue;
            closure.insert(target);
            queue.enqueue(target);
        }
    }
    // A missing asset with inputs of its own is a generated output the
    // rebuild recreates; only a missing leaf (source file, media) blocks.
    auto missingLeaf = [this](const QString &target) {
        const auto asset = m_assets.constFind(target);
        return asset == m_assets.cend()
               || (!asset->available && asset->dependencies.isEmpty());
    };
    if (missingLeaf(id))
        result.append(QVariantMap{{QStringLiteral("type"), QStringLiteral("missing")},
                                  {QStringLiteral("sourceId"), id},
                                  {QStringLiteral("targetId"), id},
                                  {QStringLiteral("message"),
                                   QStringLiteral("%1 is missing").arg(id)}});
    for (const QVariant &raw : diagnostics()) {
        const QVariantMap row = raw.toMap();
        if (row.value(QStringLiteral("type")).toString() == QStringLiteral("missing")
            && !missingLeaf(row.value(QStringLiteral("targetId")).toString()))
            continue;
        bool inClosure = closure.contains(row.value(QStringLiteral("sourceId")).toString());
        for (const QVariant &member : row.value(QStringLiteral("members")).toList())
            inClosure = inClosure || closure.contains(member.toString());
        if (inClosure) result.append(row);
    }
    return result;
}

// AGENT-NOTE (EXT-10): "schedules only valid rebuilds". An output is blocked
// when anything it transitively needs is a missing leaf or on a cycle; one reverse
// walk from every such asset finds them all, so this is O(assets + edges).
QVariantMap AssetDependencyIndex::rebuildPlan(const QString &changedId) const
{
    const QStringList affected = affectedBy(changedId);
    QHash<QString, QStringList> reverse;
    for (auto it = m_assets.cbegin(); it != m_assets.cend(); ++it)
        for (auto edge = it->dependencies.cbegin(); edge != it->dependencies.cend(); ++edge)
            reverse[edge.key()].append(it.key());
    QHash<QString, QString> reason;  // blocked asset -> why
    QQueue<QString> queue;
    for (auto it = m_assets.cbegin(); it != m_assets.cend(); ++it) {
        if (it->available || !it->dependencies.isEmpty()) continue;  // see rebuildBlockers
        reason.insert(it.key(), QStringLiteral("%1 is missing").arg(it.key()));
        queue.enqueue(it.key());
    }
    for (const QVariant &raw : diagnostics()) {
        const QVariantMap row = raw.toMap();
        if (row.value(QStringLiteral("type")).toString() != QStringLiteral("cycle")) continue;
        for (const QVariant &member : row.value(QStringLiteral("members")).toList()) {
            if (reason.contains(member.toString())) continue;
            reason.insert(member.toString(), row.value(QStringLiteral("message")).toString());
            queue.enqueue(member.toString());
        }
    }
    while (!queue.isEmpty()) {
        const QString current = queue.dequeue();
        for (const QString &parent : reverse.value(current)) {
            if (reason.contains(parent)) continue;
            reason.insert(parent, reason.value(current));
            queue.enqueue(parent);
        }
    }
    // Valid outputs in dependency order (Kahn over the affected subgraph).
    QSet<QString> valid;
    QVariantList blocked;
    for (const QString &id : affected) {
        if (reason.contains(id))
            blocked.append(QVariantMap{{QStringLiteral("id"), id},
                                       {QStringLiteral("reason"), reason.value(id)}});
        else valid.insert(id);
    }
    QHash<QString, int> pending;
    for (const QString &id : std::as_const(valid)) {
        int inputs = 0;
        for (const QString &target : m_assets.value(id).dependencies.keys())
            if (valid.contains(target)) ++inputs;
        pending.insert(id, inputs);
    }
    QStringList order;
    QStringList ready;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        if (it.value() == 0) ready.append(it.key());
    while (!ready.isEmpty()) {
        ready.sort();
        const QString next = ready.takeFirst();
        order.append(next);
        for (const QString &parent : reverse.value(next)) {
            if (!pending.contains(parent)) continue;
            if (--pending[parent] == 0) ready.append(parent);
        }
    }
    return {{QStringLiteral("changedId"), changedId},
            {QStringLiteral("valid"), order},
            {QStringLiteral("blocked"), blocked}};
}

// AGENT-CONTRACT (WP-087 / WP-078): reads RenderQueueModel::jobs() rows
// (jobId, label, state, sourceSnapshot, outputPath) and, when present,
// `dependencies` (job IDs, the model's DependenciesRole). Renaming those
// keys in render_queue_model.cpp needs a matching change here.
int AssetDependencyIndex::indexRenderJobs(const QVariantList &jobs)
{
    if (jobs.size() > 1024) { fail(QStringLiteral("Too many render jobs")); return -1; }
    QHash<QString, QString> outputOf;  // job ID -> output asset ID
    for (const QVariant &raw : jobs) {
        const QVariantMap job = raw.toMap();
        const QString path = job.value(QStringLiteral("outputPath")).toString();
        if (path.isEmpty()) continue;
        const QString relative = m_projectRoot.isEmpty() ? QString()
            : QDir(m_projectRoot).relativeFilePath(QDir::cleanPath(path));
        const bool inside = !relative.isEmpty() && !relative.startsWith(QStringLiteral(".."))
                            && !QDir::isAbsolutePath(relative);
        outputOf.insert(job.value(QStringLiteral("jobId")).toString(),
                        inside ? relative : QDir::cleanPath(path));
    }
    int indexed = 0;
    ++m_quiet;
    for (const QVariant &raw : jobs) {
        const QVariantMap job = raw.toMap();
        const QString jobId = job.value(QStringLiteral("jobId")).toString();
        const QString output = outputOf.value(jobId);
        if (output.isEmpty()) continue;
        const QString path = job.value(QStringLiteral("outputPath")).toString();
        const QString label = job.value(QStringLiteral("label")).toString();
        const bool done = job.value(QStringLiteral("state")).toString() == QStringLiteral("succeeded")
                          && QFileInfo::exists(path);
        if (!upsert(output, label.isEmpty() ? QFileInfo(path).fileName() : label, path, done,
                    QStringLiteral("output"), true))
            break;
        QVariantList edges;
        QString source = job.value(QStringLiteral("sourceId")).toString();
        if (source.isEmpty()) source = job.value(QStringLiteral("sourceSnapshot")).toString();
        if (!source.isEmpty()) {
            const QString file = absolutePathIn(m_projectRoot, source);
            if (!m_assets.contains(source) && !file.isEmpty()
                && !upsert(source, QFileInfo(file).fileName(), file, true,
                           QStringLiteral("file"), true))
                break;
            edges.append(QVariantMap{{QStringLiteral("targetId"), source},
                                     {QStringLiteral("reference"), source}});
        }
        for (const QVariant &dependency : job.value(QStringLiteral("dependencies")).toList()) {
            const QString upstream = outputOf.value(dependency.toString());
            if (!upstream.isEmpty())
                edges.append(QVariantMap{{QStringLiteral("targetId"), upstream},
                                         {QStringLiteral("reference"), dependency.toString()}});
        }
        if (!commitScanEdges(output, beginScan(output), edges)) break;
        ++indexed;
    }
    --m_quiet;
    pruneOrphans(std::exchange(m_pruneCandidates, {}));
    notify();
    return indexed == outputOf.size() ? indexed : -1;
}

// AGENT-CONTRACT (WP-087 / WP-015): reads DocumentResourceLinks::manifest()
// rows (`resourceId`) and ResourceResolver::lookup() maps (`uri`,
// `availability`: missing | ready | modified) through links->resource().
// Renaming those keys needs a matching change here. Any PropertyDocument
// format that links resources this way (office documents, .sloom projects)
// gets its document -> media edges without a format-specific extractor.
int AssetDependencyIndex::indexDocumentResources(const QString &documentId,
                                                 DocumentResourceLinks *links)
{
    if (!links) { fail(QStringLiteral("Document resource links are unavailable")); return -1; }
    const QVariantList manifest = links->manifest();
    if (manifest.size() > 128) {  // MaxEdgesPerAsset in asset_dependency_index.cpp
        fail(QStringLiteral("Dependency edge limit exceeded"));
        return -1;
    }
    ++m_quiet;
    bool ok = m_assets.contains(documentId)
              || upsert(documentId, documentId, QString(), true, QStringLiteral("document"), false);
    QVariantList edges;
    for (int i = 0; ok && i < manifest.size(); ++i) {
        const QString id = manifest.at(i).toMap().value(QStringLiteral("resourceId")).toString();
        const QVariantMap entry = links->resource(id);
        const QString uri = entry.value(QStringLiteral("uri")).toString();
        const QString availability = entry.value(QStringLiteral("availability")).toString();
        const QString name = QFileInfo(QUrl(uri).isLocalFile() ? QUrl(uri).toLocalFile() : uri)
                                 .fileName();
        // A resource the resolver does not know yet counts as missing.
        ok = upsert(id, name.isEmpty() ? id : name, uri,
                    !availability.isEmpty() && availability != QStringLiteral("missing"),
                    QStringLiteral("resource"), true);
        edges.append(QVariantMap{{QStringLiteral("targetId"), id},
                                 {QStringLiteral("kind"), QStringLiteral("reference")},
                                 {QStringLiteral("reference"), uri.isEmpty() ? id : uri}});
    }
    if (ok) ok = commitScanEdges(documentId, beginScan(documentId), edges);
    --m_quiet;
    pruneOrphans(std::exchange(m_pruneCandidates, {}));
    notify();
    return ok ? edges.size() : -1;
}

} // namespace QindaTK
