// SPDX-License-Identifier: LGPL-3.0-or-later
#include "agent_provider.h"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QUuid>
namespace QindaTK {
AgentProvider::AgentProvider(QObject *parent) : QObject(parent),
    m_id(QUuid::createUuid().toString(QUuid::WithoutBraces)) {}
QString AgentProvider::revision() const
{
    // AGENT-GUARD: content generation prevents an edit/undo ABA from accepting
    // an old request. The state hash also detects selection/view/draft changes.
    return QString::number(m_generation) + QLatin1Char(':') + QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(state()).toJson(QJsonDocument::Compact),
                                 QCryptographicHash::Sha256).toHex());
}
void AgentProvider::touch() { ++m_generation; emit changed(); }
}
