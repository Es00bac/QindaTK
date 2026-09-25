// SPDX-License-Identifier: LGPL-3.0-or-later
#include "asset_dependency_index.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <algorithm>

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
    QStringList files;
    QDirIterator it(root, {QStringLiteral("*.qml")},
                    QDir::Files | QDir::Readable | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (cancel->load()) { scan.cancelled = true; return scan; }
        it.next();
        const QString id = QDir(root).relativeFilePath(it.fileInfo().canonicalFilePath());
        if (id.isEmpty() || id.startsWith(QStringLiteral("../"))) continue;
        if (files.size() >= MaxProjectSources) {
            scan.error = QStringLiteral("Project has more than 4,096 QML files");
            return scan;
        }
        files.append(id);
    }
    files.sort();
    scan.sources.reserve(files.size());
    for (const QString &id : std::as_const(files)) {
        if (cancel->load()) { scan.cancelled = true; return scan; }
        scan.sources.append(SourceResult{id, extractQml(root, id)});
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
        if (generation != m_projectGeneration || scan.cancelled) {
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
    // QML sources that were indexed but no longer exist are deleted: their
    // outgoing edges go, their dependents now report a missing input.
    for (auto it = m_assets.begin(); it != m_assets.end(); ++it) {
        if (it->kind != QStringLiteral("qml") || seen.contains(it.key()) || newer(it.key()))
            continue;
        if (QFileInfo::exists(QDir(m_projectRoot).absoluteFilePath(it.key()))) continue;
        it->available = false;
        m_edgeCount -= it->dependencies.size();
        it->dependencies.clear();
        ++it->scanToken;
    }
    --m_quiet;
    m_error = firstError;
    notify();
    emit projectScanFinished(requestId, applied, skipped);
}

QStringList AssetDependencyIndex::relinkCandidates(const QString &id) const
{
    const QString name = QFileInfo(id).fileName();
    QStringList result;
    if (m_projectRoot.isEmpty() || name.isEmpty()) return result;
    QDirIterator it(m_projectRoot, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot
                                   | QDir::NoSymLinks, QDirIterator::Subdirectories);
    int visited = 0;
    while (it.hasNext() && visited < MaxRelinkEntriesVisited
           && result.size() < MaxRelinkCandidates) {
        it.next();
        ++visited;
        const QFileInfo info = it.fileInfo();
        if (!info.isFile() || info.fileName() != name) continue;
        const QString candidate = QDir(m_projectRoot).relativeFilePath(info.canonicalFilePath());
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
