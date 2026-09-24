// SPDX-License-Identifier: LGPL-3.0-or-later
// The QML map facade of AutomationModel transactions.
#include "automation_model.h"

namespace QindaTK {
namespace {

QVariantMap failure(const OperationError &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error.message},
            {QStringLiteral("code"), int(error.code)}, {QStringLiteral("field"), error.fieldPath}};
}

OperationError argument(const QString &message)
{
    return {ErrorCode::InvalidArgument, message, {}, QStringLiteral("automation")};
}

OperationError invalid(const QString &message)
{
    return {ErrorCode::Validation, message, {}, QStringLiteral("automation")};
}

bool readFields(const QVariantMap &map, AutomationEditFields &fields, QString &error)
{
    if (map.contains(QStringLiteral("ticks"))) {
        const QVariant value = map.value(QStringLiteral("ticks"));
        fields.ticks = value.metaType().id() == QMetaType::QString ? TimelineTime::parseDecimal(value.toString())
                                                                   : std::nullopt;
        if (!fields.ticks) return (error = AutomationModel::tr("ticks must be an exact decimal string")), false;
    }
    for (const auto &[key, target] : {std::pair{QStringLiteral("value"), &fields.value},
                                      std::pair{QStringLiteral("minimum"), &fields.minimum},
                                      std::pair{QStringLiteral("maximum"), &fields.maximum},
                                      std::pair{QStringLiteral("default"), &fields.defaultValue}}) {
        if (!map.contains(key)) continue;
        bool ok = false;
        *target = map.value(key).toDouble(&ok);
        if (!ok) return (error = AutomationModel::tr("%1 must be a number").arg(key)), false;
    }
    if (map.contains(QStringLiteral("interpolation"))) {
        fields.interpolation = AutomationCodec::interpolationFromName(map.value(QStringLiteral("interpolation")).toString());
        if (!fields.interpolation) return (error = AutomationModel::tr("interpolation must be step, linear or cubic")), false;
    }
    if (map.contains(QStringLiteral("handle"))) {
        const QVariantMap handle = map.value(QStringLiteral("handle")).toMap();
        bool okX = false, okY = false;
        fields.handle = AutomationHandle{handle.value(QStringLiteral("x")).toDouble(&okX),
                                         handle.value(QStringLiteral("y")).toDouble(&okY)};
        if (!okX || !okY) return (error = AutomationModel::tr("handle needs numeric x and y")), false;
    }
    fields.clearHandle = map.value(QStringLiteral("clearHandle")).toBool();
    return true;
}
} // namespace

QVariantMap AutomationModel::beginEdit(const QVariantMap &map)
{
    static const QHash<QString, AutomationEditKind> kinds{
        {QStringLiteral("movePoints"), AutomationEditKind::MovePoints},
        {QStringLiteral("addPoint"), AutomationEditKind::AddPoint},
        {QStringLiteral("removePoints"), AutomationEditKind::RemovePoints},
        {QStringLiteral("setPoint"), AutomationEditKind::SetPoint},
        {QStringLiteral("moveSegment"), AutomationEditKind::MoveSegment},
        {QStringLiteral("setHandle"), AutomationEditKind::SetHandle},
        {QStringLiteral("setInterpolation"), AutomationEditKind::SetInterpolation},
        {QStringLiteral("editParameter"), AutomationEditKind::EditParameter}};
    const auto kind = kinds.constFind(map.value(QStringLiteral("kind")).toString());
    if (kind == kinds.cend()) return failure(fail(argument(tr("Unknown automation edit kind"))));
    AutomationEditRequest request;
    request.kind = *kind;
    request.parameterId = map.value(QStringLiteral("parameterId")).toString();
    for (const auto &id : map.value(QStringLiteral("pointIds")).toStringList()) request.pointIds.push_back(QUuid(id));
    request.anchorPointId = QUuid(map.value(QStringLiteral("pointId"), map.value(QStringLiteral("anchorPointId"))).toString());
    request.side = map.value(QStringLiteral("side")).toString();
    request.label = map.value(QStringLiteral("label")).toString();
    if (map.contains(QStringLiteral("expectedRevision"))) {
        bool ok = false;
        request.expectedRevision = map.value(QStringLiteral("expectedRevision")).toString().toULongLong(&ok);
        if (!ok) return failure(fail(argument(tr("expectedRevision must be a decimal string"))));
    }
    QString error;
    if (!readFields(map, request.fields, error)) return failure(fail(argument(error)));
    OperationError failed;
    const QString operation = begin(request, &failed);
    if (operation.isEmpty()) return failure(failed);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("operationId"), operation}};
}

QVariantMap AutomationModel::updateEdit(const QString &operationId, const QVariantMap &input)
{
    AutomationEditFields fields;
    QString error;
    if (!readFields(input, fields, error)) return failure(argument(error));
    if (!m_session || m_session->operationId != operationId) return failure(argument(tr("No such automation edit")));
    const OperationError result = update(operationId, fields);
    return {{QStringLiteral("ok"), result.code != ErrorCode::StaleRevision && result.code != ErrorCode::InvalidArgument},
            {QStringLiteral("valid"), !result}, {QStringLiteral("error"), result.message}};
}

QVariantMap AutomationModel::commitEdit(const QString &operationId)
{
    QString transaction;
    QStringList created;
    if (auto error = commit(operationId, &transaction, &created)) return failure(error);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("transactionId"), transaction},
            {QStringLiteral("changed"), !transaction.isEmpty()}, {QStringLiteral("createdIds"), created},
            {QStringLiteral("revision"), revisionText()}};
}

bool AutomationModel::cancelEdit(const QString &operationId)
{
    if (!m_session || m_session->operationId != operationId) return false;
    cancel(operationId);
    return true;
}

QVariantMap AutomationModel::applyEdit(const QVariantMap &request, const QVariantMap &input)
{
    const QVariantMap begun = beginEdit(request);
    if (!begun.value(QStringLiteral("ok")).toBool()) return begun;
    const QString operation = begun.value(QStringLiteral("operationId")).toString();
    const QVariantMap updated = updateEdit(operation, input);
    if (!updated.value(QStringLiteral("valid")).toBool()) {
        cancel(operation);
        return failure(fail(invalid(updated.value(QStringLiteral("error")).toString())));
    }
    return commitEdit(operation);
}

} // namespace QindaTK
