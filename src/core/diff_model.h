// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

struct DiffDecision {
  QString requestId;
  QString hunkId;
  QString conflictId;
  QString action;
  QString leftRevision;
  QString rightRevision;
  int leftStart = 0;
  int leftCount = 0;
  QStringList replacement;
};

struct DiffRegion {
  QString kind, leftText, rightText, hunkId;
  int leftLine = 0, rightLine = 0;
  QVariant leftValue, rightValue;
  QString conflictId;
};

struct DiffHunk {
  QString id;
  int row = 0, leftStart = 0, leftCount = 0, rightStart = 0, rightCount = 0;
  QString conflictId;
};

// A host may back a large or structured comparison with indexed/paged storage.
// Only requested regions and hunks are fetched; ownership stays with the host.
class DiffPageProvider : public QObject {
  Q_OBJECT
  QML_ANONYMOUS
public:
  using QObject::QObject;
  virtual int regionCount() const = 0;
  virtual DiffRegion regionAt(int index) const = 0;
  virtual int hunkCount() const = 0;
  virtual DiffHunk hunkAt(int index) const = 0;
  virtual QStringList replacementFor(const DiffHunk &hunk,
                                     const QString &action) const = 0;
};

// AGENT-CONTRACT: the host owns source documents and the transaction. Returning
// true means the request was queued, not committed. Cancellation must prevent
// a queued operation from committing; a completed operation is reported by
// supplying new document snapshots/revisions to DiffModel.
class DiffTransactionAdapter : public QObject {
  Q_OBJECT
  QML_ANONYMOUS
public:
  using QObject::QObject;
  virtual bool submitDecision(const DiffDecision &decision) = 0;
  virtual void cancelDecision(const QString &requestId) = 0;
};

class DiffModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QString leftRevision READ leftRevision NOTIFY stateChanged)
  Q_PROPERTY(QString rightRevision READ rightRevision NOTIFY stateChanged)
  Q_PROPERTY(QVariantList hunks READ hunks NOTIFY stateChanged)
  Q_PROPERTY(int hunkCount READ hunkCount NOTIFY stateChanged)
  Q_PROPERTY(QString error READ error NOTIFY stateChanged)
  Q_PROPERTY(
      QindaTK::DiffTransactionAdapter *transactionAdapter READ
          transactionAdapter WRITE setTransactionAdapter NOTIFY stateChanged)
  Q_PROPERTY(QindaTK::DiffPageProvider *pageProvider READ pageProvider NOTIFY
                 stateChanged)
public:
  enum Role {
    KindRole = Qt::UserRole + 1,
    LeftTextRole,
    RightTextRole,
    LeftLineRole,
    RightLineRole,
    HunkIdRole,
    ConflictIdRole,
    LeftValueRole,
    RightValueRole
  };
  explicit DiffModel(QObject *parent = nullptr);
  int rowCount(const QModelIndex &parent = {}) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  QString leftRevision() const { return m_leftRevision; }
  QString rightRevision() const { return m_rightRevision; }
  QVariantList hunks() const { return m_hunks; }
  int hunkCount() const {
    return m_provider ? m_provider->hunkCount() : m_hunks.size();
  }
  QString error() const { return m_error; }
  DiffTransactionAdapter *transactionAdapter() const { return m_adapter; }
  DiffPageProvider *pageProvider() const { return m_provider; }
  void setTransactionAdapter(DiffTransactionAdapter *adapter);
  Q_INVOKABLE bool setDocuments(const QStringList &left,
                                const QStringList &right,
                                const QString &leftRevision,
                                const QString &rightRevision);
  Q_INVOKABLE bool setPageProvider(DiffPageProvider *provider,
                                   const QString &leftRevision,
                                   const QString &rightRevision);
  Q_INVOKABLE QVariantMap get(int row) const;
  Q_INVOKABLE QVariantMap hunkAt(int index) const;
  Q_INVOKABLE QString requestDecision(const QString &hunkId,
                                      const QString &action,
                                      const QString &expectedLeftRevision,
                                      const QString &expectedRightRevision);
  Q_INVOKABLE bool cancelDecision(const QString &requestId);
  Q_INVOKABLE int hunkRow(const QString &hunkId) const;
signals:
  void stateChanged();
  void decisionRequested(const QString &requestId, const QString &hunkId,
                         const QString &action);

private:
  QVector<DiffRegion> m_rows;
  QVector<DiffHunk> m_hunkData;
  QVariantList m_hunks;
  QStringList m_left, m_right;
  QString m_leftRevision, m_rightRevision, m_error;
  QPointer<DiffTransactionAdapter> m_adapter;
  QPointer<DiffPageProvider> m_provider;
  QMetaObject::Connection m_providerDestroyed;
  QHash<QString, DiffDecision> m_pending;
  quint64 m_nextRequest = 1;
  bool fail(const QString &message);
};
} // namespace QindaTK
