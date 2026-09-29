// SPDX-License-Identifier: LGPL-3.0-or-later
#include "agent_host.h"
#include "agent_schema.h"
#include <QJsonDocument>
#include <QThread>
namespace QindaTK {
namespace {
QJsonObject failure(const QString &status, const QString &message) {
  return {{QStringLiteral("status"), status},
          {QStringLiteral("message"), message}};
}
} // namespace
bool AgentHost::addProvider(AgentProvider *p, Grant access) {
  if (!p || p->thread() != thread() || m_providers.contains(p->id()))
    return false;
  m_providers.insert(p->id(), {p, access});
  connect(p, &AgentProvider::changed, this, [this, id = p->id()] {
    publish(id, QStringLiteral("state_changed"));
  });
  connect(p, &QObject::destroyed, this, [this, id = p->id()] {
    m_providers.remove(id);
    publish(id, QStringLiteral("closed"));
  });
  publish(p->id(), QStringLiteral("opened"));
  return true;
}
void AgentHost::setGrant(const QString &id, Grant access) {
  if (!m_providers.contains(id))
    return;
  m_providers[id].grant = access;
  publish(id, QStringLiteral("access_changed"));
}
AgentHost::Grant AgentHost::grant(const QString &id) const {
  return m_providers.value(id).grant;
}
void AgentHost::publish(const QString &id, const QString &kind) {
  const QJsonObject e{
      {QStringLiteral("sequence"), QString::number(++m_sequence)},
      {QStringLiteral("document_id"), id},
      {QStringLiteral("kind"), kind}};
  m_events.append(e);
  while (m_events.size() > 256)
    m_events.removeFirst();
  emit event(e);
}
QJsonObject AgentHost::inspect(AgentProvider *p) const {
  QJsonObject s = p->state();
  s.insert(QStringLiteral("document_id"), p->id());
  s.insert(QStringLiteral("revision"), p->revision());
  s.insert(QStringLiteral("access"), grant(p->id()) == Grant::Edit
                                         ? QStringLiteral("edit")
                                         : QStringLiteral("read"));
  return s;
}
QJsonObject AgentHost::request(const QString &method, const QJsonObject &args) {
  Q_ASSERT(QThread::currentThread() == thread());
  if (method == QLatin1String("hello"))
    return {{QStringLiteral("protocol"), QStringLiteral("qindatk-agent/1")},
            {QStringLiteral("methods"),
             QJsonArray{QStringLiteral("documents.list"),
                        QStringLiteral("document.inspect"),
                        QStringLiteral("actions.list"),
                        QStringLiteral("action.invoke"),
                        QStringLiteral("events.poll")}}};
  if (method == QLatin1String("documents.list")) {
    QJsonArray documents;
    for (const auto &e : m_providers)
      if (e.provider && e.grant != Grant::Off) {
        const auto s = inspect(e.provider);
        documents.append(QJsonObject{
            {QStringLiteral("document_id"), e.provider->id()},
            {QStringLiteral("title"), s.value(QStringLiteral("title"))},
            {QStringLiteral("type"), s.value(QStringLiteral("type"))},
            {QStringLiteral("access"), s.value(QStringLiteral("access"))}});
      }
    return {{QStringLiteral("documents"), documents}};
  }
  if (method == QLatin1String("events.poll")) {
    bool ok = false;
    const quint64 after = args.value(QStringLiteral("after"))
                              .toString(QStringLiteral("0"))
                              .toULongLong(&ok);
    if (!ok)
      return failure(QStringLiteral("invalid_request"),
                     QStringLiteral("after must be a decimal sequence string"));
    QJsonArray events;
    for (const auto value : m_events) {
      const auto e = value.toObject();
      if (e.value(QStringLiteral("sequence")).toString().toULongLong() >
              after &&
          grant(e.value(QStringLiteral("document_id")).toString()) !=
              Grant::Off)
        events.append(e);
    }
    return {{QStringLiteral("events"), events},
            {QStringLiteral("sequence"), QString::number(m_sequence)},
            {QStringLiteral("refresh_required"),
             after > m_sequence ||
                 (!m_events.isEmpty() &&
                  after + 1 < m_events.first()
                                  .toObject()
                                  .value(QStringLiteral("sequence"))
                                  .toString()
                                  .toULongLong())}};
  }
  const QString id = args.value(QStringLiteral("document_id")).toString();
  const auto e = m_providers.value(id);
  if (!e.provider || e.grant == Grant::Off)
    return failure(
        QStringLiteral("access_denied"),
        QStringLiteral("Document is closed or access is not granted"));
  if (method == QLatin1String("document.inspect"))
    return inspect(e.provider);
  if (method == QLatin1String("actions.list")) {
    QJsonArray actions = e.provider->actions();
    for (qsizetype i = 0; i < actions.size(); ++i) {
      auto a = actions.at(i).toObject();
      if (e.grant != Grant::Edit) {
        a.insert(QStringLiteral("enabled"), false);
        a.insert(QStringLiteral("disabled_reason"),
                 QStringLiteral("Read-only document grant"));
      }
      actions[i] = a;
    }
    return {{QStringLiteral("actions"), actions},
            {QStringLiteral("revision"), e.provider->revision()}};
  }
  if (method != QLatin1String("action.invoke"))
    return failure(QStringLiteral("unknown_method"), method);
  if (e.grant != Grant::Edit)
    return failure(QStringLiteral("read_only"),
                   QStringLiteral("Editing this document is not granted"));
  const QString requestId = args.value(QStringLiteral("request_id")).toString();
  if (requestId.isEmpty() || requestId.size() > 128)
    return failure(QStringLiteral("invalid_request"),
                   QStringLiteral("request_id must contain 1–128 characters"));
  const QString key = id + QLatin1Char('/') + requestId;
  if (m_receipts.contains(key)) {
    const auto cached = m_receipts.value(key);
    if (cached.request != args)
      return failure(
          QStringLiteral("request_id_conflict"),
          QStringLiteral("Request ID was used with different inputs"));
    auto receipt = cached.receipt;
    receipt.insert(QStringLiteral("replayed"), true);
    receipt.insert(QStringLiteral("current_revision"), e.provider->revision());
    return receipt;
  }
  if (m_receipts.size() >= 1024)
    return failure(QStringLiteral("session_limit"),
                   QStringLiteral("Session write receipt limit reached; start "
                                  "a new application session"));
  if (args.value(QStringLiteral("if_revision")).toString() !=
      e.provider->revision())
    return failure(
        QStringLiteral("stale_revision"),
        QStringLiteral("Refresh the document snapshot before changing it"));
  if (!args.value(QStringLiteral("arguments")).isObject())
    return failure(QStringLiteral("invalid_request"),
                   QStringLiteral("arguments must be an object"));
  const QString action = args.value(QStringLiteral("action")).toString();
  QJsonObject descriptor;
  for (const auto value : e.provider->actions())
    if (value.toObject().value(QStringLiteral("id")).toString() == action)
      descriptor = value.toObject();
  if (descriptor.isEmpty())
    return failure(QStringLiteral("unknown_action"), action);
  if (!descriptor.value(QStringLiteral("enabled")).toBool())
    return failure(
        QStringLiteral("disabled"),
        descriptor.value(QStringLiteral("disabled_reason")).toString());
  if (!descriptor.value(QStringLiteral("inputSchema")).isObject())
    return failure(QStringLiteral("invalid_schema"),
                   QStringLiteral("Action has no input schema"));
  const QString error = validateAgentJson(
      args.value(QStringLiteral("arguments")),
      descriptor.value(QStringLiteral("inputSchema")).toObject());
  if (!error.isEmpty())
    return failure(QStringLiteral("validation_failed"), error);
  auto receipt = e.provider->execute(
      action, args.value(QStringLiteral("arguments")).toObject());
  receipt.insert(QStringLiteral("request_id"), requestId);
  receipt.insert(QStringLiteral("document_id"), id);
  // A semantic action may synchronously close its document. The borrowed
  // QPointer is then null; retain the receipt without reading a dead object.
  receipt.insert(QStringLiteral("revision"),
                 e.provider ? QJsonValue(e.provider->revision())
                            : QJsonValue(QJsonValue::Null));
  if (!e.provider)
    receipt.insert(QStringLiteral("document_closed"), true);
  m_receipts.insert(key, {args, receipt});
  publish(id, QStringLiteral("action_finished"));
  return receipt;
}
} // namespace QindaTK
