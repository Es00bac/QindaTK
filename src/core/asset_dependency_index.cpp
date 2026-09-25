// SPDX-License-Identifier: LGPL-3.0-or-later
#include "asset_dependency_index.h"
#include <QDir>
#include <QFileInfo>
#include <QQueue>
#include <QUrl>
#include <algorithm>
#include <functional>
#include <limits>

namespace QindaTK {
namespace {
constexpr int MaxAssets = 4096;
constexpr int MaxEdges = 16384;
constexpr int MaxEdgesPerAsset = 128;
}

bool AssetDependencyIndex::validId(const QString &id)
{
    return !id.trimmed().isEmpty() && id.size() <= 512 && !id.contains(QChar::Null);
}

AssetDependencyIndex::~AssetDependencyIndex()
{
    if (m_scanCancel) m_scanCancel->store(true);
}

void AssetDependencyIndex::notify()
{
    if (m_quiet == 0) emit changed();
}

bool AssetDependencyIndex::fail(const QString &message)
{
    m_error = message;
    notify();
    return false;
}

void AssetDependencyIndex::setProjectRoot(const QString &path)
{
    if (!m_assets.isEmpty()) { fail(QStringLiteral("Set the project root before adding assets")); return; }
    const QUrl url(path);
    const QString root = QFileInfo(url.isLocalFile() ? url.toLocalFile() : path)
                             .canonicalFilePath();
    if (root.isEmpty() || !QFileInfo(root).isDir()) {
        fail(QStringLiteral("Project root must be an existing directory")); return;
    }
    // AGENT-GUARD: a scan started under the old root must never commit its
    // relative IDs against the new one; cancelling bumps the generation, so
    // its result is discarded (applyProjectScan also checks the root).
    if (root != m_projectRoot) cancelProjectScan();
    m_projectRoot = root;
    m_error.clear();
    notify();
}

QVariantMap AssetDependencyIndex::assetMap(const Asset &entry) const
{
    return {{QStringLiteral("id"), entry.id},
            {QStringLiteral("label"), entry.label},
            {QStringLiteral("uri"), entry.uri},
            {QStringLiteral("kind"), entry.kind},
            {QStringLiteral("available"), entry.available},
            {QStringLiteral("dependencyCount"), entry.dependencies.size()},
            {QStringLiteral("secondary"), entry.kind + (entry.available
                ? QStringLiteral(" · %1 inputs").arg(entry.dependencies.size())
                : QStringLiteral(" · Missing"))}};
}

QVariantMap AssetDependencyIndex::asset(const QString &id) const
{
    const auto found = m_assets.constFind(id);
    return found == m_assets.cend() ? QVariantMap{} : assetMap(found.value());
}

QVariantList AssetDependencyIndex::assets() const
{
    QStringList ids = m_assets.keys();
    ids.sort();
    QVariantList result;
    result.reserve(ids.size());
    for (const QString &id : ids) result.append(assetMap(m_assets.value(id)));
    return result;
}

bool AssetDependencyIndex::upsertAsset(const QString &id, const QString &label,
                                      const QString &uri, bool available,
                                      const QString &kind)
{
    return upsert(id, label, uri, available, kind, false);
}

bool AssetDependencyIndex::upsert(const QString &id, const QString &label,
                                  const QString &uri, bool available,
                                  const QString &kind, bool discovered)
{
    if (!validId(id) || label.trimmed().isEmpty() || label.size() > 512
        || uri.size() > 2048 || kind.trimmed().isEmpty() || kind.size() > 64)
        return fail(QStringLiteral("Invalid asset ID or metadata"));
    if (!m_assets.contains(id) && m_assets.size() >= MaxAssets)
        return fail(QStringLiteral("Asset index is full"));
    const bool created = !m_assets.contains(id);
    Asset &entry = m_assets[id];
    // The host registering an asset makes it host-owned for good.
    entry.discovered = created ? discovered : entry.discovered && discovered;
    const bool different = entry.id != id || entry.label != label || entry.uri != uri
                           || entry.available != available || entry.kind != kind;
    entry.id = id;
    entry.label = label;
    entry.uri = uri;
    entry.available = available;
    entry.kind = kind;
    m_error.clear();
    if (different) notify();
    return true;
}

bool AssetDependencyIndex::setAvailable(const QString &id, bool available)
{
    const auto found = m_assets.find(id);
    if (found == m_assets.end()) return fail(QStringLiteral("Unknown asset"));
    if (found->available == available) return true;
    found->available = available;
    m_error.clear();
    notify();
    return true;
}

QString AssetDependencyIndex::beginScan(const QString &sourceId)
{
    const auto found = m_assets.find(sourceId);
    if (found == m_assets.end() || found->scanToken == std::numeric_limits<quint64>::max()) {
        fail(QStringLiteral("Unknown asset or scan token exhausted"));
        return {};
    }
    ++found->scanToken;
    return QString::number(found->scanToken);
}

bool AssetDependencyIndex::commitScan(const QString &sourceId, const QString &token,
                                      const QStringList &dependencies)
{
    if (dependencies.size() > MaxEdgesPerAsset)
        return fail(QStringLiteral("Dependency edge limit exceeded"));
    QHash<QString, Edge> next;
    for (const QString &dependency : dependencies)
        next.insert(dependency, Edge{QStringLiteral("host"), 0, dependency});
    return commitEdges(sourceId, token, std::move(next));
}

bool AssetDependencyIndex::commitScanEdges(const QString &sourceId, const QString &token,
                                           const QVariantList &edges)
{
    if (edges.size() > MaxEdgesPerAsset)
        return fail(QStringLiteral("Dependency edge limit exceeded"));
    QHash<QString, Edge> next;
    for (const QVariant &raw : edges) {
        const QVariantMap row = raw.toMap();
        const QString target = row.value(QStringLiteral("targetId")).toString();
        Edge edge{row.value(QStringLiteral("kind"), QStringLiteral("host")).toString(),
                  row.value(QStringLiteral("line"), 0).toInt(),
                  row.value(QStringLiteral("reference"), target).toString()};
        if (edge.kind != QStringLiteral("import") && edge.kind != QStringLiteral("reference")
            && edge.kind != QStringLiteral("host"))
            return fail(QStringLiteral("Unknown dependency edge kind"));
        if (edge.line < 0 || edge.reference.size() > 2048)
            return fail(QStringLiteral("Invalid dependency edge location"));
        const auto existing = next.constFind(target);
        if (existing == next.cend() || (edge.line > 0 && edge.line < existing->line))
            next.insert(target, edge);
    }
    return commitEdges(sourceId, token, std::move(next));
}

bool AssetDependencyIndex::commitEdges(const QString &sourceId, const QString &token,
                                       QHash<QString, Edge> next)
{
    const auto found = m_assets.find(sourceId);
    if (found == m_assets.end() || found->scanToken == 0 || token.isEmpty()
        || QString::number(found->scanToken) != token) {
        emit scanRejected(sourceId, QStringLiteral("Stale or unknown scan result"));
        return fail(QStringLiteral("Stale or unknown scan result"));
    }
    for (auto it = next.cbegin(); it != next.cend(); ++it)
        if (!validId(it.key())) return fail(QStringLiteral("Invalid dependency ID"));
    if (next.size() > MaxEdgesPerAsset
        || m_edgeCount - found->dependencies.size() + next.size() > MaxEdges)
        return fail(QStringLiteral("Dependency edge limit exceeded"));
    int missing = 0;
    for (auto it = next.cbegin(); it != next.cend(); ++it)
        if (!m_assets.contains(it.key())) ++missing;
    if (m_assets.size() + missing > MaxAssets)
        return fail(QStringLiteral("Asset index is full"));
    for (auto it = next.cbegin(); it != next.cend(); ++it) {
        const QString &id = it.key();
        if (!m_assets.contains(id)) {
            Asset placeholder;
            placeholder.id = id;
            placeholder.label = QFileInfo(id).fileName();
            if (placeholder.label.isEmpty()) placeholder.label = id;
            placeholder.kind = QStringLiteral("unresolved");
            placeholder.discovered = true;
            m_assets.insert(id, placeholder);
        }
    }
    Asset &source = m_assets[sourceId];
    QSet<QString> dropped;
    for (auto it = source.dependencies.cbegin(); it != source.dependencies.cend(); ++it)
        if (!next.contains(it.key())) dropped.insert(it.key());
    m_edgeCount += next.size() - source.dependencies.size();
    source.dependencies = std::move(next);
    m_error.clear();
    pruneOrphans(dropped);  // a removed reference's placeholder goes with it
    notify();
    return true;
}

QStringList AssetDependencyIndex::dependencies(const QString &id) const
{
    const auto found = m_assets.constFind(id);
    if (found == m_assets.cend()) return {};
    QStringList result = found->dependencies.keys();
    result.sort();
    return result;
}

QVariantList AssetDependencyIndex::references(const QString &id) const
{
    QVariantList result;
    const auto found = m_assets.constFind(id);
    if (found == m_assets.cend()) return result;
    for (const QString &target : dependencies(id)) {
        const Edge edge = found->dependencies.value(target);
        result.append(QVariantMap{{QStringLiteral("targetId"), target},
                                  {QStringLiteral("kind"), edge.kind},
                                  {QStringLiteral("line"), edge.line},
                                  {QStringLiteral("reference"), edge.reference},
                                  {QStringLiteral("available"), m_assets.value(target).available}});
    }
    std::stable_sort(result.begin(), result.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("line")).toInt()
               < b.toMap().value(QStringLiteral("line")).toInt();
    });
    return result;
}

