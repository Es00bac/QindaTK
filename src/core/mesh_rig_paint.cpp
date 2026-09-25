// SPDX-License-Identifier: LGPL-3.0-or-later
#include "mesh_rig_document.h"
#include "raster_brush_p.h"

#include <QBuffer>
#include <QColorSpace>
#include <QHash>
#include <QQuaternion>
#include <QSaveFile>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
constexpr int MaximumStrokeDabs = 8192;
constexpr qint64 MaximumStrokeTexelWork = 16000000;
// Dab spacing along a stroke, as a fraction of the brush radius; the same
// default as TiledRasterDocument's preset (spacing 0.2 of the diameter / 2).
constexpr double StrokeSpacing = 0.2;
const QString ChannelNames[MeshRigDocument::ChannelCount] = {
    QStringLiteral("baseColor"), QStringLiteral("roughness"), QStringLiteral("metalness")};

struct Vec { double x, y, z; };
Vec vec(const QVector3D &v) { return {v.x(), v.y(), v.z()}; }
double distance(const Vec &a, const Vec &b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}
// World units per texel on one face: sqrt(surface area / UV area in texels).
// Zero when the face has no UV or surface area.
double texelWorldSize(const QVector3D p[3], const QPointF uv[3], int width, int height) {
    const double surface = 0.5 * double(QVector3D::crossProduct(p[1] - p[0], p[2] - p[0]).length());
    const double texels = 0.5 * qAbs((uv[1].x() - uv[0].x()) * (uv[2].y() - uv[0].y())
                                     - (uv[2].x() - uv[0].x()) * (uv[1].y() - uv[0].y())) * width * height;
    return texels < 1e-12 || surface < 1e-12 ? 0.0 : std::sqrt(surface / texels);
}
QString pngDataUrl(const QImage &image) {
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}
}

QStringList MeshRigDocument::textureChannels() {
    return {ChannelNames[0], ChannelNames[1], ChannelNames[2]};
}
int MeshRigDocument::channelIndex(const QString &channel) {
    for (int c = 0; c < ChannelCount; ++c) if (ChannelNames[c] == channel) return c;
    return -1;
}
QImage MeshRigDocument::blankChannel(int channel, int width, int height) {
    // AGENT-NOTE (D-156): baseColor is sRGB colour; roughness and metalness
    // are linear scalars stored as gray and read from the red component.
    // Defaults reproduce the pre-channel material: white, roughness 0.8, metal 0.
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    if (channel == BaseColor) {
        image.fill(Qt::white);
        image.setColorSpace(QColorSpace::SRgb);
    } else {
        image.fill(channel == Roughness ? qRgb(204, 204, 204) : qRgb(0, 0, 0));
    }
    return image;
}
int MeshRigDocument::textureWidth() const { return m_state.textures[BaseColor].width(); }
int MeshRigDocument::textureHeight() const { return m_state.textures[BaseColor].height(); }
QImage MeshRigDocument::texture(const QString &channel) const {
    const int c = channelIndex(channel);
    return c < 0 ? QImage() : m_state.textures[c];
}
QString MeshRigDocument::textureDataUrl() const { return pngDataUrl(m_state.textures[BaseColor]); }
QString MeshRigDocument::channelDataUrl(const QString &channel) const {
    const int c = channelIndex(channel);
    return c < 0 ? QString() : pngDataUrl(m_state.textures[c]);
}
QVariantMap MeshRigDocument::textureDataUrls() const {
    QVariantMap result;
    for (int c = 0; c < ChannelCount; ++c) result.insert(ChannelNames[c], pngDataUrl(m_state.textures[c]));
    return result;
}
bool MeshRigDocument::resizeTexture(int width, int height) {
    if (m_painting || width < 1 || height < 1 || width > 1024 || height > 1024)
        return fail("Texture size must be 1 to 1024 pixels per side");
    if (width == textureWidth() && height == textureHeight()) return true;
    State next = m_state;
    for (int c = 0; c < ChannelCount; ++c) next.textures[c] = blankChannel(c, width, height);
    return commit(next, QStringLiteral("Resize texture"));
}

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

