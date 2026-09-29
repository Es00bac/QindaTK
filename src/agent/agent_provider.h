// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

namespace QindaTK {
// GUI/owner-thread only, borrowed by AgentHost. IDs live for the provider's
// lifetime, not a filename or delegate index. Destruction withdraws the scope.
class AgentProvider : public QObject {
    Q_OBJECT
public:
    explicit AgentProvider(QObject *parent = nullptr);
    QString id() const { return m_id; }
    // No QObject, credentials, or implementation pointers in these copies.
    virtual QJsonObject state() const = 0;
    // {id, description, inputSchema, enabled, disabled_reason}; mutations only.
    virtual QJsonArray actions() const = 0;
    // Return {status, message, changed, result, undo_ref?}. Report partial
    // application honestly. Host validation runs before this editing seam.
    virtual QJsonObject execute(const QString &action, const QJsonObject &arguments) = 0;
    QString revision() const;
    quint64 generation() const { return m_generation; }
public slots:
    // Connect every document mutation/load/undo to this, including human edits.
    void touch();
signals:
    void changed();
private:
    QString m_id;
    quint64 m_generation = 1;
};
}
