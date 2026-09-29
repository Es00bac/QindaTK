// SPDX-License-Identifier: LGPL-3.0-or-later
#include "agent_schema.h"
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <cmath>
namespace QindaTK {
namespace {
QString schemaError(const QJsonObject &s, const QString &path, int depth) {
  if (depth > 16)
    return path + QStringLiteral(": schema nesting exceeds 16");
  static const QSet<QString> supported{
      QStringLiteral("type"),          QStringLiteral("properties"),
      QStringLiteral("required"),      QStringLiteral("additionalProperties"),
      QStringLiteral("items"),         QStringLiteral("minItems"),
      QStringLiteral("maxItems"),      QStringLiteral("minProperties"),
      QStringLiteral("maxProperties"), QStringLiteral("minLength"),
      QStringLiteral("maxLength"),     QStringLiteral("minimum"),
      QStringLiteral("maximum"),       QStringLiteral("enum"),
      QStringLiteral("pattern"),       QStringLiteral("anyOf"),
      QStringLiteral("description")};
  for (auto it = s.begin(); it != s.end(); ++it)
    if (!supported.contains(it.key()))
      return path + QStringLiteral(": unsupported schema keyword ") + it.key();
  const auto malformed = [&] {
    return path + QStringLiteral(": malformed input schema");
  };
  if (s.contains(QStringLiteral("type"))) {
    const auto type = s.value(QStringLiteral("type"));
    const QStringList types{
        QStringLiteral("object"),  QStringLiteral("array"),
        QStringLiteral("string"),  QStringLiteral("number"),
        QStringLiteral("integer"), QStringLiteral("boolean"),
        QStringLiteral("null")};
    if (!type.isString() || !types.contains(type.toString()))
      return malformed();
  }
  for (const char *key : {"minItems", "maxItems", "minProperties",
                          "maxProperties", "minLength", "maxLength"}) {
    const auto limit = s.value(QString::fromLatin1(key));
    if (!limit.isUndefined() &&
        (!limit.isDouble() || limit.toDouble() < 0 ||
         std::floor(limit.toDouble()) != limit.toDouble()))
      return malformed();
  }
  for (const char *key : {"minimum", "maximum"}) {
    const auto limit = s.value(QString::fromLatin1(key));
    if (!limit.isUndefined() && !limit.isDouble())
      return malformed();
  }
  for (const char *key : {"pattern", "description"}) {
    const auto text = s.value(QString::fromLatin1(key));
    if (!text.isUndefined() && !text.isString())
      return malformed();
  }
  if (s.contains(QStringLiteral("pattern")) &&
      !QRegularExpression(s.value(QStringLiteral("pattern")).toString())
           .isValid())
    return malformed();
  const auto properties = s.value(QStringLiteral("properties"));
  if (!properties.isUndefined() && !properties.isObject())
    return malformed();
  const auto required = s.value(QStringLiteral("required"));
  if (!required.isUndefined() && !required.isArray())
    return malformed();
  for (const auto item : required.toArray())
    if (!item.isString())
      return malformed();
  const auto values = s.value(QStringLiteral("enum"));
  if (!values.isUndefined() &&
      (!values.isArray() || values.toArray().isEmpty()))
    return malformed();
  QJsonArray children;
  for (const auto property : properties.toObject())
    children.append(property);
  const auto items = s.value(QStringLiteral("items"));
  if (!items.isUndefined())
    children.append(items);
  const auto extra = s.value(QStringLiteral("additionalProperties"));
  if (!extra.isUndefined() && !extra.isBool())
    children.append(extra);
  const auto variants = s.value(QStringLiteral("anyOf"));
  if (!variants.isUndefined()) {
    if (!variants.isArray() || variants.toArray().isEmpty())
      return malformed();
    for (const auto variant : variants.toArray())
      children.append(variant);
  }
  for (const auto child : children) {
    if (!child.isObject())
      return malformed();
    const auto error = schemaError(child.toObject(), path, depth + 1);
    if (!error.isEmpty())
      return error;
  }
  return {};
}

QString validateValue(const QJsonValue &v, const QJsonObject &s,
                      const QString &path, int depth) {
  const auto variants = s.value(QStringLiteral("anyOf"));
  if (!variants.isUndefined()) {
    if (!variants.isArray() || variants.toArray().isEmpty())
      return path + QStringLiteral(": malformed anyOf");
    bool matches = false;
    for (const auto variant : variants.toArray())
      if (variant.isObject() &&
          validateValue(v, variant.toObject(), path, depth + 1).isEmpty())
        matches = true;
    if (!matches)
      return path + QStringLiteral(": no allowed type matches");
  }
  const QString type = s.value(QStringLiteral("type")).toString();
  const bool valid = type.isEmpty() ||
                     (type == QLatin1String("object") && v.isObject()) ||
                     (type == QLatin1String("array") && v.isArray()) ||
                     (type == QLatin1String("string") && v.isString()) ||
                     (type == QLatin1String("number") && v.isDouble()) ||
                     (type == QLatin1String("boolean") && v.isBool()) ||
                     (type == QLatin1String("null") && v.isNull()) ||
                     (type == QLatin1String("integer") && v.isDouble() &&
                      std::floor(v.toDouble()) == v.toDouble());
  if (!valid)
    return path + QStringLiteral(": expected ") + type;
  if (s.contains(QStringLiteral("enum")) &&
      !s.value(QStringLiteral("enum")).toArray().contains(v))
    return path + QStringLiteral(": value is not in enum");
  const auto bound = [&](const char *key, double actual, bool lower) {
    const auto limit = s.value(QString::fromLatin1(key));
    return limit.isUndefined() ||
           (limit.isDouble() &&
            (lower ? actual >= limit.toDouble() : actual <= limit.toDouble()));
  };
  if (v.isString()) {
    const QString text = v.toString();
    if (!bound("minLength", text.size(), true) ||
        !bound("maxLength", text.size(), false))
      return path + QStringLiteral(": text length is outside bounds");
    if (s.contains(QStringLiteral("pattern"))) {
      const QRegularExpression expression(
          s.value(QStringLiteral("pattern")).toString());
      if (!expression.isValid() || !expression.match(text).hasMatch())
        return path + QStringLiteral(": text does not match pattern");
    }
  }
  if (v.isDouble() && (!bound("minimum", v.toDouble(), true) ||
                       !bound("maximum", v.toDouble(), false)))
    return path + QStringLiteral(": number is outside bounds");
  if (v.isArray()) {
    const auto array = v.toArray();
    if (!bound("minItems", array.size(), true) ||
        !bound("maxItems", array.size(), false))
      return path + QStringLiteral(": array size is outside bounds");
    const auto item = s.value(QStringLiteral("items"));
    if (!item.isUndefined() && !item.isObject())
      return path + QStringLiteral(": malformed items schema");
    if (item.isObject())
      for (qsizetype i = 0; i < array.size(); ++i) {
        const QString error =
            validateValue(array.at(i), item.toObject(),
                          path + QStringLiteral("[%1]").arg(i), depth + 1);
        if (!error.isEmpty())
          return error;
      }
  }
  if (v.isObject()) {
    const auto object = v.toObject(),
               properties = s.value(QStringLiteral("properties")).toObject();
    if (!bound("minProperties", object.size(), true) ||
        !bound("maxProperties", object.size(), false))
      return path + QStringLiteral(": object size is outside bounds");
    for (const auto required : s.value(QStringLiteral("required")).toArray())
      if (!required.isString() || !object.contains(required.toString()))
        return path + QStringLiteral(": missing required property ") +
               required.toString();
    for (auto it = object.begin(); it != object.end(); ++it) {
      const auto extra = s.value(QStringLiteral("additionalProperties"));
      if (!properties.contains(it.key()) && extra.isBool() && !extra.toBool())
        return path + QStringLiteral(": unknown property ") + it.key();
      const auto schema =
          properties.contains(it.key()) ? properties.value(it.key()) : extra;
      if (schema.isObject()) {
        const QString error =
            validateValue(it.value(), schema.toObject(),
                          path + QLatin1Char('.') + it.key(), depth + 1);
        if (!error.isEmpty())
          return error;
      }
    }
  }
  return {};
}
} // namespace

QString validateAgentJson(const QJsonValue &value, const QJsonObject &schema,
                          const QString &path, int depth) {
  const auto error = schemaError(schema, path, depth);
  return error.isEmpty() ? validateValue(value, schema, path, depth) : error;
}
} // namespace QindaTK