QStringList AssetDependencyIndex::dependents(const QString &id) const
{
    QStringList result;
    for (auto it = m_assets.cbegin(); it != m_assets.cend(); ++it)
        if (it->dependencies.contains(id)) result.append(it.key());
    result.sort();
    return result;
}

QStringList AssetDependencyIndex::affectedBy(const QString &id) const
{
    if (!m_assets.contains(id)) return {};
    QHash<QString, QStringList> reverse;
    for (auto it = m_assets.cbegin(); it != m_assets.cend(); ++it)
        for (auto edge = it->dependencies.cbegin(); edge != it->dependencies.cend(); ++edge)
            reverse[edge.key()].append(it.key());
    QSet<QString> seen{id};
    QQueue<QString> queue;
    queue.enqueue(id);
    while (!queue.isEmpty()) {
        const QString current = queue.dequeue();
        for (const QString &parent : reverse.value(current)) {
            if (seen.contains(parent)) continue;
            seen.insert(parent);
            queue.enqueue(parent);
        }
    }
    seen.remove(id);
    QStringList result = seen.values();
    result.sort();
    return result;
}

QVariantList AssetDependencyIndex::diagnostics() const
{
    QVariantList result;
    QStringList ids = m_assets.keys();
    ids.sort();
    for (const QString &source : ids) {
        const Asset &entry = *m_assets.constFind(source);
        for (const QString &target : dependencies(source)) {
            if (m_assets.value(target).available) continue;
            const Edge edge = entry.dependencies.value(target);
            const QString where = edge.line > 0
                ? QStringLiteral("%1 line %2").arg(source).arg(edge.line) : source;
            result.append(QVariantMap{{QStringLiteral("type"), QStringLiteral("missing")},
                                      {QStringLiteral("sourceId"), source},
                                      {QStringLiteral("targetId"), target},
                                      {QStringLiteral("line"), edge.line},
                                      {QStringLiteral("reference"), edge.reference},
                                      {QStringLiteral("message"),
                                       QStringLiteral("%1 needs missing %2").arg(where, target)}});
        }
    }
    QHash<QString, int> index, low;
    QSet<QString> onStack;
    QStringList stack;
    int serial = 0;
    std::function<void(const QString &)> visit = [&](const QString &id) {
        index.insert(id, serial);
        low.insert(id, serial++);
        stack.append(id);
        onStack.insert(id);
        for (const QString &target : dependencies(id)) {
            if (!index.contains(target)) {
                visit(target);
                low[id] = std::min(low.value(id), low.value(target));
            } else if (onStack.contains(target)) {
                low[id] = std::min(low.value(id), index.value(target));
            }
        }
        if (low.value(id) != index.value(id)) return;
        QStringList members;
        QString member;
        do {
            member = stack.takeLast();
            onStack.remove(member);
            members.append(member);
        } while (member != id);
        members.sort();
        if (members.size() > 1 || m_assets.value(id).dependencies.contains(id))
            result.append(QVariantMap{{QStringLiteral("type"), QStringLiteral("cycle")},
                                      {QStringLiteral("sourceId"), members.first()},
                                      {QStringLiteral("members"), members},
                                      {QStringLiteral("message"),
                                       QStringLiteral("Cycle: %1").arg(members.join(QStringLiteral(" → ")))}});
    };
    for (const QString &id : ids) if (!index.contains(id)) visit(id);
    return result;
}

} // namespace QindaTK
