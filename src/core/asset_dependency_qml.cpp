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
#include <functional>

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

namespace {
// Blanks <!-- --> and /* */ comments, keeping newlines so lines stay true.
QString withoutMarkupComments(QString text)
{
    qsizetype i = 0;
    while (i < text.size()) {
        const bool html = text.mid(i, 4) == QStringLiteral("<!--");
        const bool css = !html && text.mid(i, 2) == QStringLiteral("/*");
        if (!html && !css) { ++i; continue; }
        const QString close = html ? QStringLiteral("-->") : QStringLiteral("*/");
        qsizetype end = text.indexOf(close, i + (html ? 4 : 2));
        end = end < 0 ? text.size() : end + close.size();
        for (qsizetype j = i; j < end; ++j)
            if (text.at(j) != QLatin1Char('\n')) text[j] = QLatin1Char(' ');
        i = end;
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

namespace {
// A bounded read that notices a file changing underneath it.
struct SourceRead {
    QString text;
    QString error;
    qint64 size = 0;
    QDateTime modified;
    QString path;
};

SourceRead readSource(const QString &path, const QString &what)
{
    SourceRead read;
    read.path = path;
    const QFileInfo before(path);
    if (!before.isFile() || before.size() > MaxQmlBytes) {
        read.error = QStringLiteral("%1 source is missing or exceeds 1 MiB").arg(what);
        return read;
    }
    read.size = before.size();
    read.modified = before.lastModified();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        read.error = QStringLiteral("Cannot read %1 source").arg(what);
        return read;
    }
    const QByteArray bytes = file.read(MaxQmlBytes + 1);
    if (bytes.size() > MaxQmlBytes || bytes.size() != read.size) {
        read.error = bytes.size() > MaxQmlBytes
            ? QStringLiteral("%1 source exceeds 1 MiB").arg(what)
            : QStringLiteral("%1 source changed during scan").arg(what);
        return read;
    }
    read.text = QString::fromUtf8(bytes);
    return read;
}

bool changedSince(const SourceRead &read)
{
    const QFileInfo after(read.path);
    return after.size() != read.size || after.lastModified() != read.modified;
}

std::function<int(qsizetype)> lineIndex(const QString &content)
{
    QList<qsizetype> newlines;
    for (qsizetype i = 0; i < content.size(); ++i)
        if (content.at(i) == QLatin1Char('\n')) newlines.append(i);
    return [newlines](qsizetype offset) {
        return int(std::lower_bound(newlines.cbegin(), newlines.cend(), offset)
                   - newlines.cbegin()) + 1;
    };
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
    const SourceRead read = readSource(path, QStringLiteral("QML"));
    if (!read.error.isEmpty()) {
        result.error = read.error;
        result.changedDuringRead = read.error.endsWith(QStringLiteral("changed during scan"));
        return result;
    }
    const QString content = withoutComments(read.text);
    const auto lineAt = lineIndex(content);
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
    if (changedSince(read)) {
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
    const QString sourceType = sourceKind(sourceId);
    if (!upsert(sourceId, QFileInfo(sourceId).fileName(), path, true,
                sourceType.isEmpty() ? QStringLiteral("qml") : sourceType, true))
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
            kind = sourceKind(id).isEmpty() ? QStringLiteral("file") : sourceKind(id);
        if (!upsert(id, label, info.absoluteFilePath(), found.exists, kind, true)) return false;
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

bool AssetDependencyIndex::scanFile(const QString &sourceId)
{
    return applyExtraction(sourceId, extractSource(m_projectRoot, sourceId));
}

QString AssetDependencyIndex::sourceKind(const QString &id)
{
    const QString suffix = QFileInfo(id).suffix().toLower();
    if (suffix == QStringLiteral("qml")) return QStringLiteral("qml");
    if (suffix == QStringLiteral("svg")) return QStringLiteral("svg");
    if (suffix == QStringLiteral("html") || suffix == QStringLiteral("htm"))
        return QStringLiteral("html");
    if (suffix == QStringLiteral("css")) return QStringLiteral("css");
    return {};
}

AssetDependencyIndex::Extraction AssetDependencyIndex::extractSource(const QString &root,
                                                                    const QString &sourceId)
{
    const QString kind = sourceKind(sourceId);
    if (kind == QStringLiteral("qml")) return extractQml(root, sourceId);
    if (!kind.isEmpty()) return extractMarkup(root, sourceId);
    Extraction result;
    result.error = QStringLiteral("Not a scannable source: %1").arg(sourceId);
    return result;
}

// AGENT-NOTE (EXT-10): documents and media reach each other through markup
// references: SVG/HTML href/src/poster attributes, CSS url() and @import.
// Same rules as QML: local, relative, inside the root; fragments and queries
// are dropped from the ID but kept in `reference`; comments are ignored.
AssetDependencyIndex::Extraction AssetDependencyIndex::extractMarkup(const QString &root,
                                                                    const QString &sourceId)
{
    Extraction result;
    const QString kind = sourceKind(sourceId);
    const QString path = absolutePathIn(root, sourceId);
    if (path.isEmpty() || kind.isEmpty() || kind == QStringLiteral("qml")) {
        result.error = QStringLiteral("Markup source must be inside the project root");
        return result;
    }
    const SourceRead read = readSource(path, kind.toUpper());
    if (!read.error.isEmpty()) {
        result.error = read.error;
        result.changedDuringRead = read.error.endsWith(QStringLiteral("changed during scan"));
        return result;
    }
    const QString content = withoutMarkupComments(read.text);
    const auto lineAt = lineIndex(content);
    static const QRegularExpression attributes(QStringLiteral(
        R"(\b(?:xlink:href|href|src|poster)\s*=\s*["']([^"']+)["'])"));
    static const QRegularExpression urls(QStringLiteral(
        R"(\burl\(\s*["']?([^"')\s]+)["']?\s*\))"));
    static const QRegularExpression cssImports(QStringLiteral(
        R"(@import\s+(?:url\(\s*)?["']([^"']+)["'])"));
    QHash<QString, ExtractedEdge> found;
    auto scan = [&](const QRegularExpression &pattern, const QString &edgeKind) {
        auto it = pattern.globalMatch(content);
        while (it.hasNext() && found.size() <= MaxQmlReferences) {
            const auto match = it.next();
            const QString reference = match.captured(1).trimmed();
            QString local = reference.section(QLatin1Char('#'), 0, 0)
                                .section(QLatin1Char('?'), 0, 0);
            const QString id = localId(root, sourceId, local);
            if (id.isEmpty()) continue;
            const Edge edge{edgeKind, lineAt(match.capturedStart(1)), reference};
            const auto existing = found.constFind(id);
            if (existing != found.cend() && existing->edge.line <= edge.line) continue;
            found.insert(id, ExtractedEdge{id, edge,
                                           QFileInfo::exists(QDir(root).absoluteFilePath(id))});
        }
    };
    if (kind != QStringLiteral("css")) scan(attributes, QStringLiteral("reference"));
    scan(cssImports, QStringLiteral("import"));
    scan(urls, QStringLiteral("reference"));
    if (found.size() > MaxQmlReferences) {
        result.error = QStringLiteral("Markup source has too many local references");
        return result;
    }
    if (changedSince(read)) {
        result.changedDuringRead = true;
        result.error = QStringLiteral("%1 source changed during scan").arg(kind.toUpper());
        return result;
    }
    result.edges = found.values();
    return result;
}

} // namespace QindaTK
