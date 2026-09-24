// SPDX-License-Identifier: LGPL-3.0-or-later
#include "asset_dependency_index.h"
#include <QDir>
#include <QFileInfo>
#include <QQueue>
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

bool AssetDependencyIndex::fail(const QString &message)
{
    m_error = message;
    emit changed();
    return false;
}

void AssetDependencyIndex::setProjectRoot(const QString &path)
{
    if (!m_assets.isEmpty()) { fail(QStringLiteral("Set the project root before adding assets")); return; }
    const QString root = QFileInfo(path).canonicalFilePath();
    if (root.isEmpty() || !QFileInfo(root).isDir()) {
        fail(QStringLiteral("Project root must be an existing directory")); return;
    }
    m_projectRoot = root;
    m_error.clear();
    emit changed();
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
    if (!validId(id) || label.trimmed().isEmpty() || label.size() > 512
        || uri.size() > 2048 || kind.trimmed().isEmpty() || kind.size() > 64)
        return fail(QStringLiteral("Invalid asset ID or metadata"));
    if (!m_assets.contains(id) && m_assets.size() >= MaxAssets)
        return fail(QStringLiteral("Asset index is full"));
    Asset &entry = m_assets[id];
    const bool different = entry.id != id || entry.label != label || entry.uri != uri
                           || entry.available != available || entry.kind != kind;
    entry.id = id;
    entry.label = label;
    entry.uri = uri;
    entry.available = available;
    entry.kind = kind;
    m_error.clear();
    if (different) emit changed();
    return true;
}

bool AssetDependencyIndex::setAvailable(const QString &id, bool available)
{
    const auto found = m_assets.find(id);
    if (found == m_assets.end()) return fail(QStringLiteral("Unknown asset"));
    if (found->available == available) return true;
    found->available = available;
    m_error.clear();
    emit changed();
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
    const auto found = m_assets.find(sourceId);
    if (found == m_assets.end() || found->scanToken == 0 || token.isEmpty()
        || QString::number(found->scanToken) != token) {
        emit scanRejected(sourceId, QStringLiteral("Stale or unknown scan result"));
        return fail(QStringLiteral("Stale or unknown scan result"));
    }
    if (dependencies.size() > MaxEdgesPerAsset)
        return fail(QStringLiteral("Dependency edge limit exceeded"));
    QSet<QString> next;
    for (const QString &dependency : dependencies) {
        if (!validId(dependency)) return fail(QStringLiteral("Invalid dependency ID"));
        next.insert(dependency);
    }
    if (next.size() > MaxEdgesPerAsset
        || m_edgeCount - found->dependencies.size() + next.size() > MaxEdges)
        return fail(QStringLiteral("Dependency edge limit exceeded"));
    int missing = 0;
    for (const QString &id : next) if (!m_assets.contains(id)) ++missing;
    if (m_assets.size() + missing > MaxAssets)
        return fail(QStringLiteral("Asset index is full"));
    for (const QString &id : next) {
        if (!m_assets.contains(id)) {
            Asset placeholder;
            placeholder.id = id;
            placeholder.label = QFileInfo(id).fileName();
            if (placeholder.label.isEmpty()) placeholder.label = id;
            placeholder.kind = QStringLiteral("unresolved");
            m_assets.insert(id, placeholder);
        }
    }
    Asset &source = m_assets[sourceId];
    m_edgeCount += next.size() - source.dependencies.size();
    source.dependencies = std::move(next);
    m_error.clear();
    emit changed();
    return true;
}

QStringList AssetDependencyIndex::dependencies(const QString &id) const
{
    const auto found = m_assets.constFind(id);
    if (found == m_assets.cend()) return {};
    QStringList result = found->dependencies.values();
    result.sort();
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
        for (const QString &dependency : it->dependencies)
            reverse[dependency].append(it.key());
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
        for (const QString &target : dependencies(source)) {
            if (m_assets.value(target).available) continue;
            result.append(QVariantMap{{QStringLiteral("type"), QStringLiteral("missing")},
                                      {QStringLiteral("sourceId"), source},
                                      {QStringLiteral("targetId"), target},
                                      {QStringLiteral("message"),
                                       QStringLiteral("%1 needs missing %2").arg(source, target)}});
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
