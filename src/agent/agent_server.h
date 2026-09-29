// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "agent_host.h"
#include <QLocalServer>
namespace QindaTK {
// Local user-owned socket, newline JSON-RPC 2.0, owner thread. No TCP listener.
class AgentServer : public QObject {
  Q_OBJECT
public:
  explicit AgentServer(AgentHost *host, QObject *parent = nullptr);
  bool listen(const QString &path, QString *error = nullptr);
  QString path() const { return m_server.fullServerName(); }
  static QString runtimeDirectory();

private:
  AgentHost *m_host;
  QLocalServer m_server;
  int m_connections = 0;
};
} // namespace QindaTK
