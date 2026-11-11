// SPDX-License-Identifier: LGPL-3.0-or-later
#include "agent_service.h"
#include "agent_schema.h"
#include <QThread>
#include <QUrl>
#include <limits>

namespace QindaTK {
namespace {
QJsonObject typeSchema(QMetaType type) {
  switch (type.id()) {
  case QMetaType::QString:
  case QMetaType::QUrl:
    return {{QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("maxLength"), 65536}};
  case QMetaType::Bool:
    return {{QStringLiteral("type"), QStringLiteral("boolean")}};
  case QMetaType::Int:
    return {{QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("minimum"), std::numeric_limits<int>::min()},
            {QStringLiteral("maximum"), std::numeric_limits<int>::max()}};
  case QMetaType::LongLong:
    // Decimal strings preserve 64-bit IDs and money in JavaScript harnesses.
    return {{QStringLiteral("anyOf"), QJsonArray{
        QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")},
                    {QStringLiteral("minimum"), -9007199254740991.0},
                    {QStringLiteral("maximum"), 9007199254740991.0}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                    {QStringLiteral("pattern"), QStringLiteral("^-?[0-9]{1,19}$")}}}}};
  case QMetaType::QVariantMap:
    return {{QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("maxProperties"), 256}};
  case QMetaType::QVariantList:
    return {{QStringLiteral("type"), QStringLiteral("array")},
            {QStringLiteral("maxItems"), 20000}};
  default:
    return {};
  }
}
bool supportedReturn(QMetaType type) {
  return type.id() == QMetaType::Void || !typeSchema(type).isEmpty();
}
} // namespace

bool AgentServiceCatalog::add(QObject *target, const QString &id,
                              const QByteArray &methodName,
                              const QString &description, bool readOnly,
                              const QJsonObject &parameterSchemas, QString *error) {
  const auto fail = [&](const QString &why) {
    if (error) *error = id + QStringLiteral(": ") + why;
    return false;
  };
  if (!target || target->thread() != QThread::currentThread() || id.isEmpty() || contains(id))
    return fail(QStringLiteral("Invalid target, thread or duplicate action ID"));
  Method entry;
  entry.target = target;
  entry.id = id;
  entry.description = description;
  entry.readOnly = readOnly;
  const auto *meta = target->metaObject();
  int shortest = 11;
  for (int i = meta->methodOffset(); i < meta->methodCount(); ++i) {
    const auto method = meta->method(i);
    if (method.name() != methodName || method.methodType() != QMetaMethod::Method ||
        method.access() != QMetaMethod::Public)
      continue;
    entry.overloads.append(method);
    shortest = qMin(shortest, method.parameterCount());
    if (!entry.method.isValid() || method.parameterCount() > entry.method.parameterCount())
      entry.method = method;
  }
  if (!entry.method.isValid() || entry.method.parameterCount() > 10 ||
      !supportedReturn(entry.method.returnMetaType()))
    return fail(QStringLiteral("Method or return type is not supported"));
  if (readOnly && !entry.method.isConst())
    return fail(QStringLiteral("Read-only service methods must be const"));
  QJsonObject properties;
  QJsonArray required;
  const auto names = entry.method.parameterNames();
  for (int i = 0; i < names.size(); ++i) {
    const QString name = QString::fromLatin1(names[i]);
    auto schema = typeSchema(entry.method.parameterMetaType(i));
    if (schema.isEmpty() || name.isEmpty())
      return fail(QStringLiteral("Unnamed or unsupported argument"));
    if (parameterSchemas.contains(name))
      schema = parameterSchemas.value(name).toObject();
    properties.insert(name, schema);
    if (i < shortest) required.append(name);
  }
  for (auto it = parameterSchemas.begin(); it != parameterSchemas.end(); ++it)
    if (!properties.contains(it.key()) || !it.value().isObject())
      return fail(QStringLiteral("Schema names an unknown parameter"));
  entry.schema = {{QStringLiteral("type"), QStringLiteral("object")},
                  {QStringLiteral("properties"), properties},
                  {QStringLiteral("required"), required},
                  {QStringLiteral("additionalProperties"), false}};
  m_methods.append(entry);
  return true;
}

bool AgentServiceCatalog::contains(const QString &id) const {
  for (const auto &entry : m_methods)
    if (entry.id == id) return true;
  return false;
}
bool AgentServiceCatalog::isReadOnly(const QString &id) const {
  for (const auto &entry : m_methods)
    if (entry.id == id) return entry.readOnly;
  return false;
}
QJsonArray AgentServiceCatalog::actions() const {
  QJsonArray result;
  for (const auto &entry : m_methods)
    result.append(QJsonObject{
        {QStringLiteral("id"), entry.id},
        {QStringLiteral("description"), entry.description},
        {QStringLiteral("inputSchema"), entry.schema},
        {QStringLiteral("readOnly"), entry.readOnly},
        {QStringLiteral("enabled"), !entry.target.isNull()},
        {QStringLiteral("disabled_reason"), entry.target ? QString() : QStringLiteral("Service closed")}});
  return result;
}

AgentServiceCatalog::CallResult AgentServiceCatalog::invoke(
    const QString &id, const QJsonObject &arguments) const {
  for (const auto &entry : m_methods) {
    if (entry.id != id) continue;
    if (!entry.target || entry.target->thread() != QThread::currentThread())
      return {false, {}, QStringLiteral("Service closed or called from the wrong thread")};
    const auto invalid = validateAgentJson(arguments, entry.schema);
    if (!invalid.isEmpty()) return {false, {}, invalid};
    const auto names = entry.method.parameterNames();
    int count = names.size();
    while (count && !arguments.contains(QString::fromLatin1(names[count - 1]))) --count;
    QMetaMethod method;
    for (const auto &candidate : entry.overloads)
      if (candidate.parameterCount() == count) method = candidate;
    if (!method.isValid()) return {false, {}, QStringLiteral("Missing required argument")};
    QVariant values[10];
    QGenericArgument args[10];
    for (int i = 0; i < count; ++i) {
      const QString name = QString::fromLatin1(names[i]);
      if (!arguments.contains(name)) return {false, {}, QStringLiteral("Missing argument: ") + name};
      const auto type = method.parameterMetaType(i);
      values[i] = arguments.value(name).toVariant();
      if (type.id() == QMetaType::LongLong && values[i].metaType().id() == QMetaType::QString) {
        bool ok = false;
        const auto number = values[i].toString().toLongLong(&ok);
        if (!ok) return {false, {}, QStringLiteral("Integer overflow: ") + name};
        values[i] = number;
      }
      if (!values[i].convert(type)) return {false, {}, QStringLiteral("Invalid argument: ") + name};
      args[i] = QGenericArgument(type.name(), values[i].constData());
    }
    QVariant result(method.returnMetaType());
    const auto output = method.returnMetaType().id() == QMetaType::Void
                            ? QGenericReturnArgument()
                            : QGenericReturnArgument(method.returnMetaType().name(), result.data());
    if (!method.invoke(entry.target, Qt::DirectConnection, output, args[0], args[1], args[2],
                       args[3], args[4], args[5], args[6], args[7], args[8], args[9]))
      return {false, {}, QStringLiteral("Service invocation failed")};
    return {true, result, {}};
  }
  return {false, {}, QStringLiteral("Unknown service action")};
}
} // namespace QindaTK
