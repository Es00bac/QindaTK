// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"

#include <QObject>
#include <QHash>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Maps stable logical resource identities to external or embedded resources.
// Document commands own reference edits; this resolver only changes locations.
class ResourceResolver : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList resources READ resources NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    enum class Availability { Missing, Ready, Modified };
    Q_ENUM(Availability)
    enum class Origin { External, Embedded };
    Q_ENUM(Origin)

    explicit ResourceResolver(QObject *parent = nullptr);

    QVariantList resources() const;
    QString lastError() const { return m_error.message; }

    // expectedSha256 may be empty when no baseline is known. External resources
    // are inspected immediately; embedded resources use the supplied hash.
    Q_INVOKABLE bool registerResource(const QString &resourceId,
                                      const QString &uri,
                                      const QByteArray &expectedSha256 = {},
                                      const QString &origin = QStringLiteral("external"));
    // Cached lookup never stats or hashes external files; suitable for hot UI paths.
    Q_INVOKABLE QVariantMap lookup(const QString &resourceId);
    Q_INVOKABLE QVariantMap resolve(const QString &resourceId);
    Q_INVOKABLE bool refresh(const QString &resourceId);
    // Relinking validates the destination before changing the ID-to-URI map.
    Q_INVOKABLE bool relink(const QString &resourceId, const QString &newUri);
    // Returns exact content-hash matches only. Results are proposals; no mapping
    // changes until relink() is called explicitly.
    Q_INVOKABLE QVariantList proposeRelinks(const QStringList &resourceIds,
                                            const QStringList &candidateUris);
    Q_INVOKABLE bool removeResource(const QString &resourceId);

signals:
    void changed();
    void resourceChanged(const QString &resourceId);
    void relinked(const QString &resourceId, const QString &oldUri,
                  const QString &newUri);

private:
    struct Entry {
        QString id;
        QString uri;
        QByteArray expectedHash;
        QByteArray currentHash;
        Availability availability = Availability::Missing;
        Origin origin = Origin::External;
        quint64 revision = 0;
    };

    static bool validId(const QString &id);
    static bool validHash(const QByteArray &hash);
    static QString availabilityName(Availability availability);
    static QString originName(Origin origin);
    static OperationError inspectExternal(const QString &uri, QByteArray *hash,
                                          bool *exists);
    static bool localPath(const QString &uri, QString *path);
    QVariantMap entryMap(const Entry &entry) const;
    bool refreshEntry(Entry *entry, bool *changed);
    bool fail(ErrorCode code, const QString &message, const QString &field = {});
    void clearError();

    QHash<QString, Entry> m_entries;
    OperationError m_error;
};

} // namespace QindaTK
