// SPDX-License-Identifier: LGPL-3.0-or-later
#include "mesh_rig_document.h"

namespace QindaTK {

// AGENT-GUARD: posedPositions/boneTransforms read a parent's transform before
// its children ("parents precede children"). Every hierarchy edit reorders
// with this stable topological sort and refuses a cycle.
bool MeshRigDocument::orderBones(QVector<Bone> *bones) {
    QVector<Bone> ordered; ordered.reserve(bones->size());
    QSet<QString> placed;
    QVector<Bone> pending = *bones;
    while (!pending.isEmpty()) {
        bool progress = false;
        for (int i = 0; i < pending.size();) {
            if (pending[i].parentId.isEmpty() || placed.contains(pending[i].parentId)) {
                placed.insert(pending[i].id); ordered.append(pending.takeAt(i)); progress = true;
            } else {
                ++i;
            }
        }
        if (!progress) return false;
    }
    *bones = ordered;
    return true;
}

// AGENT-NOTE (VID-18): authoring edits change the rig, never animation data.
// Removing a bone that anything still refers to (children, vertex weights,
// constraints, private keys or bound timeline lanes) is refused rather than
// silently rewriting those references.
bool MeshRigDocument::removeBone(const QString &boneId) {
    const int i = boneIndex(boneId);
    if (m_painting || i < 0) return fail("Invalid bone");
    for (const auto &b : m_state.bones) {
        if (b.parentId == boneId) return fail("Reparent or remove the bone's children first");
        for (const auto &c : b.constraints)
            if (c.targetId == boneId) return fail("A constraint targets this bone");
    }
    for (const auto &v : m_state.vertices)
        if (v.weights.contains(boneId)) return fail("Vertices are weighted to this bone");
    if (!m_state.bones[i].keys.isEmpty() || isBoneAnimationBound(boneId))
        return fail("Remove the bone's animation first");
    State next = m_state; next.bones.removeAt(i);
    return commit(next, QStringLiteral("Remove bone"));
}
bool MeshRigDocument::renameBone(const QString &boneId, const QString &name) {
    const int i = boneIndex(boneId);
    if (m_painting || i < 0 || name.trimmed().isEmpty() || name.size() > 256) return fail("Invalid bone name");
    if (m_state.bones[i].name == name) return true;
    State next = m_state; next.bones[i].name = name;
    return commit(next, QStringLiteral("Rename bone"));
}
bool MeshRigDocument::setBoneParent(const QString &boneId, const QString &parentId) {
    const int i = boneIndex(boneId);
    if (m_painting || i < 0 || boneId == parentId || (!parentId.isEmpty() && boneIndex(parentId) < 0))
        return fail("Invalid bone or parent");
    if (m_state.bones[i].parentId == parentId) return true;
    // The new parent must not descend from the bone.
    for (QString at = parentId; !at.isEmpty(); at = m_state.bones[boneIndex(at)].parentId)
        if (at == boneId) return fail("A bone cannot be parented to its own descendant");
    State next = m_state; next.bones[i].parentId = parentId;
    if (!orderBones(&next.bones)) return fail("Bone hierarchy would contain a cycle");
    return commit(next, QStringLiteral("Reparent bone"));
}
bool MeshRigDocument::setBoneRest(const QString &boneId, const QVector3D &head, const QVector3D &tail) {
    const int i = boneIndex(boneId);
    if (m_painting || i < 0 || !finitePoint(head) || !finitePoint(tail) || (head - tail).lengthSquared() < 1e-12f)
        return fail("Invalid bone head or tail");
    if (m_state.bones[i].head == head && m_state.bones[i].tail == tail) return true;
    State next = m_state; next.bones[i].head = head; next.bones[i].tail = tail;
    return commit(next, QStringLiteral("Edit bone rest"));
}

} // namespace QindaTK
