// SPDX-License-Identifier: LGPL-3.0-or-later
#include "mesh_rig_document.h"

#include <QQuaternion>
#include <QtMath>
#include <cmath>
#include <limits>

namespace QindaTK {

MeshRigDocument::RayHit MeshRigDocument::castRay(const QVector<QVector3D> &posed, const QVector3D &origin,
                                                 const QVector3D &direction) const {
    RayHit best; float nearest = std::numeric_limits<float>::max();
    for (int i = 0; i < m_state.faces.size(); ++i) {
        const auto &face = m_state.faces[i];
        const QVector3D a = posed[face.vertices[0]], b = posed[face.vertices[1]], c = posed[face.vertices[2]];
        const auto e1 = b - a, e2 = c - a;
        const auto p = QVector3D::crossProduct(direction, e2);
        const float det = QVector3D::dotProduct(e1, p);
        if (qAbs(det) < 1e-8f) continue;
        const float inv = 1.0f / det;
        const auto t = origin - a;
        const float u = QVector3D::dotProduct(t, p) * inv;
        if (u < -1e-5f || u > 1.00001f) continue;
        const auto q = QVector3D::crossProduct(t, e1);
        const float v = QVector3D::dotProduct(direction, q) * inv;
        if (v < -1e-5f || u + v > 1.00001f) continue;
        const float along = QVector3D::dotProduct(e2, q) * inv;
        if (along <= 1e-6f || along >= nearest) continue;
        nearest = along;
        best.face = i; best.distance = along;
        best.point = a * (1 - u - v) + b * u + c * v;
        best.uv = face.uv[0] * (1 - u - v) + face.uv[1] * u + face.uv[2] * v;
    }
    return best;
}
QVariantMap MeshRigDocument::pickFace(const QVector3D &origin, const QVector3D &direction) const {
    if (!finitePoint(origin) || !finitePoint(direction) || direction.lengthSquared() < 1e-12f) return {};
    const RayHit hit = castRay(posedPositions(), origin, direction);
    if (hit.face < 0) return {};
    return {{"faceId", m_state.faces[hit.face].id}, {"uv", hit.uv},
            {"position", QVariant::fromValue(hit.point)}, {"distance", double(hit.distance)}};
}
bool MeshRigDocument::perspectiveRay(const QVector3D &position, const QVector3D &euler, qreal fov,
                                     const QSizeF &viewport, const QPointF &point,
                                     QVector3D *origin, QVector3D *direction) {
    if (!(viewport.width() > 0) || !(viewport.height() > 0) || !finitePoint(position)
        || !finitePoint(euler) || !(fov > 0 && fov < 180)
        || !std::isfinite(point.x()) || !std::isfinite(point.y())) return false;
    // AGENT-CONTRACT: SceneView3DRenderer's PerspectiveCamera binds the same
    // position, eulerRotation and fieldOfView (vertical orientation, the Qt
    // Quick 3D default), so this ray passes through the pixel the GPU draws.
    const double tangent = std::tan(qDegreesToRadians(fov) / 2);
    const double aspect = viewport.width() / viewport.height();
    const double nx = 2 * point.x() / viewport.width() - 1;
    const double ny = 1 - 2 * point.y() / viewport.height();
    const QVector3D local(float(nx * tangent * aspect), float(ny * tangent), -1.0f);
    *origin = position;
    *direction = QQuaternion::fromEulerAngles(euler).rotatedVector(local).normalized();
    return true;
}
QVariantMap MeshRigDocument::cameraRay(const QVector3D &position, const QVector3D &euler, qreal fov,
                                       const QSizeF &viewport, const QPointF &point) const {
    QVector3D origin, direction;
    if (!perspectiveRay(position, euler, fov, viewport, point, &origin, &direction)) return {};
    return {{"origin", QVariant::fromValue(origin)}, {"direction", QVariant::fromValue(direction)}};
}

bool MeshRigDocument::projectPoint(const QVector3D &position, const QVector3D &euler, qreal fov,
                                   const QSizeF &viewport, const QVector3D &world, QPointF *pixel) {
    if (!(viewport.width() > 0) || !(viewport.height() > 0) || !finitePoint(position)
        || !finitePoint(euler) || !(fov > 0 && fov < 180) || !finitePoint(world)) return false;
    // Exactly inverts perspectiveRay: camera-local point, then the same
    // tangent/aspect normalisation, so projected overlays sit on the pixels
    // that meshRay turns back into the same surface point.
    const QVector3D local = QQuaternion::fromEulerAngles(euler).inverted().rotatedVector(world - position);
    if (local.z() > -1e-4f) return false; // behind or on the camera plane
    const double tangent = std::tan(qDegreesToRadians(fov) / 2);
    const double aspect = viewport.width() / viewport.height();
    const double nx = double(local.x()) / (-double(local.z()) * tangent * aspect);
    const double ny = double(local.y()) / (-double(local.z()) * tangent);
    *pixel = QPointF((nx + 1) / 2 * viewport.width(), (1 - ny) / 2 * viewport.height());
    return true;
}
QVariantMap MeshRigDocument::cameraOverlay(const QVector3D &position, const QVector3D &euler, qreal fov,
                                           const QSizeF &viewport, const QString &faceId) const {
    QVariantList face, bones;
    const auto project = [&](const QVector3D &world, QPointF *pixel) {
        return projectPoint(position, euler, fov, viewport, world, pixel);
    };
    if (const int f = faceIndex(faceId); f >= 0) {
        const auto posed = posedPositions();
        QVariantList points;
        for (int k = 0; k < 3; ++k) {
            QPointF pixel;
            if (!project(posed[m_state.faces[f].vertices[k]], &pixel)) { points.clear(); break; }
            points.append(pixel);
        }
        face = points;
    }
    const auto transforms = boneTransforms();
    for (int i = 0; i < m_state.bones.size(); ++i) {
        QPointF head, tail;
        const auto &b = m_state.bones[i];
        if (!project(transforms[i].map(b.head), &head) || !project(transforms[i].map(b.tail), &tail)) continue;
        bones.append(QVariantMap{{"boneId", b.id}, {"head", head}, {"tail", tail}});
    }
    return {{"face", face}, {"bones", bones}};
}

} // namespace QindaTK
