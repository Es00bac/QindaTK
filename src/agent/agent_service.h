// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QMetaMethod>
#include <QPointer>
#include <QVariant>

namespace QindaTK {

// An explicit allowlist over an application's existing QObject services.
// Owner-thread only; targets are borrowed and may be destroyed independently.
// No inherited methods, properties, signals, slots or QObject pointers are
// exported. Registering a method is a deliberate public API decision.
class AgentServiceCatalog {
public:
  struct CallResult {
    bool ok = false;
    QVariant value;
    QString error;
  };
  bool add(QObject *target, const QString &id, const QByteArray &methodName,
           const QString &description, bool readOnly,
           const QJsonObject &parameterSchemas = {}, QString *error = nullptr);
  QJsonArray actions() const;
  CallResult invoke(const QString &id, const QJsonObject &arguments) const;
  bool contains(const QString &id) const;
  bool isReadOnly(const QString &id) const;

private:
  struct Method {
    QPointer<QObject> target;
    QString id, description;
    QMetaMethod method;
    QList<QMetaMethod> overloads;
    QJsonObject schema;
    bool readOnly = false;
  };
  QList<Method> m_methods;
};
} // namespace QindaTK
