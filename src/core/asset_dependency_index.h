// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Stable-ID asset graph. Sources own their outgoing edges; a rescan replaces
// only that source's edges. Analysis tokens stop older workers overwriting a
// newer result. The host owns rebuilds and URI relinks.
class AssetDependencyIndex : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString projectRoot READ projectRoot WRITE setProjectRoot NOTIFY changed)
    Q_PROPERTY(QVariantList assets READ assets NOTIFY changed)
    Q_PROPERTY(QVariantList diagnostics READ diagnostics NOTIFY changed)
    Q_PROPERTY(int assetCount READ assetCount NOTIFY changed)
    Q_PROPERTY(int edgeCount READ edgeCount NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    explicit AssetDependencyIndex(QObject *parent = nullptr) : QObject(parent) {}
    QString projectRoot() const { return m_projectRoot; }
    void setProjectRoot(const QString &path);
    QVariantList assets() const;
    QVariantList diagnostics() const;
    int assetCount() const { return m_assets.size(); }
    int edgeCount() const { return m_edgeCount; }
    QString lastError() const { return m_error; }

    Q_INVOKABLE bool upsertAsset(const QString &id, const QString &label,
                                const QString &uri, bool available,
                                const QString &kind = QStringLiteral("asset"));
    Q_INVOKABLE bool setAvailable(const QString &id, bool available);
    Q_INVOKABLE QString beginScan(const QString &sourceId);
    Q_INVOKABLE bool commitScan(const QString &sourceId, const QString &token,
                                const QStringList &dependencies);
    Q_INVOKABLE bool scanQmlFile(const QString &sourceId);
    Q_INVOKABLE QVariantMap asset(const QString &id) const;
    Q_INVOKABLE QStringList dependencies(const QString &id) const;
    Q_INVOKABLE QStringList dependents(const QString &id) const;
    Q_INVOKABLE QStringList affectedBy(const QString &id) const;

signals:
    void changed();
    void scanRejected(const QString &sourceId, const QString &reason);

private:
    struct Asset {
        QString id;
        QString label;
        QString uri;
        QString kind;
        bool available = false;
        QSet<QString> dependencies;
        quint64 scanToken = 0;
    };
    static bool validId(const QString &id);
    bool fail(const QString &message);
    QVariantMap assetMap(const Asset &entry) const;
    QString absolutePathFor(const QString &id) const;
    QString localId(const QString &sourceId, const QString &reference) const;
    QHash<QString, Asset> m_assets;
    QString m_projectRoot;
    QString m_error;
    int m_edgeCount = 0;
};

} // namespace QindaTK
