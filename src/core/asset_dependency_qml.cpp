// SPDX-License-Identifier: LGPL-3.0-or-later
#include "asset_dependency_index.h"
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>

namespace QindaTK {
namespace {
constexpr qsizetype MaxQmlBytes = 1024 * 1024;
constexpr int MaxQmlReferences = 128;

// Preserve strings and newlines while hiding comments, so a commented-out
// source/import does not become a dependency.
QString withoutComments(QString text)
{
    enum class State { Code, Line, Block, Single, Double } state = State::Code;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        const QChar next = i + 1 < text.size() ? text.at(i + 1) : QChar{};
        if (state == State::Line) {
            if (ch == QLatin1Char('\n')) state = State::Code;
            else text[i] = QLatin1Char(' ');
        } else if (state == State::Block) {
            if (ch == QLatin1Char('*') && next == QLatin1Char('/')) {
                text[i] = text[i + 1] = QLatin1Char(' ');
                ++i;
                state = State::Code;
            } else if (ch != QLatin1Char('\n')) text[i] = QLatin1Char(' ');
        } else if (state == State::Single || state == State::Double) {
            if (ch == QLatin1Char('\\')) { ++i; continue; }
            if (ch == (state == State::Single ? QLatin1Char('\'') : QLatin1Char('"')))
                state = State::Code;
        } else if (ch == QLatin1Char('/') && next == QLatin1Char('/')) {
            text[i] = text[i + 1] = QLatin1Char(' ');
            ++i;
            state = State::Line;
        } else if (ch == QLatin1Char('/') && next == QLatin1Char('*')) {
            text[i] = text[i + 1] = QLatin1Char(' ');
            ++i;
            state = State::Block;
        } else if (ch == QLatin1Char('\'')) state = State::Single;
        else if (ch == QLatin1Char('"')) state = State::Double;
    }
    return text;
}
}

QString AssetDependencyIndex::absolutePathIn(const QString &root, const QString &id)
{
    if (root.isEmpty() || id.isEmpty() || QDir::isAbsolutePath(id)
        || id == QStringLiteral("..") || id.startsWith(QStringLiteral("../"))) return {};
    const QString clean = QDir::cleanPath(id);
    if (clean != id) return {};
    const QString path = QDir(root).absoluteFilePath(id);
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty() || !canonical.startsWith(root + QLatin1Char('/')))
        return {};
    return canonical;
}

namespace {
QString localId(const QString &root, const QString &sourceId, const QString &reference)
{
    if (reference.isEmpty() || !QUrl(reference).scheme().isEmpty()
        || reference.startsWith(QLatin1Char(':'))
        || reference.startsWith(QLatin1Char('/'))
        || reference.contains(QStringLiteral("://"))
        || reference.startsWith(QStringLiteral("data:"))) return {};
    const QString parent = QFileInfo(sourceId).path();
    const QString id = QDir::cleanPath(QDir(parent).filePath(reference));
    if (id == QStringLiteral("..") || id.startsWith(QStringLiteral("../"))
        || id.startsWith(QLatin1Char('/')) || id.size() > 512) return {};
    const QString absolute = QDir(root).absoluteFilePath(id);
    const QString canonical = QFileInfo(absolute).canonicalFilePath();
    if (!canonical.isEmpty() && !canonical.startsWith(root + QLatin1Char('/')))
        return {};
    return id;
}
}

