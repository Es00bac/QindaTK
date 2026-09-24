// SPDX-License-Identifier: LGPL-3.0-or-later
#include "asset_dependency_index.h"
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

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

QString AssetDependencyIndex::absolutePathFor(const QString &id) const
{
    if (m_projectRoot.isEmpty() || id.isEmpty() || QDir::isAbsolutePath(id)
        || id == QStringLiteral("..") || id.startsWith(QStringLiteral("../"))) return {};
    const QString clean = QDir::cleanPath(id);
    if (clean != id) return {};
    const QString path = QDir(m_projectRoot).absoluteFilePath(id);
    const QString canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty() || !canonical.startsWith(m_projectRoot + QLatin1Char('/')))
        return {};
    return canonical;
}

QString AssetDependencyIndex::localId(const QString &sourceId,
                                      const QString &reference) const
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
    const QString absolute = QDir(m_projectRoot).absoluteFilePath(id);
    const QString canonical = QFileInfo(absolute).canonicalFilePath();
    if (!canonical.isEmpty() && !canonical.startsWith(m_projectRoot + QLatin1Char('/')))
        return {};
    return id;
}

bool AssetDependencyIndex::scanQmlFile(const QString &sourceId)
{
    const QString path = absolutePathFor(sourceId);
    if (path.isEmpty() || !path.endsWith(QStringLiteral(".qml")))
        return fail(QStringLiteral("QML source must be inside the project root"));
    QFileInfo before(path);
    if (!before.isFile() || before.size() > MaxQmlBytes)
        return fail(QStringLiteral("QML source is missing or exceeds 1 MiB"));
    const qint64 initialSize = before.size();
    const QDateTime initialModified = before.lastModified();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(QStringLiteral("Cannot read QML source"));
    const QByteArray bytes = file.read(MaxQmlBytes + 1);
    if (bytes.size() > MaxQmlBytes) return fail(QStringLiteral("QML source exceeds 1 MiB"));
    if (!upsertAsset(sourceId, before.fileName(), path, true, QStringLiteral("qml")))
        return false;
    const QString token = beginScan(sourceId);
    if (token.isEmpty()) return false;
    const QString content = withoutComments(QString::fromUtf8(bytes));
    static const QRegularExpression imports(
        QStringLiteral(R"((?m)^\s*import\s+["']([^"']+)["'])"));
    static const QRegularExpression references(
        QStringLiteral(R"(\b(?:source|iconSource|url|file|fileName|texture)\s*:\s*(?:Qt\.resolvedUrl\s*\()?\s*["']([^"']+)["'])"));
    QSet<QString> found;
    auto addReference = [&](const QString &reference, bool imported) {
        const QString id = localId(sourceId, reference);
        if (id.isEmpty()) return;
        const QString absolute = QDir(m_projectRoot).absoluteFilePath(id);
        const QFileInfo info(absolute);
        if (imported && info.isDir()) {
            QDirIterator files(absolute, {QStringLiteral("*.qml")},
                QDir::Files | QDir::Readable | QDir::NoSymLinks);
            while (files.hasNext() && found.size() <= MaxQmlReferences) {
                files.next();
                const QFileInfo qml = files.fileInfo();
                const QString child = QDir(m_projectRoot).relativeFilePath(qml.canonicalFilePath());
                if (validId(child) && !child.startsWith(QStringLiteral("../")))
                    found.insert(child);
            }
        } else {
            found.insert(id);
        }
    };
    auto iterator = imports.globalMatch(content);
    while (iterator.hasNext()) addReference(iterator.next().captured(1), true);
    iterator = references.globalMatch(content);
    while (iterator.hasNext()) addReference(iterator.next().captured(1), false);
    if (found.size() > MaxQmlReferences)
        return fail(QStringLiteral("QML source has too many local references"));
    before.refresh();
    if (before.size() != initialSize || bytes.size() != initialSize
        || before.lastModified() != initialModified) {
        emit scanRejected(sourceId, QStringLiteral("QML source changed during scan"));
        return fail(QStringLiteral("QML source changed during scan"));
    }
    for (const QString &id : found) {
        const QFileInfo info(QDir(m_projectRoot).absoluteFilePath(id));
        const QVariantMap existing = asset(id);
        const QString label = existing.value(QStringLiteral("label"), info.fileName()).toString();
        const QString kind = existing.value(QStringLiteral("kind"),
            id.endsWith(QStringLiteral(".qml")) ? QStringLiteral("qml")
                                                 : QStringLiteral("file")).toString();
        if (!upsertAsset(id, label, info.absoluteFilePath(), info.exists(), kind)) return false;
    }
    return commitScan(sourceId, token, found.values());
}

} // namespace QindaTK
