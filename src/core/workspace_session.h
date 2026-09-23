// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "document_manager.h"
#include "dock_model.h"
#include "state_store.h"
#include <QJsonObject>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Restorable window/view topology. Documents retain their own independent
// persistence and DockModel remains the sole panel-arrangement authority.
class WorkspaceSession : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY changed)
    Q_PROPERTY(QVariantList windows READ windows NOTIFY changed)
    Q_PROPERTY(QVariantList views READ views NOTIFY changed)
    Q_PROPERTY(QVariantList documents READ documents NOTIFY changed)
    Q_PROPERTY(QString activeWindowId READ activeWindowId NOTIFY changed)
    Q_PROPERTY(QString activeDocumentId READ activeDocumentId NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(QStringList unavailableDocuments READ unavailableDocuments NOTIFY changed)
public:
    explicit WorkspaceSession(QObject *parent = nullptr);
    QString path() const { return m_path; }
    void setPath(const QString &path);
    QVariantList windows() const;
    QVariantList views() const;
    QVariantList documents() const;
    QString activeWindowId() const;
    QString activeDocumentId() const;
    QString lastError() const { return m_lastError; }
    QStringList unavailableDocuments() const { return m_unavailableDocuments; }
    void setStore(StateStorePtr store);
    // Screen entries: {id, availableRect:{x,y,width,height}}. An empty list
    // uses QGuiApplication screens; tests can inject a deterministic list.
    Q_INVOKABLE bool setState(const QVariantMap &state, const QVariantList &screens = {});
    Q_INVOKABLE QVariantMap state() const;
    Q_INVOKABLE bool capture(DocumentManager *documents, DockModel *dock,
                             const QVariantList &windows, const QVariantList &views,
                             const QString &activeWindowId);
    Q_INVOKABLE bool restore(DocumentManager *documents, DockModel *dock);
    Q_INVOKABLE bool load(const QVariantList &screens = {});
    Q_INVOKABLE bool save();
    Q_INVOKABLE void reset();
    static QByteArray encode(const QVariantMap &state, OperationError *error = nullptr);
    static std::optional<QVariantMap> decode(const QByteArray &bytes,
                                             OperationError *error = nullptr);
signals:
    void changed();
private:
    static std::optional<QVariantMap> normalize(const QVariantMap &state,
                                                 const QVariantList &screens,
                                                 OperationError *error);
    void fail(const OperationError &error);
    QVariantMap m_state;
    QString m_path;
    QString m_lastError;
    QStringList m_unavailableDocuments;
    StateStorePtr m_store;
};

} // namespace QindaTK
