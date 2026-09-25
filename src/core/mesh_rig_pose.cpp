// SPDX-License-Identifier: LGPL-3.0-or-later
#include "mesh_rig_document.h"
#include "automation_model.h"
#include "timeline_time.h"

#include <QHash>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <functional>

namespace QindaTK {
namespace {
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool near(const QVector3D &a, const QVector3D &b) { return (a - b).lengthSquared() < 1e-12f; }
constexpr int MaximumFrame = 100000;
constexpr int MaximumKeysPerBone = 1024;
constexpr int MaximumConstraintsPerBone = 4;
}

bool MeshRigDocument::withinLimits(const Bone &b, const QVector3D &p) {
    return p.x() >= b.minimum.x() && p.y() >= b.minimum.y() && p.z() >= b.minimum.z()
        && p.x() <= b.maximum.x() && p.y() <= b.maximum.y() && p.z() <= b.maximum.z();
}

// AGENT-NOTE (D-155): a bone's evaluated local rotation is, in order: its
// keys interpolated linearly at the frame (or its static pose when it has no
// keys), plus influence x each copyRotation target's evaluated rotation per
// Euler axis, clamped to the bone's own limits. Evaluation never writes back
// into poses or keys, so constraints and animation stay non-destructive.
QVector3D MeshRigDocument::sourcePose(const Bone &b, int frame) {
    if (b.keys.isEmpty()) return b.pose;
    if (frame <= b.keys.first().frame) return b.keys.first().pose;
    if (frame >= b.keys.last().frame) return b.keys.last().pose;
    for (int k = 1; k < b.keys.size(); ++k) {
        if (frame > b.keys[k].frame) continue;
        const auto &from = b.keys[k - 1], &to = b.keys[k];
        const float t = float(frame - from.frame) / float(to.frame - from.frame);
        return from.pose + (to.pose - from.pose) * t;
    }
    return b.keys.last().pose;
}
QVector3D MeshRigDocument::boneSource(const Bone &b) const {
    // AGENT-NOTE (D-178): a bone bound to the AnimationModel reads its three
    // rotation lanes at timeTicks; an open edit gesture previews its pose
    // until endEditGesture keys it. Private keys serve unbound bones.
    if (const auto it = m_gesturePoses.constFind(b.id); it != m_gesturePoses.cend()) return *it;
    if (isBoneAnimationBound(b.id)) {
        const QStringList lanes = boneAnimationParameters(b.id);
        const QString at = timeTicks();
        QVector3D pose;
        for (int axis = 0; axis < 3; ++axis) {
            const double value = m_animation->valueAt(lanes[axis], at);
            pose[axis] = std::isfinite(value) ? float(value) : b.pose[axis];
        }
        return pose;
    }
    return sourcePose(b, m_frame);
}
QVector<QVector3D> MeshRigDocument::currentPoses() const {
    return evaluatePoses(m_state, m_frame, [this](const Bone &b) { return boneSource(b); });
}
QVector<QVector3D> MeshRigDocument::evaluatePoses(const State &state, int frame,
                                                  const std::function<QVector3D(const Bone &)> &source) {
    const int count = int(state.bones.size());
    QHash<QString, int> index;
    for (int i = 0; i < count; ++i) index.insert(state.bones[i].id, i);
    QVector<QVector3D> result(count);
    QVector<int> mark(count, 0); // 0 pending, 1 in progress, 2 done
    std::function<QVector3D(int)> eval = [&](int i) -> QVector3D {
        if (mark[i] == 2) return result[i];
        const Bone &b = state.bones[i];
        QVector3D pose = source ? source(b) : sourcePose(b, frame);
        // AGENT-GUARD: addConstraint/stateFromJson reject cycles; the mark
        // only keeps a corrupt graph from recursing forever.
        if (mark[i] == 1) return pose;
        mark[i] = 1;
        for (const auto &c : b.constraints) {
            const int target = index.value(c.targetId, -1);
            if (target >= 0 && c.type == QLatin1String("copyRotation"))
                pose += eval(target) * float(c.influence);
        }
        for (int axis = 0; axis < 3; ++axis)
            pose[axis] = std::clamp(pose[axis], b.minimum[axis], b.maximum[axis]);
        result[i] = pose; mark[i] = 2;
        return pose;
    };
    for (int i = 0; i < count; ++i) eval(i);
    return result;
}

QVector<QMatrix4x4> MeshRigDocument::boneTransforms() const {
    const auto poses = currentPoses();
    QVector<QMatrix4x4> transforms(m_state.bones.size());
    for (int i = 0; i < m_state.bones.size(); ++i) {
        const auto &b = m_state.bones[i];
        QMatrix4x4 transform;
        const int p = boneIndex(b.parentId);
        if (p >= 0) transform = transforms[p]; // parents precede children
        transform.translate(b.head);
        transform.rotate(QQuaternion::fromEulerAngles(poses[i]));
        transform.translate(-b.head);
        transforms[i] = transform;
    }
    return transforms;
}
QVector<QVector3D> MeshRigDocument::posedPositions() const {
    const auto transforms = boneTransforms();
    QVector<QVector3D> result; result.reserve(m_state.vertices.size());
    for (const auto &v : m_state.vertices) {
        if (v.weights.isEmpty()) { result.append(v.rest); continue; }
        QVector3D posed;
        for (auto it = v.weights.cbegin(); it != v.weights.cend(); ++it)
            posed += transforms[boneIndex(it.key())].map(v.rest) * float(it.value());
        result.append(posed);
    }
    return result;
}

QVariantList MeshRigDocument::bones() const {
    const auto poses = currentPoses();
    const auto transforms = boneTransforms();
    QVariantList result;
    for (int i = 0; i < m_state.bones.size(); ++i) {
        const auto &b = m_state.bones[i];
        QVariantList constraints, keys;
        for (const auto &c : b.constraints)
            constraints.append(QVariantMap{{"constraintId", c.id}, {"type", c.type},
                                           {"targetId", c.targetId}, {"influence", c.influence}});
        for (const auto &k : b.keys)
            keys.append(QVariantMap{{"frame", k.frame}, {"pose", QVariant::fromValue(k.pose)}});
        result.append(QVariantMap{{"boneId", b.id}, {"name", b.name}, {"parentId", b.parentId},
                                  {"head", QVariant::fromValue(b.head)}, {"tail", QVariant::fromValue(b.tail)},
                                  {"minimum", QVariant::fromValue(b.minimum)},
                                  {"maximum", QVariant::fromValue(b.maximum)},
                                  {"pose", QVariant::fromValue(b.pose)},
                                  {"sourcePose", QVariant::fromValue(boneSource(b))},
                                  {"evaluatedPose", QVariant::fromValue(poses[i])},
                                  {"posedHead", QVariant::fromValue(transforms[i].map(b.head))},
                                  {"posedTail", QVariant::fromValue(transforms[i].map(b.tail))},
                                  {"animated", !b.keys.isEmpty() || isBoneAnimationBound(b.id)},
                                  {"animationBound", isBoneAnimationBound(b.id)},
                                  {"constraints", constraints}, {"keys", keys}});
    }
    return result;
}
QVector3D MeshRigDocument::evaluatedPose(const QString &boneId) const {
    const int i = boneIndex(boneId);
    return i < 0 ? QVector3D() : currentPoses().at(i);
}
// AGENT-CONTRACT (D-178): one playhead. currentFrame and timeTicks move
// together (ticks = frame x ticksPerFrame); private keys read the frame and
// AnimationModel lanes read the exact ticks. Neither is an undoable edit.
void MeshRigDocument::setCurrentFrame(int frame) {
    frame = std::clamp(frame, 0, MaximumFrame);
    const qint64 ticks = qint64(frame) * m_ticksPerFrame;
    if (frame == m_frame && ticks == m_ticks) return;
    m_frame = frame; m_ticks = ticks; notify();
}
void MeshRigDocument::setTimeTicks(const QString &text) {
    const auto ticks = TimelineTime::parseDecimal(text);
    if (!ticks || *ticks < 0 || *ticks / m_ticksPerFrame > MaximumFrame) { fail("Invalid rig time ticks"); return; }
    if (*ticks == m_ticks) return;
    m_ticks = *ticks; m_frame = int(*ticks / m_ticksPerFrame); notify();
}
void MeshRigDocument::setTicksPerFrame(const QString &text) {
    const auto ticks = TimelineTime::parseDecimal(text);
    if (!ticks || *ticks < 1 || *ticks > (qint64(1) << 40)) { fail("Ticks per frame must be 1 to 2^40"); return; }
    if (*ticks == m_ticksPerFrame) return;
    m_ticksPerFrame = *ticks; m_ticks = qint64(m_frame) * m_ticksPerFrame; notify();
}

QString MeshRigDocument::addBone(const QString &name, const QString &parentId,
                                  const QVector3D &head, const QVector3D &tail) {
    if (m_painting || name.isEmpty() || name.size() > 256 || (!parentId.isEmpty() && boneIndex(parentId) < 0)
        || !finitePoint(head) || !finitePoint(tail) || near(head, tail) || m_state.bones.size() >= 128)
        { fail("Invalid bone, parent or bone limit reached"); return {}; }
    Bone bone; bone.id = newId(); bone.name = name;
    bone.parentId = parentId; bone.head = head; bone.tail = tail;
    State next = m_state; next.bones.append(bone);
    return commit(next, QStringLiteral("Add bone")) ? bone.id : QString();
}
bool MeshRigDocument::setBoneLimits(const QString &id, const QVector3D &min, const QVector3D &max) {
    const int i = boneIndex(id);
    if (m_painting || i < 0 || !finitePoint(min) || !finitePoint(max)
        || min.x() < -180 || min.y() < -180 || min.z() < -180
        || max.x() > 180 || max.y() > 180 || max.z() > 180
        || min.x() > max.x() || min.y() > max.y() || min.z() > max.z())
        return fail("Invalid bone rotation limits");
    if (min.x() > 0 || min.y() > 0 || min.z() > 0
        || max.x() < 0 || max.y() < 0 || max.z() < 0)
        return fail("Bone limits must include the reset pose");
    Bone candidate = m_state.bones[i]; candidate.minimum = min; candidate.maximum = max;
    // AGENT-GUARD: limits never clamp stored data. A limit that excludes the
    // static pose or any animation key is refused, so animation is not
    // silently baked or destroyed by a rig edit.
    if (!withinLimits(candidate, candidate.pose)) return fail("Current pose is outside new limits");
    for (const auto &k : candidate.keys)
        if (!withinLimits(candidate, k.pose)) return fail("An animation key is outside new limits");
    if (!boundLanesWithin(candidate)) return fail("A timeline key is outside new limits");
    State next = m_state; next.bones[i] = candidate;
    return commit(next, QStringLiteral("Set bone limits"));
}
bool MeshRigDocument::setBonePose(const QString &id, const QVector3D &euler) {
    const int i = boneIndex(id);
    if (m_painting || i < 0 || !finitePoint(euler)) return fail("Invalid bone pose");
    if (!withinLimits(m_state.bones[i], euler)) return fail("Bone pose exceeds rotation limits");
    if (isBoneAnimationBound(id)) {
        // Inside a gesture the pose previews; endEditGesture keys it once.
        if (m_gestureOpen) { m_gesturePoses.insert(id, euler); notify(); return true; }
        return keyBoundBone(id, m_ticks, euler);
    }
    // An animated bone is posed by keying the current frame (auto-key).
    if (!m_state.bones[i].keys.isEmpty()) return setPoseKey(id, m_frame, euler);
    if (euler == m_state.bones[i].pose) return true;
    State next = m_state; next.bones[i].pose = euler;
    return commit(next, QStringLiteral("Pose bone"));
}
bool MeshRigDocument::resetPose() {
    if (m_painting) return fail("Finish painting before posing");
    // Reset clears static poses only; animation keys are data, not pose state.
    bool changed = false; for (const auto &b : m_state.bones) changed |= !b.pose.isNull();
    if (!changed) return true;
    State next = m_state; for (auto &b : next.bones) b.pose = {};
    return commit(next, QStringLiteral("Reset pose"));
}

bool MeshRigDocument::constraintReaches(const State &state, const QString &from, const QString &to) {
    QSet<QString> seen; QVector<QString> stack{from};
    while (!stack.isEmpty()) {
        const QString id = stack.takeLast();
        if (id == to) return true;
        if (seen.contains(id)) continue;
        seen.insert(id);
        for (const auto &b : state.bones)
            if (b.id == id) for (const auto &c : b.constraints) stack.append(c.targetId);
    }
    return false;
}
QString MeshRigDocument::addConstraint(const QString &boneId, const QString &type,
                                       const QString &targetId, qreal influence) {
    const int i = boneIndex(boneId);
    if (m_painting || i < 0 || boneIndex(targetId) < 0 || boneId == targetId
        || type != QLatin1String("copyRotation") || !std::isfinite(influence)
        || influence < 0 || influence > 1)
        { fail("Invalid constraint bone, target, type or influence"); return {}; }
    if (m_state.bones[i].constraints.size() >= MaximumConstraintsPerBone)
        { fail("Bone constraint limit reached"); return {}; }
    // The target's rotation must not already depend on this bone.
    if (constraintReaches(m_state, targetId, boneId))
        { fail("Constraint would create a dependency cycle"); return {}; }
    Constraint c; c.id = newId(); c.type = type; c.targetId = targetId; c.influence = influence;
    State next = m_state; next.bones[i].constraints.append(c);
    return commit(next, QStringLiteral("Add constraint")) ? c.id : QString();
}
bool MeshRigDocument::removeConstraint(const QString &constraintId) {
    if (m_painting) return fail("Finish painting before editing the rig");
    for (int i = 0; i < m_state.bones.size(); ++i)
        for (int k = 0; k < m_state.bones[i].constraints.size(); ++k)
            if (m_state.bones[i].constraints[k].id == constraintId) {
                State next = m_state; next.bones[i].constraints.removeAt(k);
                return commit(next, QStringLiteral("Remove constraint"));
            }
    return fail("Unknown constraint");
}
bool MeshRigDocument::setPoseKey(const QString &boneId, int frame, const QVector3D &euler) {
    const int i = boneIndex(boneId);
    if (m_painting || i < 0 || frame < 0 || frame > MaximumFrame || !finitePoint(euler))
        return fail("Invalid pose key");
    if (!withinLimits(m_state.bones[i], euler)) return fail("Pose key exceeds rotation limits");
    if (isBoneAnimationBound(boneId)) return keyBoundBone(boneId, qint64(frame) * m_ticksPerFrame, euler);
    State next = m_state; auto &keys = next.bones[i].keys;
    auto at = std::lower_bound(keys.begin(), keys.end(), frame,
                               [](const PoseKey &key, int f) { return key.frame < f; });
    if (at != keys.end() && at->frame == frame) {
        if (at->pose == euler) return true;
        at->pose = euler;
    } else {
        if (keys.size() >= MaximumKeysPerBone) return fail("Pose key limit reached");
        keys.insert(at, PoseKey{frame, euler});
    }
    return commit(next, QStringLiteral("Key bone pose"));
}
bool MeshRigDocument::removePoseKey(const QString &boneId, int frame) {
    const int i = boneIndex(boneId);
    if (m_painting || i < 0) return fail("Invalid pose key");
    if (isBoneAnimationBound(boneId)) return removeBoundKey(boneId, qint64(frame) * m_ticksPerFrame);
    const auto &keys = m_state.bones[i].keys;
    for (int k = 0; k < keys.size(); ++k)
        if (keys[k].frame == frame) {
            State next = m_state; next.bones[i].keys.removeAt(k);
            return commit(next, QStringLiteral("Remove pose key"));
        }
    return fail("No pose key at that frame");
}

} // namespace QindaTK
