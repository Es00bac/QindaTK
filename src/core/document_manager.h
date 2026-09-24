// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "document_session.h"
#include "recovery_manager.h"
#include <QHash>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Owns sessions of any registered document kind. Opening, restoring and
// adopting go through the codec registry: bytes are recognized by content,
// decoded with identity/bounds validation, then adopted on the GUI thread.
class DocumentManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY changed)
    Q_PROPERTY(QindaTK::DocumentSession *activeSession READ activeSession NOTIFY changed)
    Q_PROPERTY(QindaTK::UndoService *undoService READ undoService CONSTANT)
    Q_PROPERTY(QindaTK::RecoveryManager *recoveryManager READ recoveryManager CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(QStringList viewOrder READ orderedViewIds NOTIFY changed)
    Q_PROPERTY(QVariantList documentTypes READ documentTypes NOTIFY changed)
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
    std::shared_ptr<DocumentCodecRegistry> codecRegistry() const { return m_codecs; }
    // Only before the first session; the recovery manager follows it.
    bool setCodecRegistry(std::shared_ptr<DocumentCodecRegistry> registry);
    QVariantList documentTypes() const;
    DocumentSession *create(std::unique_ptr<PropertyDocument> document);
    // Any registered model; its schema selects the codec.
    DocumentSession *create(std::unique_ptr<DocumentModel> model);
    // Decodes native bytes of any registered schema into a new unsaved session.
    DocumentSession *adoptBytes(const QByteArray &bytes, OperationError *error = nullptr);
    Q_INVOKABLE DocumentSession *createReferenceDocument();
    Q_INVOKABLE DocumentSession *createDocument(const QString &schemaId);
    Q_INVOKABLE DocumentSession *openReferenceDocument(const QString &path);
    // Opens any registered kind; a nonempty schemaId must match the file.
    Q_INVOKABLE DocumentSession *openDocument(const QString &path, const QString &schemaId = {});
    Q_INVOKABLE DocumentSession *restoreRecovered(const QString &documentId,
                                                  const QString &originalPath = {});
    Q_INVOKABLE DocumentSession *findSession(const QString &documentId) const;
    Q_INVOKABLE bool activate(const QString &documentId);
    Q_INVOKABLE QString duplicateView(const QString &documentId);
    Q_INVOKABLE QStringList viewsFor(const QString &documentId) const;
    QStringList orderedViewIds() const { return m_viewOrder; }
    // Move before another stable view ID; an empty target appends at the end.
    Q_INVOKABLE bool moveView(const QString &viewId, const QString &beforeViewId = {});
    // Removes a non-last stable view identity without closing the document.
    Q_INVOKABLE bool removeView(const QString &documentId, const QString &viewId);
    // Restore stable view identities from a validated workspace snapshot.
    // Document content and dirty state are unaffected.
    bool setViewIds(const QString &documentId, const QStringList &viewIds);
    bool setViewOrder(const QStringList &viewIds);
    Q_INVOKABLE bool closeDocument(const QString &documentId, ClosePolicy policy);
    Q_INVOKABLE bool requestSaveAll();
signals:
    void changed();
    void closeBlocked(const QString &documentId, const QString &reason);
    void saveAllFinished(const QVariantList &results);
private:
    static QString canonicalPath(const QString &path);
    DocumentSession *adopt(std::unique_ptr<DocumentModel> model, DocumentCodecPtr codec);
    DocumentSession *failWith(const OperationError &error);
    bool remove(DocumentSession *session);
    void finishSaveAllIfDone();
    QList<DocumentSession *> m_sessions;
    QPointer<DocumentSession> m_active;
    UndoService *m_undo = nullptr;
    RecoveryManager *m_recovery = nullptr;
    DocumentStorePtr m_store;
    std::shared_ptr<DocumentCodecRegistry> m_codecs = DocumentCodecRegistry::shared();
    QHash<QString, QString> m_views; // view UUID -> document UUID
    QStringList m_viewOrder;
    QString m_lastError;
    QVariantList m_saveAllResults;
    int m_saveAllPending = 0;
    bool m_saveAllRunning = false;
};

} // namespace QindaTK
