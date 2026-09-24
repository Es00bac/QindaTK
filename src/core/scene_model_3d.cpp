// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scene_model_3d.h"
#include <QUuid>
#include <cmath>

namespace QindaTK {
namespace {
bool finite(const QVector3D &v) {
    return std::isfinite(v.x()) && std::isfinite(v.y()) && std::isfinite(v.z())
        && v.length() <= 1e6f;
}
}
int SceneModel3D::index(const QString &id) const {
    for (int i = 0; i < m_entities.size(); ++i) if (m_entities[i].id == id) return i;
    return -1;
}
SceneModel3D::Transform SceneModel3D::world(int i) const {
    const auto &e = m_entities[i];
    Transform t{e.position, e.rotation, e.scale};
    int p = index(e.parent);
    while (p >= 0) {
        const auto &a = m_entities[p];
        t.position = a.position + a.rotation.rotatedVector(t.position * a.scale);
        t.rotation = (a.rotation * t.rotation).normalized();
        t.scale *= a.scale;
        p = index(a.parent);
    }
    return t;
}
bool SceneModel3D::validWorld() const {
    for (int i = 0; i < m_entities.size(); ++i) {
        const auto t = world(i);
        if (!finite(t.position) || !std::isfinite(t.scale) || t.scale < 1e-6f || t.scale > 1e6f) return false;
    }
    return true;
}
bool SceneModel3D::effectivelyVisible(int i) const {
    while (i >= 0) {
        if (!m_entities[i].visible) return false;
        i = index(m_entities[i].parent);
    }
    return true;
}
QVariantMap SceneModel3D::worldTransform(const QString &id) const {
    int i = index(id);
    if (i < 0) return {};
    const auto t = world(i);
    return {{"position", QVariant::fromValue(t.position)},
            {"rotation", QVariant::fromValue(t.rotation)}, {"scale", t.scale}};
}
QVariantList SceneModel3D::entities() const {
    QVariantList out;
    for (int i = 0; i < m_entities.size(); ++i) {
        const auto &e = m_entities[i];
        const auto t = world(i);
        out.append(QVariantMap{{"entityId", e.id}, {"parentId", e.parent}, {"name", e.name},
            {"primitive", e.primitive}, {"position", QVariant::fromValue(e.position)},
            {"eulerDegrees", QVariant::fromValue(e.rotation.toEulerAngles())}, {"scale", e.scale},
            {"worldPosition", QVariant::fromValue(t.position)},
            {"worldRotation", QVariant::fromValue(t.rotation)}, {"worldScale", t.scale},
            {"color", e.color}, {"visible", effectivelyVisible(i)}});
    }
    return out;
}
bool SceneModel3D::fail(const QString &message) { m_error = message; emit errorChanged(); return false; }
void SceneModel3D::notify() { m_error.clear(); emit errorChanged(); emit changed(); }
void SceneModel3D::remember() {
    m_undo.append(m_entities);
    if (m_undo.size() > 64) m_undo.removeFirst();
    m_redo.clear();
}
void SceneModel3D::restore(const QVector<Entity> &state) {
    m_entities = state;
    if (!m_selected.isEmpty() && index(m_selected) < 0) { m_selected.clear(); emit selectionChanged(); }
    notify();
}
QString SceneModel3D::addEntity(const QString &name, const QString &primitive,
                              const QVector3D &position, const QColor &color) {
    if (editing() || m_entities.size() >= 512 || !finite(position) || !color.isValid()
        || (primitive != "cube" && primitive != "sphere" && primitive != "cylinder")) {
        fail("Invalid primitive, position, color, scene limit or active transform"); return {};
    }
    remember();
    Entity e;
    e.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    e.name = name; e.primitive = primitive; e.position = position; e.color = color;
    m_entities.append(e); notify(); return e.id;
}
bool SceneModel3D::select(const QString &id) {
    if (!id.isEmpty() && index(id) < 0) return fail("Unknown entity");
    if (m_selected != id) { m_selected = id; emit selectionChanged(); }
    return true;
}
bool SceneModel3D::removeEntity(const QString &id) {
    int i = index(id);
    if (i < 0 || editing()) return fail("Unknown entity or active transform");
    for (const auto &e : m_entities) if (e.parent == id) return fail("Reparent children before removal");
    remember(); m_entities.removeAt(i);
    if (m_selected == id) { m_selected.clear(); emit selectionChanged(); }
    notify(); return true;
}
bool SceneModel3D::setTransform(const QString &id, const QVector3D &position,
                              const QVector3D &eulerDegrees, float scale) {
    int i = index(id);
    if (i < 0 || editing() || !finite(position) || !finite(eulerDegrees)
        || !std::isfinite(scale) || scale < 0.001f || scale > 1000.f)
        return fail("Invalid transform or active transform");
    const auto before = m_entities;
    auto &e = m_entities[i]; e.position = position;
    e.rotation = QQuaternion::fromEulerAngles(eulerDegrees); e.scale = scale;
    if (!validWorld()) { m_entities = before; return fail("World transform exceeds supported range"); }
    const auto after = m_entities; m_entities = before; remember(); m_entities = after;
    notify(); return true;
}
bool SceneModel3D::setAppearance(const QString &id, const QColor &color, bool visible) {
    int i = index(id);
    if (i < 0 || editing() || !color.isValid()) return fail("Invalid appearance or active transform");
    remember(); m_entities[i].color = color; m_entities[i].visible = visible;
    notify(); return true;
}
bool SceneModel3D::reparent(const QString &id, const QString &parentId, bool keepWorld) {
    int i = index(id), p = index(parentId);
    if (i < 0 || editing() || (!parentId.isEmpty() && p < 0)) return fail("Invalid reparent request");
    int depth = 0;
    for (int a = p; a >= 0; a = index(m_entities[a].parent)) {
        if (a == i) return fail("Scene hierarchy cycle");
        if (++depth > 32) return fail("Scene hierarchy depth limit");
    }
    // Bound complete subtree depth as well as the new parent chain.
    for (int child = 0; child < m_entities.size(); ++child) {
        int d = 0, a = child;
        while (a >= 0 && a != i) { ++d; a = index(m_entities[a].parent); }
        if (a == i && d + depth > 32) return fail("Scene hierarchy depth limit");
    }
    const auto old = world(i);
    const auto parent = p < 0 ? Transform{} : world(p);
    const auto position = parent.rotation.conjugated().rotatedVector(old.position - parent.position) / parent.scale;
    const float scale = old.scale / parent.scale;
    if (keepWorld && (!finite(position) || !std::isfinite(scale) || scale < .001f || scale > 1000.f))
        return fail("Keep-world transform exceeds supported range");
    const auto before = m_entities;
    auto &e = m_entities[i]; e.parent = parentId;
    if (keepWorld) {
        e.position = position; e.rotation = (parent.rotation.conjugated() * old.rotation).normalized();
        e.scale = scale;
    }
    if (!validWorld()) { m_entities = before; return fail("World transform exceeds supported range"); }
    const auto after = m_entities; m_entities = before; remember(); m_entities = after;
    notify(); return true;
}
bool SceneModel3D::beginTransform(const QString &id) {
    int i = index(id);
    if (i < 0 || editing()) return fail("Unknown entity or transform already active");
    m_before = m_entities; m_editId = id; m_startWorld = world(i); notify(); return true;
}
bool SceneModel3D::previewWorldTranslation(const QVector3D &delta, float snap) {
    if (!editing() || !finite(delta) || !std::isfinite(snap) || snap < 0)
        return fail("Invalid translation preview");
    auto target = m_startWorld.position + delta;
    if (snap > 0) for (int a = 0; a < 3; ++a) target[a] = std::round(target[a] / snap) * snap;
    auto pose = m_startWorld; pose.position = target;
    return previewWorldTransform(pose);
}
bool SceneModel3D::previewWorldTransform(const Transform &target) {
    if (!finite(target.position) || !std::isfinite(target.scale)
        || !std::isfinite(target.rotation.scalar()) || !std::isfinite(target.rotation.x())
        || !std::isfinite(target.rotation.y()) || !std::isfinite(target.rotation.z()))
        return fail("Transform exceeds supported range");
    int i = index(m_editId), p = index(m_entities[i].parent);
    const auto parent = p < 0 ? Transform{} : world(p);
    auto local = parent.rotation.conjugated().rotatedVector(target.position - parent.position) / parent.scale;
    const float scale = target.scale / parent.scale;
    if (!finite(local) || !std::isfinite(scale) || scale < .001f || scale > 1000.f)
        return fail("Transform exceeds supported range");
    const auto previous = m_entities[i];
    m_entities[i].position = local;
    m_entities[i].rotation = (parent.rotation.conjugated() * target.rotation).normalized();
    m_entities[i].scale = scale;
    if (!validWorld()) { m_entities[i] = previous; return fail("World transform exceeds supported range"); }
    notify(); return true;
}
bool SceneModel3D::previewConstrainedTranslation(const QVector3D &delta,
        const QString &constraint, const QString &space, float snap) {
    if (!editing() || !finite(delta) || !std::isfinite(snap) || snap < 0
        || (space != "world" && space != "local")
        || (constraint != "free" && constraint != "x" && constraint != "y"
            && constraint != "z" && constraint != "xy" && constraint != "xz" && constraint != "yz"))
        return fail("Invalid constrained translation preview");
    if (constraint == "free") return previewWorldTranslation(delta, snap);
    const QQuaternion basis = space == "local" ? m_startWorld.rotation : QQuaternion();
    const QVector3D axes[3] = {basis.rotatedVector({1, 0, 0}),
                               basis.rotatedVector({0, 1, 0}), basis.rotatedVector({0, 0, 1})};
    QVector3D projected;
    const QString names[3] = {"x", "y", "z"};
    for (int a = 0; a < 3; ++a) {
        if (!constraint.contains(names[a])) continue;
        float component = QVector3D::dotProduct(delta, axes[a]);
        if (snap > 0) component = std::round(component / snap) * snap;
        projected += axes[a] * component;
    }
    auto pose = m_startWorld; pose.position += projected;
    return previewWorldTransform(pose);
}
bool SceneModel3D::previewAxisTranslation(float distance, const QString &axis,
        const QString &space, float snap) {
    if (!editing() || !std::isfinite(distance) || !std::isfinite(snap) || snap < 0
        || (space != "world" && space != "local")
        || (axis != "x" && axis != "y" && axis != "z"))
        return fail("Invalid axis translation preview");
    if (snap > 0) distance = std::round(distance / snap) * snap;
    const QVector3D unit = axis == "x" ? QVector3D(1, 0, 0)
        : axis == "y" ? QVector3D(0, 1, 0) : QVector3D(0, 0, 1);
    const auto worldAxis = space == "local" ? m_startWorld.rotation.rotatedVector(unit) : unit;
    auto pose = m_startWorld; pose.position += worldAxis * distance;
    return previewWorldTransform(pose);
}
bool SceneModel3D::previewRotation(float degrees, const QString &axis,
        const QString &space, float snapDegrees) {
    if (!editing() || !std::isfinite(degrees) || !std::isfinite(snapDegrees)
        || snapDegrees < 0 || (space != "world" && space != "local")
        || (axis != "x" && axis != "y" && axis != "z"))
        return fail("Invalid rotation preview");
    if (snapDegrees > 0) degrees = std::round(degrees / snapDegrees) * snapDegrees;
    if (!std::isfinite(degrees) || std::abs(degrees) > 1e6f)
        return fail("Rotation exceeds supported range");
    const QVector3D vector = axis == "x" ? QVector3D(1, 0, 0)
        : axis == "y" ? QVector3D(0, 1, 0) : QVector3D(0, 0, 1);
    const auto turn = QQuaternion::fromAxisAndAngle(vector, degrees);
    auto pose = m_startWorld;
    pose.rotation = space == "world" ? (turn * pose.rotation).normalized()
                                      : (pose.rotation * turn).normalized();
    return previewWorldTransform(pose);
}
bool SceneModel3D::previewUniformScale(float factor, float snap) {
    if (!editing() || !std::isfinite(factor) || factor <= 0
        || !std::isfinite(snap) || snap < 0) return fail("Invalid scale preview");
    auto pose = m_startWorld;
    pose.scale *= factor;
    if (snap > 0) pose.scale = std::round(pose.scale / snap) * snap;
    return previewWorldTransform(pose);
}
bool SceneModel3D::commitTransform() {
    if (!editing()) return false;
    if (m_entities != m_before) {
        m_undo.append(m_before); if (m_undo.size() > 64) m_undo.removeFirst();
        m_redo.clear();
    }
    m_before.clear(); m_editId.clear(); notify(); return true;
}
bool SceneModel3D::cancelTransform() {
    if (!editing()) return false;
    auto before = m_before; m_before.clear(); m_editId.clear(); restore(before); return true;
}
bool SceneModel3D::undo() {
    if (!canUndo()) return false;
    m_redo.append(m_entities); restore(m_undo.takeLast()); return true;
}
bool SceneModel3D::redo() {
    if (!canRedo()) return false;
    m_undo.append(m_entities); restore(m_redo.takeLast()); return true;
}
} // namespace QindaTK
