// SPDX-License-Identifier: LGPL-3.0-or-later
#include "mesh_rig_document.h"
#include "automation_model.h"

namespace QindaTK {
namespace {
const char *const AxisNames[3] = {"x", "y", "z"};
}

// AGENT-CONTRACT (D-291): a bone's timeline animation lives in the WP-073
// AutomationModel as three angle lanes "meshrig/bone:<boneId>/rotation.<axis>"
// (degrees, -180..180, linear by default). KeyframeTimeline, DopeSheet and
// CurveEditor edit those lanes directly; this document only reads them at
// timeTicks and keys them from pose edits. The host saves the AutomationModel
// next to the mesh (as MotionComposition does); the mesh file keeps the rig.
QStringList MeshRigDocument::boneAnimationParameters(const QString &boneId) const {
    QStringList ids;
    for (const char *axis : AxisNames)
        ids.append(QStringLiteral("meshrig/bone:%1/rotation.%2").arg(boneId, QLatin1String(axis)));
    return ids;
}
bool MeshRigDocument::isBoneAnimationBound(const QString &boneId) const {
    if (!m_animation) return false;
    for (const QString &id : boneAnimationParameters(boneId))
        if (!m_animation->lane(id)) return false;
    return true;
}
void MeshRigDocument::setAnimationModel(AutomationModel *model) {
    if (model == m_animation) return;
    if (m_painting) { fail("Finish painting before changing the animation model"); return; }
    for (const auto &connection : std::as_const(m_animationConnections)) disconnect(connection);
    m_animationConnections.clear();
    m_gesturePoses.clear();
    m_animation = model;
    if (model) {
        const auto refresh = [this] { emit changed(); };
        m_animationConnections = {
            connect(model, &AutomationModel::revisionChanged, this, refresh),
            connect(model, &AutomationModel::parametersChanged, this, refresh),
            connect(model, &AutomationModel::pointsChanged, this, refresh),
            connect(model, &QObject::destroyed, this, [this] {
                m_animation = nullptr; m_gesturePoses.clear();
                emit animationModelChanged(); notify();
            })};
    }
    emit animationModelChanged();
    notify();
}
bool MeshRigDocument::bindBoneAnimation(const QString &boneId) {
    const int i = boneIndex(boneId);
    if (!m_animation) return fail("Set an animationModel before binding bone animation");
    if (m_painting || i < 0) return fail("Invalid bone");
    if (isBoneAnimationBound(boneId)) return true;
    const Bone &bone = m_state.bones[i];
    const QStringList ids = boneAnimationParameters(boneId);
    for (const QString &id : ids)
        if (m_animation->lane(id)) return fail("A rotation lane for this bone already exists");
    for (int axis = 0; axis < 3; ++axis) {
        AutomationParameter parameter;
        parameter.id = ids[axis];
        parameter.name = QStringLiteral("%1 rotation %2").arg(bone.name, QString::fromLatin1(AxisNames[axis]).toUpper());
        parameter.range = {UnitDimension::Angle, QStringLiteral("deg"), -180.0, 180.0};
        parameter.defaultValue = bone.pose[axis];
        parameter.interpolation = CurveInterpolation::Linear;
        OperationError error;
        if (!m_animation->addParameter(parameter, &error)) {
            for (int added = 0; added < axis; ++added) m_animation->removeParameter(ids[added]);
            return fail(error.message);
        }
    }
    // Private keys are copied, not moved: the mesh file keeps them, so an
    // unbound document still evaluates the same animation (never destroyed).
    for (const PoseKey &key : bone.keys) {
        QVector<AutomationBatchEdit> edits;
        for (int axis = 0; axis < 3; ++axis) {
            AutomationBatchEdit edit;
            edit.request.kind = AutomationEditKind::AddPoint;
            edit.request.parameterId = ids[axis];
            edit.input.ticks = qint64(key.frame) * m_ticksPerFrame;
            edit.input.value = key.pose[axis];
            edit.input.interpolation = CurveInterpolation::Linear;
            edits.append(edit);
        }
        if (auto error = m_animation->applyBatch(edits, QStringLiteral("Bind bone animation"))) {
            for (const QString &id : ids) m_animation->removeParameter(id);
            return fail(error.message);
        }
    }
    notify();
    return true;
}
bool MeshRigDocument::keyBoundBone(const QString &boneId, qint64 ticks, const QVector3D &euler) {
    const QStringList ids = boneAnimationParameters(boneId);
    QVector<AutomationBatchEdit> edits;
    for (int axis = 0; axis < 3; ++axis) {
        const AutomationModel::Lane *lane = m_animation->lane(ids[axis]);
        AutomationBatchEdit edit;
        edit.request.parameterId = ids[axis];
        edit.request.kind = AutomationEditKind::AddPoint;
        edit.input.ticks = ticks;
        for (const auto &point : lane->points)
            if (point.ticks == ticks) {
                if (point.value == euler[axis]) { edit.request.parameterId.clear(); break; }
                edit.request.kind = AutomationEditKind::SetPoint;
                edit.request.anchorPointId = point.id;
                edit.input.ticks.reset();
            }
        if (edit.request.parameterId.isEmpty()) continue;
        edit.input.value = euler[axis];
        edits.append(edit);
    }
    if (edits.isEmpty()) return true;
    if (auto error = m_animation->applyBatch(edits, QStringLiteral("Key bone pose"))) return fail(error.message);
    notify();
    return true;
}
bool MeshRigDocument::removeBoundKey(const QString &boneId, qint64 ticks) {
    const QStringList ids = boneAnimationParameters(boneId);
    QVector<AutomationBatchEdit> edits;
    for (const QString &id : ids)
        for (const auto &point : m_animation->lane(id)->points)
            if (point.ticks == ticks) {
                AutomationBatchEdit edit;
                edit.request.kind = AutomationEditKind::RemovePoints;
                edit.request.parameterId = id;
                edit.request.pointIds = {point.id};
                edits.append(edit);
            }
    if (edits.isEmpty()) return fail("No pose key at that frame");
    if (auto error = m_animation->applyBatch(edits, QStringLiteral("Remove pose key"))) return fail(error.message);
    notify();
    return true;
}
bool MeshRigDocument::boundLanesWithin(const Bone &bone) const {
    if (!isBoneAnimationBound(bone.id)) return true;
    const QStringList ids = boneAnimationParameters(bone.id);
    for (int axis = 0; axis < 3; ++axis) {
        const auto *lane = m_animation->lane(ids[axis]);
        const auto inside = [&](double v) { return v >= bone.minimum[axis] && v <= bone.maximum[axis]; };
        if (!inside(lane->parameter.defaultValue)) return false;
        for (const auto &point : lane->points) if (!inside(point.value)) return false;
    }
    return true;
}

bool MeshRigDocument::beginEditGesture() {
    if (m_gestureOpen) return fail("An edit gesture is already open");
    m_gestureOpen = true; ++m_gestureSerial; m_gesturePoses.clear();
    notify(); return true;
}
bool MeshRigDocument::endEditGesture() {
    if (!m_gestureOpen) return fail("No edit gesture is open");
    m_gestureOpen = false;
    const auto previews = std::exchange(m_gesturePoses, {});
    bool ok = true;
    for (auto it = previews.cbegin(); it != previews.cend(); ++it)
        if (isBoneAnimationBound(it.key())) ok = keyBoundBone(it.key(), m_ticks, it.value()) && ok;
    if (ok) notify(); else emit changed(); // keep the refusal in lastError
    return ok;
}

} // namespace QindaTK
