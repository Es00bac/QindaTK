// SPDX-License-Identifier: LGPL-3.0-or-later
#include "asset_dependency_index.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <algorithm>
#include <utility>

namespace QindaTK {
namespace {
constexpr int MaxProjectSources = 4096;
constexpr int MaxRelinkEntriesVisited = 20000;
constexpr int MaxRelinkCandidates = 32;
}

// AGENT-NOTE (D-162): the worker only reads files and extracts edges. It
// never touches m_assets; the GUI thread applies the result, and only if no
// newer project scan or per-source scan happened meanwhile.
AssetDependencyIndex::ProjectScan
AssetDependencyIndex::runProjectScan(const QString &root,
                                     std::shared_ptr<std::atomic_bool> cancel)
{
    ProjectScan scan;
    scan.root = root;
    QStringList files;
    QDirIterator it(root, {QStringLiteral("*.qml"), QStringLiteral("*.svg"),
                           QStringLiteral("*.html"), QStringLiteral("*.htm"),
                           QStringLiteral("*.css")},
                    QDir::Files | QDir::Readable | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (cancel->load()) { scan.cancelled = true; return scan; }
        it.next();
        const QString id = QDir(root).relativeFilePath(it.fileInfo().canonicalFilePath());
        if (id.isEmpty() || id.startsWith(QStringLiteral("../"))) continue;
        if (files.size() >= MaxProjectSources) {
            scan.error = QStringLiteral("Project has more than 4,096 source files");
            return scan;
        }
        files.append(id);
    }
    files.sort();
    scan.sources.reserve(files.size());
    for (const QString &id : std::as_const(files)) {
        if (cancel->load()) { scan.cancelled = true; return scan; }
        scan.sources.append(SourceResult{id, extractSource(root, id)});
    }
    return scan;
}

QString AssetDependencyIndex::scanProjectAsync()
{
    if (m_projectRoot.isEmpty()) {
        fail(QStringLiteral("Set the project root before scanning"));
        return {};
    }
    if (m_scanCancel) m_scanCancel->store(true);
    auto cancel = std::make_shared<std::atomic_bool>(false);
    m_scanCancel = cancel;
    const quint64 generation = ++m_projectGeneration;
    const QString requestId = QString::number(generation);
    QHash<QString, quint64> tokens;
    for (auto it = m_assets.cbegin(); it != m_assets.cend(); ++it)
        tokens.insert(it.key(), it->scanToken);
    const QString root = m_projectRoot;
    auto *watcher = new QFutureWatcher<ProjectScan>(this);
    connect(watcher, &QFutureWatcher<ProjectScan>::finished, this,
            [this, watcher, generation, requestId, tokens] {
        const ProjectScan scan = watcher->result();
        watcher->deleteLater();
        if (generation != m_projectGeneration || scan.cancelled || scan.root != m_projectRoot) {
            emit projectScanDiscarded(requestId);
            return;
        }
        m_scanCancel.reset();
        applyProjectScan(scan, tokens, requestId);
    });
    m_error.clear();
    notify();
    watcher->setFuture(QtConcurrent::run([root, cancel] { return runProjectScan(root, cancel); }));
    return requestId;
}

void AssetDependencyIndex::cancelProjectScan()
{
    if (!m_scanCancel) return;
    m_scanCancel->store(true);
    m_scanCancel.reset();
    ++m_projectGeneration;
    notify();
}

void AssetDependencyIndex::applyProjectScan(const ProjectScan &scan,
                                            const QHash<QString, quint64> &tokens,
                                            const QString &requestId)
{
    if (!scan.error.isEmpty()) {
        fail(scan.error);
        emit projectScanFinished(requestId, 0, 0);
        return;
    }
    // A source rescanned after this scan started holds a newer answer.
    auto newer = [&](const QString &id) {
        const auto found = m_assets.constFind(id);
        return found != m_assets.cend() && found->scanToken != tokens.value(id, 0);
    };
    int applied = 0, skipped = 0;
    QString firstError;
    QSet<QString> seen;
    ++m_quiet;
    for (const SourceResult &source : scan.sources) {
        seen.insert(source.sourceId);
        if (newer(source.sourceId)) { ++skipped; continue; }
        if (applyExtraction(source.sourceId, source.extraction)) ++applied;
        else if (firstError.isEmpty()) firstError = m_error;
    }
    // Sources that were indexed but no longer exist are deleted: their
    // outgoing edges go and their dependents report a missing input. One
    // nothing uses any more is pruned, with the placeholders only it used.
    QSet<QString> deleted;
    for (auto it = m_assets.begin(); it != m_assets.end(); ++it) {
        const QString kind = sourceKind(it.key());
        if (kind.isEmpty() || it->kind != kind || seen.contains(it.key()) || newer(it.key()))
            continue;
        if (QFileInfo::exists(QDir(m_projectRoot).absoluteFilePath(it.key()))) continue;
        it->available = false;
        m_edgeCount -= it->dependencies.size();
        for (auto edge = it->dependencies.cbegin(); edge != it->dependencies.cend(); ++edge)
            deleted.insert(edge.key());
        it->dependencies.clear();
        ++it->scanToken;
        deleted.insert(it.key());
    }
    --m_quiet;
    // Twice: a deleted source pruned first can leave its deleted inputs unused.
    QSet<QString> candidates = std::exchange(m_pruneCandidates, {}) + deleted;
    pruneOrphans(candidates);
    pruneOrphans(candidates);
    m_error = firstError;
    notify();
    emit projectScanFinished(requestId, applied, skipped);
}

QStringList AssetDependencyIndex::relinkCandidates(const QString &id) const
{
    return findRelinkCandidates(m_projectRoot, id);
}

QString AssetDependencyIndex::relinkCandidatesAsync(const QString &id)
{
    // AGENT-NOTE: views use this form; the walk visits up to 20,000 entries
    // and must not run inside a binding on the GUI thread.
    const QString requestId = QStringLiteral("relink-%1").arg(++m_relinkSerial);
    const QString root = m_projectRoot;
    auto *watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this,
            [this, watcher, requestId, id, root] {
        const QStringList candidates = watcher->result();
        watcher->deleteLater();
        // A root change meanwhile makes the relative candidates meaningless.
        emit relinkCandidatesReady(requestId, id, root == m_projectRoot ? candidates
                                                                         : QStringList{});
    });
    watcher->setFuture(QtConcurrent::run([root, id] { return findRelinkCandidates(root, id); }));
    return requestId;
}

