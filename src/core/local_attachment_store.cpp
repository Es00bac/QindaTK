// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_attachment_store.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

namespace QindaTK {
namespace {
constexpr auto kSchema = "qindatk.attachment/1";
constexpr qint64 kMaximumBytes = 8 * 1024 * 1024;
}

LocalAttachmentStore::LocalAttachmentStore(QObject *parent) : QObject(parent) {}

QString LocalAttachmentStore::defaultPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/attachments");
}

qint64 LocalAttachmentStore::maximumBytes() const { return kMaximumBytes; }

void LocalAttachmentStore::setRootPath(const QString &path)
{
    if (m_rootPath == path) return;
    m_rootPath = path;
    emit rootPathChanged();
}

QString LocalAttachmentStore::effectiveRoot() const
{
    return m_rootPath.trimmed().isEmpty() ? defaultPath() : m_rootPath;
}

QString LocalAttachmentStore::directoryFor(const QString &attachmentId) const
{
    const QByteArray digest = QCryptographicHash::hash(attachmentId.toUtf8(), QCryptographicHash::Sha256).toHex();
    return effectiveRoot() + QLatin1Char('/') + QString::fromLatin1(digest);
}

QVariantMap LocalAttachmentStore::result(bool ok, const QString &error) const
{
    return {{QStringLiteral("ok"), ok}, {QStringLiteral("status"), m_status},
            {QStringLiteral("error"), error}};
}

void LocalAttachmentStore::setStatus(const QString &status, const QString &error)
{
    if (m_status == status && m_lastError == error) return;
    m_status = status;
    m_lastError = error;
    emit statusChanged();
}

QString LocalAttachmentStore::safeFileName(const QString &name)
{
    QString base = QFileInfo(name).fileName().trimmed();
    base.remove(QChar::Null);
    if (base.isEmpty() || base == QLatin1String(".") || base == QLatin1String(".."))
        base = QStringLiteral("attachment.bin");
    return base.left(180);
}

QVariantMap LocalAttachmentStore::importFile(const QString &attachmentId, const QString &sourcePath)
{
    const QUrl sourceUrl(sourcePath);
    const QString localPath = sourceUrl.isLocalFile() ? sourceUrl.toLocalFile() : sourcePath;
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) {
        const QString error = file.errorString(); setStatus(QStringLiteral("error"), error); return result(false, error);
    }
    if (file.size() < 0 || file.size() > kMaximumBytes) {
        const QString error = QStringLiteral("Attachment exceeds 8 MiB limit"); setStatus(QStringLiteral("error"), error); return result(false, error);
    }
    const QByteArray bytes = file.read(kMaximumBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() != file.size()) {
        const QString error = file.errorString(); setStatus(QStringLiteral("error"), error); return result(false, error);
    }
    const QString mime = QMimeDatabase().mimeTypeForFile(file.fileName()).name();
    return importBytes(attachmentId, QFileInfo(file).fileName(), mime, bytes);
}

