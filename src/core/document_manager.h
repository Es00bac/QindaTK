// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "document_session.h"
#include "recovery_manager.h"
#include <QHash>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

class DocumentManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY changed)
    Q_PROPERTY(QindaTK::DocumentSession *activeSession READ activeSession NOTIFY changed)
    Q_PROPERTY(QindaTK::UndoService *undoService READ undoService CONSTANT)
    Q_PROPERTY(QindaTK::RecoveryManager *recoveryManager READ recoveryManager CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
public:
    enum class ClosePolicy { Cancel, Discard, Save };
    Q_ENUM(ClosePolicy)
    explicit DocumentManager(QObject *parent = nullptr);
    QVariantList sessions() const;
    DocumentSession *activeSession() const { return m_active; }
    UndoService *undoService() const { return m_undo; }
    RecoveryManager *recoveryManager() const { return m_recovery; }
    QString lastError() const { return m_lastError; }
    void setStore(DocumentStorePtr store);
    DocumentSession *create(std::unique_ptr<PropertyDocument> document);
    Q_INVOKABLE DocumentSession *createReferenceDocument();
    Q_INVOKABLE DocumentSession *openReferenceDocument(const QString &path);
    Q_INVOKABLE DocumentSession *restoreRecovered(const QString &documentId,
                                                  const QString &originalPath = {});
    Q_INVOKABLE DocumentSession *findSession(const QString &documentId) const;
    Q_INVOKABLE bool activate(const QString &documentId);
    Q_INVOKABLE QString duplicateView(const QString &documentId);
    Q_INVOKABLE QStringList viewsFor(const QString &documentId) const;
    Q_INVOKABLE bool closeDocument(const QString &documentId, ClosePolicy policy);
    Q_INVOKABLE bool requestSaveAll();
signals:
    void changed();
    void closeBlocked(const QString &documentId, const QString &reason);
    void saveAllFinished(const QVariantList &results);
private:
    static QString canonicalPath(const QString &path);
    DocumentSession *adopt(std::unique_ptr<PropertyDocument> document);
    bool remove(DocumentSession *session);
    void finishSaveAllIfDone();
    QList<DocumentSession *> m_sessions;
    QPointer<DocumentSession> m_active;
    UndoService *m_undo = nullptr;
    RecoveryManager *m_recovery = nullptr;
    DocumentStorePtr m_store;
    QHash<QString, QString> m_views; // view UUID -> document UUID
    QString m_lastError;
    QVariantList m_saveAllResults;
    int m_saveAllPending = 0;
    bool m_saveAllRunning = false;
};

} // namespace QindaTK
