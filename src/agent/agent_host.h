// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "agent_provider.h"
#include <QHash>
#include <QPointer>
namespace QindaTK {
class AgentHost : public QObject {
    Q_OBJECT
public:
    enum class Grant { Off, Read, Edit };
    explicit AgentHost(QObject *parent=nullptr):QObject(parent){}
    // Same thread, borrowed. Grants are set only by application code/UI.
    bool addProvider(AgentProvider *provider, Grant grant=Grant::Off);
    void setGrant(const QString &id, Grant grant);
    Grant grant(const QString &id) const;
    // Same in-process contract as RPC. No remote method grants access.
    QJsonObject request(const QString &method,const QJsonObject &parameters={});
signals:
    // Metadata only; consumers refresh state under the CURRENT grant.
    void event(const QJsonObject &event);
private:
    struct Entry { QPointer<AgentProvider> provider; Grant grant=Grant::Off; };
    struct Cached { QJsonObject request; QJsonObject receipt; };
    void publish(const QString &id,const QString &kind);
    QJsonObject inspect(AgentProvider *provider) const;
    QHash<QString,Entry> m_providers;
    QHash<QString,Cached> m_receipts; // Bounded, never evicted/reapplied in a session.
    QJsonArray m_events;
    quint64 m_sequence=0;
};
}