QStringList AssetDependencyIndex::findRelinkCandidates(const QString &root, const QString &id)
{
    const QString name = QFileInfo(id).fileName();
    QStringList result;
    if (root.isEmpty() || name.isEmpty()) return result;
    QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot
                                   | QDir::NoSymLinks, QDirIterator::Subdirectories);
    int visited = 0;
    while (it.hasNext() && visited < MaxRelinkEntriesVisited
           && result.size() < MaxRelinkCandidates) {
        it.next();
        ++visited;
        const QFileInfo info = it.fileInfo();
        if (!info.isFile() || info.fileName() != name) continue;
        const QString candidate = QDir(root).relativeFilePath(info.canonicalFilePath());
        if (candidate != id && !candidate.startsWith(QStringLiteral("../")))
            result.append(candidate);
    }
    result.sort();
    return result;
}

QVariantList AssetDependencyIndex::relinkPlan(const QString &missingId,
                                              const QString &replacementId) const
{
    QVariantList result;
    if (missingId == replacementId || !m_assets.contains(missingId)
        || absolutePathIn(m_projectRoot, replacementId).isEmpty())
        return result;
    for (const QString &source : dependents(missingId)) {
        const Edge edge = m_assets.value(source).dependencies.value(missingId);
        QString replacement;
        QString note;
        if (edge.kind == QStringLiteral("reference")) {
            const QString sourceDir = QDir::cleanPath(QDir(m_projectRoot)
                .absoluteFilePath(QFileInfo(source).path()));
            replacement = QDir(sourceDir).relativeFilePath(
                QDir(m_projectRoot).absoluteFilePath(replacementId));
        } else if (edge.kind == QStringLiteral("host")) {
            replacement = replacementId;
            note = QStringLiteral("Host-owned reference; update it through the host");
        } else {
            note = QStringLiteral("Directory import; restore the file or edit the import");
        }
        result.append(QVariantMap{{QStringLiteral("sourceId"), source},
                                  {QStringLiteral("line"), edge.line},
                                  {QStringLiteral("kind"), edge.kind},
                                  {QStringLiteral("reference"), edge.reference},
                                  {QStringLiteral("replacement"), replacement},
                                  {QStringLiteral("note"), note}});
    }
    return result;
}

} // namespace QindaTK
