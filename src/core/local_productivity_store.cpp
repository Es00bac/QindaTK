// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_productivity_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSet>
#include <algorithm>
#include <utility>

namespace QindaTK {
namespace {
constexpr auto kSchema = "qindatk.productivity/1";
constexpr qsizetype kMaxBytes = 16 * 1024 * 1024;
constexpr int kUndoLimit = 100;

QVariantList listFrom(const QVariantMap &snapshot, const QString &name)
{
    return snapshot.value(name).toList();
}
}

LocalProductivityStore::LocalProductivityStore(QObject *parent) : QObject(parent) {}

QString LocalProductivityStore::defaultPath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/productivity.json");
}

void LocalProductivityStore::setFilePath(const QString &path)
{
    if (m_filePath == path) return;
    m_filePath = path;
    emit filePathChanged();
}

QString LocalProductivityStore::revisionText() const { return QString::number(m_revision); }

QVariantList *LocalProductivityStore::collection(const QString &name)
{
    if (name == QLatin1String("messages")) return &m_messages;
    if (name == QLatin1String("drafts")) return &m_drafts;
    if (name == QLatin1String("events")) return &m_events;
    if (name == QLatin1String("tasks")) return &m_tasks;
    return nullptr;
}

const QVariantList *LocalProductivityStore::collection(const QString &name) const
{
    return const_cast<LocalProductivityStore *>(this)->collection(name);
}

QString LocalProductivityStore::idOf(const QVariantMap &record)
{
    return record.value(QStringLiteral("id")).toString().trimmed();
}

QVariantMap LocalProductivityStore::result(bool ok, const QString &error) const
{
    return {{QStringLiteral("ok"), ok}, {QStringLiteral("revision"), revisionText()},
            {QStringLiteral("error"), error}};
}

void LocalProductivityStore::setStatus(const QString &status, const QString &error)
{
    if (m_status == status && m_lastError == error) return;
    m_status = status;
    m_lastError = error;
    emit statusChanged();
}

QVariantMap LocalProductivityStore::exportSnapshot() const
{
    return {{QStringLiteral("schema"), QString::fromLatin1(kSchema)},
            {QStringLiteral("revision"), revisionText()},
            {QStringLiteral("messages"), m_messages}, {QStringLiteral("drafts"), m_drafts},
            {QStringLiteral("events"), m_events}, {QStringLiteral("tasks"), m_tasks}};
}

bool LocalProductivityStore::validateSnapshot(const QVariantMap &snapshot, QString *error)
{
    if (snapshot.value(QStringLiteral("schema")).toString() != QLatin1String(kSchema)) {
        *error = QStringLiteral("Unsupported productivity-store schema");
        return false;
    }
    for (const QString &name : {QStringLiteral("messages"), QStringLiteral("drafts"),
                                QStringLiteral("events"), QStringLiteral("tasks")}) {
        if (snapshot.value(name).metaType().id() != QMetaType::QVariantList) {
            *error = QStringLiteral("Collection %1 is not a list").arg(name);
            return false;
        }
        QSet<QString> ids;
        for (const QVariant &value : snapshot.value(name).toList()) {
            const QString id = idOf(value.toMap());
            if (id.isEmpty() || ids.contains(id)) {
                *error = QStringLiteral("Collection %1 has an empty or duplicate ID").arg(name);
                return false;
            }
            ids.insert(id);
        }
    }
    bool revisionOk = false;
    snapshot.value(QStringLiteral("revision")).toString().toULongLong(&revisionOk);
    if (!revisionOk) {
        *error = QStringLiteral("Invalid productivity-store revision");
        return false;
    }
    return true;
}

QVariantMap LocalProductivityStore::readSnapshot(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return {};
    }
    if (file.size() < 1 || file.size() > kMaxBytes) {
        *error = QStringLiteral("Productivity store is empty or exceeds 16 MiB");
        return {};
    }
    const auto json = QJsonDocument::fromJson(file.readAll());
    if (!json.isObject()) {
        *error = QStringLiteral("Productivity store is not a JSON object");
        return {};
    }
    const QVariantMap snapshot = json.object().toVariantMap();
    if (!validateSnapshot(snapshot, error)) return {};
    return snapshot;
}

