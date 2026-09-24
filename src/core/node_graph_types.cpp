// SPDX-License-Identifier: LGPL-3.0-or-later
#include "node_graph_model.h"

#include <QColor>
#include <QMetaType>
#include <cmath>

// AGENT-NOTE: this table is the single source of node types, typed ports and
// parameter identity. Validation, evaluation defaults, the `nodeTypes`
// descriptors and file loading all read it; docs/node-graph.md mirrors it.
namespace QindaTK {
namespace {
enum class Kind { Number, Color, Text, Choice };

struct ParameterSpec {
    QString id;
    QString label;
    Kind kind;
    double minimum;
    double maximum; // Text: maximum length
    QVariant fallback;
    QStringList options;
};

struct PortSpec {
    QString id;
    QString label;
    QString type;
    bool optional;
};

struct TypeSpec {
    QString type;
    QString title;
    QVector<PortSpec> inputs;
    QVector<PortSpec> outputs;
    QVector<ParameterSpec> parameters;
};

ParameterSpec number(const char *id, const char *label, double minimum, double maximum,
                     double fallback)
{
    return {QString::fromLatin1(id), QString::fromLatin1(label), Kind::Number,
            minimum, maximum, fallback, {}};
}

const QVector<TypeSpec> &specs()
{
    static const QVector<TypeSpec> table = [] {
        const QString image = QStringLiteral("image");
        const QString mask = QStringLiteral("mask");
        const PortSpec imageIn{image, QStringLiteral("Image"), image, false};
        const PortSpec imageOut = imageIn;
        const QStringList shapes{QStringLiteral("rectangle"), QStringLiteral("ellipse")};
        const QStringList modes{QStringLiteral("normal"), QStringLiteral("multiply"),
            QStringLiteral("screen"), QStringLiteral("overlay"), QStringLiteral("darken"),
            QStringLiteral("lighten"), QStringLiteral("difference"), QStringLiteral("add")};
        return QVector<TypeSpec>{
            {QStringLiteral("source"), QStringLiteral("Source"), {}, {imageOut},
             {{QStringLiteral("sourceId"), QStringLiteral("Source ID"), Kind::Text,
               0, 128, QString(), {}}}},
            {QStringLiteral("solid"), QStringLiteral("Solid color"), {}, {imageOut},
             {{QStringLiteral("color"), QStringLiteral("Color"), Kind::Color,
               0, 0, QStringLiteral("transparent"), {}}}},
            {QStringLiteral("mask"), QStringLiteral("Shape mask"), {},
             {{mask, QStringLiteral("Mask"), mask, false}},
             {{QStringLiteral("shape"), QStringLiteral("Shape"), Kind::Choice,
               0, 0, QStringLiteral("rectangle"), shapes},
              number("x", "X", -1000000, 1000000, 0), number("y", "Y", -1000000, 1000000, 0),
              number("width", "Width", 0, 1000000, 100),
              number("height", "Height", 0, 1000000, 100)}},
            {QStringLiteral("transform"), QStringLiteral("Transform"), {imageIn}, {imageOut},
             {number("x", "X", -1000000, 1000000, 0), number("y", "Y", -1000000, 1000000, 0),
              number("scale", "Scale", 0.001, 1000, 1),
              number("rotation", "Rotation", -3600, 3600, 0)}},
            {QStringLiteral("blend"), QStringLiteral("Blend"),
             {{QStringLiteral("background"), QStringLiteral("Background"), image, false},
              {QStringLiteral("foreground"), QStringLiteral("Foreground"), image, false},
              {mask, QStringLiteral("Mask"), mask, true}},
             {imageOut},
             {number("opacity", "Opacity", 0, 1, 1),
              {QStringLiteral("mode"), QStringLiteral("Mode"), Kind::Choice,
               0, 0, QStringLiteral("normal"), modes}}},
            {QStringLiteral("output"), QStringLiteral("Output"), {imageIn}, {}, {}}};
    }();
    return table;
}

const TypeSpec *spec(const QString &type)
{
    for (const TypeSpec &candidate : specs())
        if (candidate.type == type) return &candidate;
    return nullptr;
}

const PortSpec *findPort(const QString &type, const QString &portId, bool input)
{
    const TypeSpec *typeSpec = spec(type);
    if (!typeSpec) return nullptr;
    for (const PortSpec &candidate : input ? typeSpec->inputs : typeSpec->outputs)
        if (candidate.id == portId) return &candidate;
    return nullptr;
}

bool finiteNumber(const QVariant &value, double *number)
{
    const int type = value.metaType().id();
    if (type != QMetaType::Int && type != QMetaType::UInt
        && type != QMetaType::LongLong && type != QMetaType::ULongLong
        && type != QMetaType::Double && type != QMetaType::Float) return false;
    bool ok = false;
    *number = value.toDouble(&ok);
    return ok && std::isfinite(*number);
}

QString kindName(Kind kind)
{
    switch (kind) {
    case Kind::Number: return QStringLiteral("number");
    case Kind::Color: return QStringLiteral("color");
    case Kind::Text: return QStringLiteral("string");
    case Kind::Choice: return QStringLiteral("choice");
    }
    return {};
}
} // namespace

bool NodeGraphModel::validType(const QString &type)
{
    return spec(type) != nullptr;
}

QVariantList NodeGraphModel::portList(const QString &type, bool inputs)
{
    QVariantList result;
    if (const TypeSpec *typeSpec = spec(type)) {
        for (const PortSpec &port : inputs ? typeSpec->inputs : typeSpec->outputs) {
            result.append(QVariantMap{{QStringLiteral("portId"), port.id},
                {QStringLiteral("label"), port.label}, {QStringLiteral("type"), port.type},
                {QStringLiteral("optional"), port.optional}});
        }
    }
    return result;
}

QString NodeGraphModel::portType(const QString &type, const QString &portId, bool input)
{
    const PortSpec *port = findPort(type, portId, input);
    return port ? port->type : QString();
}

bool NodeGraphModel::hasPort(const QString &type, const QString &portId, bool input)
{
    return findPort(type, portId, input) != nullptr;
}

bool NodeGraphModel::validParameters(const QString &type, const QVariantMap &parameters)
{
    const TypeSpec *typeSpec = spec(type);
    if (!typeSpec || parameters.size() > typeSpec->parameters.size()) return false;
    for (auto it = parameters.cbegin(); it != parameters.cend(); ++it) {
        const ParameterSpec *parameterSpec = nullptr;
        for (const ParameterSpec &candidate : typeSpec->parameters)
            if (candidate.id == it.key()) parameterSpec = &candidate;
        if (!parameterSpec) return false;
        const bool text = it.value().metaType().id() == QMetaType::QString;
        const QString string = it.value().toString();
        double value = 0;
        switch (parameterSpec->kind) {
        case Kind::Number:
            if (!finiteNumber(it.value(), &value) || value < parameterSpec->minimum
                || value > parameterSpec->maximum) return false;
            break;
        case Kind::Color:
            if (!text || !QColor::isValidColorName(string)) return false;
            break;
        case Kind::Text:
            if (!text || string.size() > parameterSpec->maximum) return false;
            break;
        case Kind::Choice:
            if (!text || !parameterSpec->options.contains(string)) return false;
            break;
        }
    }
    return true;
}

QVariant NodeGraphModel::parameter(const QString &type, const QVariantMap &parameters,
                                   const QString &parameterId)
{
    const auto found = parameters.constFind(parameterId);
    if (found != parameters.cend()) return found.value();
    if (const TypeSpec *typeSpec = spec(type))
        for (const ParameterSpec &candidate : typeSpec->parameters)
            if (candidate.id == parameterId) return candidate.fallback;
    return {};
}

QVariantList NodeGraphModel::nodeTypes()
{
    QVariantList result;
    for (const TypeSpec &typeSpec : specs()) {
        QVariantList parameters;
        for (const ParameterSpec &parameterSpec : typeSpec.parameters) {
            QVariantMap entry{{QStringLiteral("parameterId"), parameterSpec.id},
                {QStringLiteral("label"), parameterSpec.label},
                {QStringLiteral("valueType"), kindName(parameterSpec.kind)},
                {QStringLiteral("defaultValue"), parameterSpec.fallback}};
            if (parameterSpec.kind == Kind::Number) {
                entry.insert(QStringLiteral("minimum"), parameterSpec.minimum);
                entry.insert(QStringLiteral("maximum"), parameterSpec.maximum);
            } else if (parameterSpec.kind == Kind::Text) {
                entry.insert(QStringLiteral("maximumLength"), int(parameterSpec.maximum));
            } else if (parameterSpec.kind == Kind::Choice) {
                entry.insert(QStringLiteral("options"), parameterSpec.options);
            }
            parameters.append(entry);
        }
        result.append(QVariantMap{{QStringLiteral("type"), typeSpec.type},
            {QStringLiteral("title"), typeSpec.title},
            {QStringLiteral("inputs"), portList(typeSpec.type, true)},
            {QStringLiteral("outputs"), portList(typeSpec.type, false)},
            {QStringLiteral("parameters"), parameters}});
    }
    return result;
}

} // namespace QindaTK