QVariantMap LocalAttachmentStore::importBytes(const QString &attachmentId, const QString &fileName,
                                              const QString &mimeType, const QByteArray &bytes)
{
    const QString id = attachmentId.trimmed();
    if (id.isEmpty() || id.size() > 256 || id.contains(QChar::Null)) {
        const QString error = QStringLiteral("Attachment ID must contain 1 to 256 characters"); setStatus(QStringLiteral("error"), error); return result(false, error);
    }
    if (bytes.isEmpty() || bytes.size() > kMaximumBytes) {
        const QString error = QStringLiteral("Attachment is empty or exceeds 8 MiB limit"); setStatus(QStringLiteral("error"), error); return result(false, error);
    }

    const QString root = effectiveRoot();
    if (!QDir().mkpath(root)) {
        const QString error = QStringLiteral("Cannot create attachment directory"); setStatus(QStringLiteral("error"), error); return result(false, error);
    }
    const QString target = directoryFor(id);
    const QString stage = root + QStringLiteral("/.stage-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!QDir().mkpath(stage)) {
        const QString error = QStringLiteral("Cannot create attachment staging directory"); setStatus(QStringLiteral("error"), error); return result(false, error);
    }
    auto cleanup = [&] { QDir(stage).removeRecursively(); };
    QSaveFile payload(stage + QStringLiteral("/payload.bin"));
    payload.setDirectWriteFallback(false);
    if (!payload.open(QIODevice::WriteOnly) || payload.write(bytes) != bytes.size() || !payload.commit()) {
        const QString error = payload.errorString(); cleanup(); setStatus(QStringLiteral("error"), error); return result(false, error);
    }
    QJsonObject metadata{{QStringLiteral("schema"), QString::fromLatin1(kSchema)},
                         {QStringLiteral("id"), id}, {QStringLiteral("name"), safeFileName(fileName)},
                         {QStringLiteral("mimeType"), mimeType.trimmed().isEmpty() ? QStringLiteral("application/octet-stream") : mimeType.trimmed()},
                         {QStringLiteral("size"), bytes.size()}};
    QSaveFile meta(stage + QStringLiteral("/metadata.json"));
    meta.setDirectWriteFallback(false);
    const QByteArray json = QJsonDocument(metadata).toJson(QJsonDocument::Compact);
    if (!meta.open(QIODevice::WriteOnly) || meta.write(json) != json.size() || !meta.commit()) {
        const QString error = meta.errorString(); cleanup(); setStatus(QStringLiteral("error"), error); return result(false, error);
    }

    const QString backup = root + QStringLiteral("/.backup-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const bool replacing = QFileInfo::exists(target);
    if (replacing && !QDir().rename(target, backup)) {
        cleanup(); const QString error = QStringLiteral("Cannot stage previous attachment version"); setStatus(QStringLiteral("error"), error); return result(false, error);
    }
    if (!QDir().rename(stage, target)) {
        if (replacing) QDir().rename(backup, target);
        cleanup(); const QString error = QStringLiteral("Cannot publish attachment data"); setStatus(QStringLiteral("error"), error); return result(false, error);
    }
    if (replacing) QDir(backup).removeRecursively();
    setStatus(QStringLiteral("stored"));
    QVariantMap value = result(true);
    value.insert(QStringLiteral("id"), id);
    value.insert(QStringLiteral("name"), safeFileName(fileName));
    value.insert(QStringLiteral("mimeType"), metadata.value(QStringLiteral("mimeType")).toString());
    value.insert(QStringLiteral("size"), bytes.size());
    return value;
}

QVariantMap LocalAttachmentStore::read(const QString &attachmentId) const
{
    const QString id = attachmentId.trimmed();
    if (id.isEmpty()) return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Attachment ID is empty")}};
    const QString directory = directoryFor(id);
    QFile metaFile(directory + QStringLiteral("/metadata.json"));
    if (!metaFile.open(QIODevice::ReadOnly)) return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Attachment was not found")}};
    if (metaFile.size() > 4096) return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Attachment metadata is invalid")}};
    const QJsonDocument metaDoc = QJsonDocument::fromJson(metaFile.readAll());
    const QJsonObject meta = metaDoc.object();
    if (!metaDoc.isObject() || meta.value(QStringLiteral("schema")).toString() != QLatin1String(kSchema)
        || meta.value(QStringLiteral("id")).toString() != id) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Attachment identity metadata is invalid")}};
    }
    QFile payload(directory + QStringLiteral("/payload.bin"));
    if (!payload.open(QIODevice::ReadOnly) || payload.size() < 1 || payload.size() > kMaximumBytes
        || payload.size() != meta.value(QStringLiteral("size")).toInteger()) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("Attachment bytes are missing or invalid")}};
    }
    const QByteArray bytes = payload.read(kMaximumBytes + 1);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("id"), id},
            {QStringLiteral("name"), meta.value(QStringLiteral("name")).toString()},
            {QStringLiteral("mimeType"), meta.value(QStringLiteral("mimeType")).toString()},
            {QStringLiteral("size"), bytes.size()}, {QStringLiteral("bytes"), bytes},
            {QStringLiteral("base64"), QString::fromLatin1(bytes.toBase64())}};
}

QVariantMap LocalAttachmentStore::remove(const QString &attachmentId)
{
    const QString id = attachmentId.trimmed();
    if (id.isEmpty()) { const QString error = QStringLiteral("Attachment ID is empty"); setStatus(QStringLiteral("error"), error); return result(false, error); }
    const QString directory = directoryFor(id);
    const QVariantMap existing = read(id);
    if (!existing.value(QStringLiteral("ok")).toBool()) {
        const QString error = existing.value(QStringLiteral("error")).toString(); setStatus(QStringLiteral("error"), error); return result(false, error);
    }
    if (!QDir(directory).removeRecursively()) {
        const QString error = QStringLiteral("Cannot remove attachment bytes"); setStatus(QStringLiteral("error"), error); return result(false, error);
    }
    setStatus(QStringLiteral("removed"));
    QVariantMap value = result(true); value.insert(QStringLiteral("id"), id); return value;
}

} // namespace QindaTK