bool LocalProductivityStore::writeAtomic(const QString &path, const QByteArray &bytes, QString *error)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        *error = QStringLiteral("Cannot create productivity-store directory");
        return false;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    return true;
}

bool LocalProductivityStore::load() { return loadFrom(m_filePath); }

bool LocalProductivityStore::loadFrom(const QString &path)
{
    if (path.trimmed().isEmpty()) { setStatus(QStringLiteral("error"), QStringLiteral("Store path is empty")); return false; }
    QString primaryError;
    QVariantMap snapshot = readSnapshot(path, &primaryError);
    bool recovered = false;
    m_recoveryAvailable = QFileInfo::exists(path + QStringLiteral(".recovery"));
    if (snapshot.isEmpty() && m_recoveryAvailable) {
        QString recoveryError;
        snapshot = readSnapshot(path + QStringLiteral(".recovery"), &recoveryError);
        if (!snapshot.isEmpty()) recovered = true;
        else primaryError += QStringLiteral("; recovery copy: ") + recoveryError;
    }
    if (snapshot.isEmpty()) {
        setStatus(QStringLiteral("error"), primaryError);
        return false;
    }
    setFilePath(path);
    m_messages = listFrom(snapshot, QStringLiteral("messages"));
    m_drafts = listFrom(snapshot, QStringLiteral("drafts"));
    m_events = listFrom(snapshot, QStringLiteral("events"));
    m_tasks = listFrom(snapshot, QStringLiteral("tasks"));
    m_revision = snapshot.value(QStringLiteral("revision")).toString().toULongLong();
    m_taskUndo.clear();
    emit dataChanged();
    emit taskHistoryChanged();
    setStatus(recovered ? QStringLiteral("recovered") : QStringLiteral("loaded"),
              recovered ? primaryError : QString());
    return true;
}

bool LocalProductivityStore::save() { return saveTo(m_filePath); }

bool LocalProductivityStore::saveTo(const QString &path)
{
    if (path.trimmed().isEmpty()) { setStatus(QStringLiteral("error"), QStringLiteral("Store path is empty")); return false; }
    QVariantMap snapshot = exportSnapshot();
    snapshot.insert(QStringLiteral("revision"), QString::number(m_revision + 1));
    QString error;
    if (!validateSnapshot(snapshot, &error)) { setStatus(QStringLiteral("error"), error); return false; }
    const QByteArray bytes = QJsonDocument::fromVariant(snapshot).toJson(QJsonDocument::Compact);
    if (QFileInfo::exists(path)) {
        QString oldError;
        const auto old = readSnapshot(path, &oldError);
        const QByteArray recoveryBytes = old.isEmpty()
            ? QJsonDocument::fromVariant(exportSnapshot()).toJson(QJsonDocument::Compact)
            : QJsonDocument::fromVariant(old).toJson(QJsonDocument::Compact);
        if (!writeAtomic(path + QStringLiteral(".recovery"), recoveryBytes, &error)) {
            setStatus(QStringLiteral("error"), error.isEmpty() ? oldError : error);
            return false;
        }
    }
    if (!writeAtomic(path, bytes, &error)) { setStatus(QStringLiteral("error"), error); return false; }
    setFilePath(path);
    ++m_revision;
    m_recoveryAvailable = QFileInfo::exists(path + QStringLiteral(".recovery"));
    emit dataChanged();
    setStatus(QStringLiteral("saved"));
    return true;
}

QVariantMap LocalProductivityStore::upsert(const QString &name, const QVariantMap &record)
{
    auto *rows = collection(name);
    const QString id = idOf(record);
    if (!rows || id.isEmpty()) { const QString error = QStringLiteral("Unknown collection or empty stable ID"); setStatus(QStringLiteral("error"), error); return result(false, error); }
    for (int i = 0; i < rows->size(); ++i) {
        if (idOf(rows->at(i).toMap()) == id) { (*rows)[i] = record; ++m_revision; emit dataChanged(); setStatus(QStringLiteral("modified")); return result(true); }
    }
    rows->append(record);
    ++m_revision;
    emit dataChanged();
    setStatus(QStringLiteral("modified"));
    return result(true);
}