bool MeshRigDocument::beginPaint(const QString &channel) {
    if (m_painting) return fail("Paint stroke already active");
    const int c = channelIndex(channel);
    if (c < 0) return fail("Unknown texture channel");
    m_beforePaint = m_state; m_painting = true; m_paintChanged = false; m_paintChannel = c;
    m_paintSamples = 0; m_paintPixelWork = 0; m_hasPreviousRay = false;
    notify(); return true;
}
// AGENT-NOTE (D-154): a dab is a sphere of the brush radius on the posed
// surface, not a disk in texture space. Every face within reach paints the
// texels whose centre maps back inside that sphere, so one dab continues
// across a UV seam into the neighbouring island. Coverage and compositing are
// the raster pipeline's kernel (raster_brush_p.h).
bool MeshRigDocument::dab(const QVector<QVector3D> &posed, const RayHit &hit, const QColor &color,
                          int radius, qreal opacity, qreal hardness) {
    QImage &image = m_state.textures[m_paintChannel];
    const int width = image.width(), height = image.height();
    const Face &hitFace = m_state.faces[hit.face];
    const QVector3D hitPoints[3] = {posed[hitFace.vertices[0]], posed[hitFace.vertices[1]],
                                    posed[hitFace.vertices[2]]};
    const double texel = texelWorldSize(hitPoints, hitFace.uv, width, height);
    if (texel <= 0) return true; // nothing addressable under this sample
    if (++m_paintSamples > MaximumStrokeDabs) { m_error = QStringLiteral("Paint stroke work limit reached"); return false; }
    const double reach = radius * texel;
    const Vec center = vec(hit.point);
    QHash<int, qreal> coverage;
    for (const Face &f : m_state.faces) {
        const Vec p[3] = {vec(posed[f.vertices[0]]), vec(posed[f.vertices[1]]), vec(posed[f.vertices[2]])};
        if (center.x < std::min({p[0].x, p[1].x, p[2].x}) - reach || center.x > std::max({p[0].x, p[1].x, p[2].x}) + reach
            || center.y < std::min({p[0].y, p[1].y, p[2].y}) - reach || center.y > std::max({p[0].y, p[1].y, p[2].y}) + reach
            || center.z < std::min({p[0].z, p[1].z, p[2].z}) - reach || center.z > std::max({p[0].z, p[1].z, p[2].z}) + reach)
            continue;
        // Texel (x, y) has its centre at UV ((x + 0.5) / w, 1 - (y + 0.5) / h).
        double U[3], V[3];
        for (int k = 0; k < 3; ++k) { U[k] = f.uv[k].x() * width; V[k] = (1 - f.uv[k].y()) * height; }
        const double denom = (V[1] - V[2]) * (U[0] - U[2]) + (U[2] - U[1]) * (V[0] - V[2]);
        if (qAbs(denom) < 1e-12) continue;
        const int x0 = std::max(0, int(std::floor(std::min({U[0], U[1], U[2]}))));
        const int x1 = std::min(width - 1, int(std::ceil(std::max({U[0], U[1], U[2]}))));
        const int y0 = std::max(0, int(std::floor(std::min({V[0], V[1], V[2]}))));
        const int y1 = std::min(height - 1, int(std::ceil(std::max({V[0], V[1], V[2]}))));
        if (x1 < x0 || y1 < y0) continue;
        m_paintPixelWork += qint64(x1 - x0 + 1) * (y1 - y0 + 1);
        if (m_paintPixelWork > MaximumStrokeTexelWork) {
            m_error = QStringLiteral("Paint stroke work limit reached"); return false;
        }
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) {
            const double px = x + 0.5, py = y + 0.5;
            const double l0 = ((V[1] - V[2]) * (px - U[2]) + (U[2] - U[1]) * (py - V[2])) / denom;
            const double l1 = ((V[2] - V[0]) * (px - U[2]) + (U[0] - U[2]) * (py - V[2])) / denom;
            const double l2 = 1 - l0 - l1;
            if (l0 < -1e-9 || l1 < -1e-9 || l2 < -1e-9) continue;
            const Vec q{p[0].x * l0 + p[1].x * l1 + p[2].x * l2, p[0].y * l0 + p[1].y * l1 + p[2].y * l2,
                        p[0].z * l0 + p[1].z * l1 + p[2].z * l2};
            const double d = distance(q, center) / reach;
            if (d >= 1.0) continue;
            const qreal alpha = qBound<qreal>(0, opacity * RasterBrush::edgeCoverage(d, hardness) * color.alphaF(), 1);
            if (alpha <= 0) continue;
            // A texel reached through two faces (a shared edge or overlapping
            // UVs) is composited once, at its strongest coverage.
            const int index = y * width + x;
            coverage.insert(index, std::max(coverage.value(index, 0.0), alpha));
        }
    }
    QList<int> order = coverage.keys();
    std::sort(order.begin(), order.end());
    for (int index : order) {
        QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(index / width));
        row[index % width] = RasterBrush::blendPremultiplied(row[index % width], color, coverage.value(index));
    }
    return true;
}
bool MeshRigDocument::paintRay(const QVector3D &origin, const QVector3D &direction,
                               const QColor &color, int radius, qreal opacity, qreal hardness) {
    if (!m_painting || !finitePoint(origin) || !finitePoint(direction)
        || direction.lengthSquared() < 1e-12f || !color.isValid()
        || radius < 1 || radius > 128 || !std::isfinite(opacity) || opacity <= 0 || opacity > 1
        || !std::isfinite(hardness) || hardness < 0 || hardness > 1)
        return fail("Invalid paint ray or brush");
    QColor source = color.toRgb();
    if (m_paintChannel != BaseColor) { // scalar channels paint the colour's gray value
        const int gray = qGray(source.rgb());
        source = QColor(gray, gray, gray, source.alpha());
    }
    const auto posed = posedPositions();
    const RayHit hit = castRay(posed, origin, direction);
    if (hit.face < 0) { m_hasPreviousRay = false; return fail("Paint ray missed the mesh"); }
    const Face &face = m_state.faces[hit.face];
    const QVector3D facePoints[3] = {posed[face.vertices[0]], posed[face.vertices[1]], posed[face.vertices[2]]};
    const double texel = texelWorldSize(facePoints, face.uv, textureWidth(), textureHeight());
    if (texel <= 0) return fail("The painted face has no UV area");
    const QVector3D unitDirection = direction.normalized();
    bool ok = true;
    if (m_hasPreviousRay) {
        // Camera-aware spacing: interpolate the camera rays between samples
        // and re-cast each one, so dabs follow the surface, not a chord.
        const double step = std::max(texel, StrokeSpacing * radius * texel);
        const double gap = (hit.point - m_previousPoint).length();
        const qint64 count = qint64(std::ceil(gap / step));
        if (count > MaximumStrokeDabs) {
            ok = false; m_error = QStringLiteral("Paint stroke work limit reached");
        }
        for (qint64 i = 1; ok && i < count; ++i) {
            const float t = float(double(i) / double(count));
            const QVector3D o = m_previousOrigin + (origin - m_previousOrigin) * t;
            const QVector3D d = m_previousDirection + (unitDirection - m_previousDirection) * t;
            if (d.lengthSquared() < 1e-12f) continue;
            const RayHit between = castRay(posed, o, d);
            if (between.face >= 0) ok = dab(posed, between, source, radius, opacity, hardness);
        }
    }
    if (ok) ok = dab(posed, hit, source, radius, opacity, hardness);
    if (!ok) { const QString reason = m_error; cancelPaint(); return fail(reason); }
    m_previousOrigin = origin; m_previousDirection = unitDirection; m_previousPoint = hit.point;
    m_hasPreviousRay = true; m_paintChanged = true;
    notify(); return true;
}
bool MeshRigDocument::endPaint() {
    if (!m_painting) return fail("No paint stroke active");
    m_painting = false; m_hasPreviousRay = false;
    if (!m_paintChanged) { m_beforePaint = {}; notify(); return true; }
    const State after = m_state;
    m_state = m_beforePaint; m_beforePaint = {};
    return commit(after, QStringLiteral("Paint texture"));
}
void MeshRigDocument::cancelPaint() {
    if (!m_painting) return;
    m_state = m_beforePaint; m_beforePaint = {}; m_painting = false; m_hasPreviousRay = false;
    notify();
}
bool MeshRigDocument::exportTexturePng(const QString &path, const QString &channel) const {
    const int c = channelIndex(channel);
    if (c < 0) return fail("Unknown texture channel");
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    return m_state.textures[c].save(&file, "PNG") && file.commit();
}

} // namespace QindaTK