// AGENT-NOTE: extraction is a pure function of (root, file bytes) so the
// project worker can run it off the GUI thread; only applyExtraction touches
// the index. Keep it free of member state.
AssetDependencyIndex::Extraction AssetDependencyIndex::extractQml(const QString &root,
                                                                 const QString &sourceId)
{
    Extraction result;
    const QString path = absolutePathIn(root, sourceId);
    if (path.isEmpty() || !path.endsWith(QStringLiteral(".qml"))) {
        result.error = QStringLiteral("QML source must be inside the project root");
        return result;
    }
    QFileInfo before(path);
    if (!before.isFile() || before.size() > MaxQmlBytes) {
        result.error = QStringLiteral("QML source is missing or exceeds 1 MiB");
        return result;
    }
    const qint64 initialSize = before.size();
    const QDateTime initialModified = before.lastModified();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Cannot read QML source");
        return result;
    }
    const QByteArray bytes = file.read(MaxQmlBytes + 1);
    if (bytes.size() > MaxQmlBytes) {
        result.error = QStringLiteral("QML source exceeds 1 MiB");
        return result;
    }
    const QString content = withoutComments(QString::fromUtf8(bytes));
    QList<qsizetype> newlines;
    for (qsizetype i = 0; i < content.size(); ++i)
        if (content.at(i) == QLatin1Char('\n')) newlines.append(i);
    auto lineAt = [&](qsizetype offset) {
        return int(std::lower_bound(newlines.cbegin(), newlines.cend(), offset)
                   - newlines.cbegin()) + 1;
    };
    static const QRegularExpression imports(
        QStringLiteral(R"((?m)^\s*import\s+["']([^"']+)["'])"));
    static const QRegularExpression references(
        QStringLiteral(R"(\b(?:source|iconSource|url|file|fileName|texture)\s*:\s*(?:Qt\.resolvedUrl\s*\()?\s*["']([^"']+)["'])"));
    QHash<QString, ExtractedEdge> found;
    auto add = [&](const QString &target, const Edge &edge) {
        const auto existing = found.constFind(target);
        if (existing != found.cend() && existing->edge.line <= edge.line) return;
        const QFileInfo info(QDir(root).absoluteFilePath(target));
        found.insert(target, ExtractedEdge{target, edge, info.exists()});
    };
    auto addReference = [&](const QRegularExpressionMatch &match, bool imported) {
        const QString reference = match.captured(1);
        const QString id = localId(root, sourceId, reference);
        if (id.isEmpty()) return;
        const Edge edge{imported ? QStringLiteral("import") : QStringLiteral("reference"),
                        lineAt(match.capturedStart(1)), reference};
        const QString absolute = QDir(root).absoluteFilePath(id);
        if (imported && QFileInfo(absolute).isDir()) {
            QDirIterator files(absolute, {QStringLiteral("*.qml")},
                QDir::Files | QDir::Readable | QDir::NoSymLinks);
            while (files.hasNext() && found.size() <= MaxQmlReferences) {
                files.next();
                const QFileInfo qml = files.fileInfo();
                const QString child = QDir(root).relativeFilePath(qml.canonicalFilePath());
                if (validId(child) && !child.startsWith(QStringLiteral("../")))
                    add(child, edge);
            }
        } else {
            add(id, edge);
        }
    };
    auto iterator = imports.globalMatch(content);
    while (iterator.hasNext()) addReference(iterator.next(), true);
    iterator = references.globalMatch(content);
    while (iterator.hasNext()) addReference(iterator.next(), false);
    if (found.size() > MaxQmlReferences) {
        result.error = QStringLiteral("QML source has too many local references");
        return result;
    }
    before.refresh();
    if (before.size() != initialSize || bytes.size() != initialSize
        || before.lastModified() != initialModified) {
        result.changedDuringRead = true;
        result.error = QStringLiteral("QML source changed during scan");
        return result;
    }
    result.edges = found.values();
    return result;
}

bool AssetDependencyIndex::applyExtraction(const QString &sourceId, const Extraction &extraction)
{
    if (!extraction.error.isEmpty()) {
        if (extraction.changedDuringRead) emit scanRejected(sourceId, extraction.error);
        return fail(extraction.error);
    }
    const QString path = absolutePathIn(m_projectRoot, sourceId);
    if (!upsertAsset(sourceId, QFileInfo(sourceId).fileName(), path, true,
                     QStringLiteral("qml")))
        return false;
    const QString token = beginScan(sourceId);
    if (token.isEmpty()) return false;
    QHash<QString, Edge> next;
    for (const ExtractedEdge &found : extraction.edges) {
        const QString &id = found.targetId;
        const QFileInfo info(QDir(m_projectRoot).absoluteFilePath(id));
        const QVariantMap existing = asset(id);
        const QString label = existing.value(QStringLiteral("label"), info.fileName()).toString();
        QString kind = existing.value(QStringLiteral("kind")).toString();
        if (kind.isEmpty() || kind == QStringLiteral("unresolved"))
            kind = id.endsWith(QStringLiteral(".qml")) ? QStringLiteral("qml")
                                                       : QStringLiteral("file");
        if (!upsertAsset(id, label, info.absoluteFilePath(), found.exists, kind)) return false;
        next.insert(id, found.edge);
    }
    return commitEdges(sourceId, token, std::move(next));
}

bool AssetDependencyIndex::scanQmlFile(const QString &sourceId)
{
    // AGENT-GUARD: this synchronous form reads one bounded file on the
    // caller's thread. Whole projects go through scanProjectAsync.
    return applyExtraction(sourceId, extractQml(m_projectRoot, sourceId));
}

} // namespace QindaTK