QVariantMap LocalProductivityStore::remove(const QString &name, const QString &recordId)
{
    auto *rows = collection(name);
    if (!rows || recordId.trimmed().isEmpty()) { const QString error = QStringLiteral("Unknown collection or empty stable ID"); setStatus(QStringLiteral("error"), error); return result(false, error); }
    for (qsizetype i = 0; i < rows->size(); ++i) {
        if (idOf(rows->at(i).toMap()) == recordId) { rows->removeAt(i); ++m_revision; emit dataChanged(); setStatus(QStringLiteral("modified")); return result(true); }
    }
    const QString error = QStringLiteral("Record %1 was not found").arg(recordId);
    setStatus(QStringLiteral("error"), error);
    return result(false, error);
}

void LocalProductivityStore::pushTaskUndo()
{
    m_taskUndo.append(m_tasks);
    if (m_taskUndo.size() > kUndoLimit) m_taskUndo.removeFirst();
}

QVariantMap LocalProductivityStore::moveTask(const QString &taskId, const QString &toStatus, int targetOrder)
{
    if (toStatus.trimmed().isEmpty()) { const QString error = QStringLiteral("Target task status is empty"); setStatus(QStringLiteral("error"), error); return result(false, error); }
    int taskIndex = -1;
    for (int i = 0; i < m_tasks.size(); ++i) if (idOf(m_tasks[i].toMap()) == taskId) { taskIndex = i; break; }
    if (taskIndex < 0) { const QString error = QStringLiteral("Task %1 was not found").arg(taskId); setStatus(QStringLiteral("error"), error); return result(false, error); }
    pushTaskUndo();
    QVariantMap task = m_tasks[taskIndex].toMap();
    task.insert(QStringLiteral("status"), toStatus);
    QList<QVariantMap> peers;
    for (const QVariant &value : std::as_const(m_tasks)) {
        QVariantMap row = value.toMap();
        if (idOf(row) != taskId && row.value(QStringLiteral("status")).toString() == toStatus) peers.append(row);
    }
    std::sort(peers.begin(), peers.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("order")).toInt() < b.value(QStringLiteral("order")).toInt();
    });
    const int peerCount = static_cast<int>(peers.size());
    const int insertAt = targetOrder < 0 ? peerCount : std::clamp(targetOrder, 0, peerCount);
    peers.insert(insertAt, task);
    for (int i = 0; i < peers.size(); ++i) peers[i].insert(QStringLiteral("order"), i);
    QVariantList next;
    for (const QVariant &value : std::as_const(m_tasks)) {
        if (idOf(value.toMap()) == taskId || value.toMap().value(QStringLiteral("status")).toString() == toStatus) continue;
        next.append(value);
    }
    for (const QVariantMap &peer : peers) next.append(peer);
    QMap<QString, QList<QVariantMap>> grouped;
    for (const QVariant &value : std::as_const(next)) {
        const QVariantMap row = value.toMap();
        grouped[row.value(QStringLiteral("status")).toString()].append(row);
    }
    QVariantList normalized;
    for (auto it = grouped.begin(); it != grouped.end(); ++it) {
        auto &rows = it.value();
        std::sort(rows.begin(), rows.end(), [](const QVariantMap &a, const QVariantMap &b) {
            const int left = a.value(QStringLiteral("order")).toInt();
            const int right = b.value(QStringLiteral("order")).toInt();
            return left == right ? idOf(a) < idOf(b) : left < right;
        });
        for (int i = 0; i < rows.size(); ++i) {
            rows[i].insert(QStringLiteral("order"), i);
            normalized.append(rows[i]);
        }
    }
    m_tasks = normalized;
    ++m_revision;
    emit dataChanged();
    emit taskHistoryChanged();
    setStatus(QStringLiteral("modified"));
    return result(true);
}

QVariantMap LocalProductivityStore::undoTaskMove()
{
    if (m_taskUndo.isEmpty()) { const QString error = QStringLiteral("No task move to undo"); setStatus(QStringLiteral("error"), error); return result(false, error); }
    m_tasks = m_taskUndo.takeLast();
    ++m_revision;
    emit dataChanged();
    emit taskHistoryChanged();
    setStatus(QStringLiteral("modified"));
    return result(true);
}

} // namespace QindaTK
