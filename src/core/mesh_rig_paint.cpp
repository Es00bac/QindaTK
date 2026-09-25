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

bool MeshRigDocument::beginPaint(const QString &channel) {
    if (m_painting) return fail("Paint stroke already active");
    const int c = channelIndex(channel);
    if (c < 0) return fail("Unknown texture channel");
    m_beforePaint = m_state; m_painting = true; m_paintChanged = false; m_paintChannel = c;
    m_paintSamples = 0; m_paintPixelWork = 0; m_hasPreviousRay = false; m_hasPreviousUv = false;
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
        // AGENT-GUARD (bounded work): only the dab's footprint on this face is
        // scanned, never the face's whole UV box. The sphere cuts the face
        // plane in a disk of radius sqrt(reach^2 - h^2); the face's affine
        // plane-to-texel map A sends that disk into a box of half extents
        // r * |row of A|. A long stroke on a large texture therefore costs
        // about (2 r + 2)^2 texels per dab and face, not w x h.
        const QVector3D P0 = posed[f.vertices[0]], e1 = posed[f.vertices[1]] - P0, e2 = posed[f.vertices[2]] - P0;
        QVector3D n = QVector3D::crossProduct(e1, e2);
        if (n.lengthSquared() < 1e-12f) continue;
        n.normalize();
        const QVector3D toCenter = hit.point - P0;
        const double h = QVector3D::dotProduct(toCenter, n);
        if (qAbs(h) >= reach) continue;
        const double disk = std::sqrt(reach * reach - h * h);
        const QVector3D ua = e1.normalized(), va = QVector3D::crossProduct(n, ua);
        const double E00 = QVector3D::dotProduct(e1, ua), E01 = QVector3D::dotProduct(e2, ua);
        const double E11 = QVector3D::dotProduct(e2, va); // e1 . va is zero
        if (qAbs(E00 * E11) < 1e-12) continue;
        // Plane (s, t) -> barycentric (a, b) is E^-1 with E = [[E00, E01], [0, E11]].
        const double I00 = 1 / E00, I01 = -E01 / (E00 * E11), I11 = 1 / E11;
        const double D00 = U[1] - U[0], D01 = U[2] - U[0], D10 = V[1] - V[0], D11 = V[2] - V[0];
        const double A00 = D00 * I00, A01 = D00 * I01 + D01 * I11;
        const double A10 = D10 * I00, A11 = D10 * I01 + D11 * I11;
        const double sc = QVector3D::dotProduct(toCenter, ua), tc = QVector3D::dotProduct(toCenter, va);
        const double cu = U[0] + A00 * sc + A01 * tc, cv = V[0] + A10 * sc + A11 * tc;
        const double hu = disk * std::hypot(A00, A01), hv = disk * std::hypot(A10, A11);
        const int x0 = std::max(0, int(std::floor(std::max(std::min({U[0], U[1], U[2]}), cu - hu))));
        const int x1 = std::min(width - 1, int(std::ceil(std::min(std::max({U[0], U[1], U[2]}), cu + hu))));
        const int y0 = std::max(0, int(std::floor(std::max(std::min({V[0], V[1], V[2]}), cv - hv))));
        const int y1 = std::min(height - 1, int(std::ceil(std::min(std::max({V[0], V[1], V[2]}), cv + hv))));
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
    if (!coverage.isEmpty()) m_paintChanged = true;
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
    // AGENT-GUARD: a sample over the stroke's work bound is refused, but the
    // stroke and every pixel it already painted stay; endPaint commits them.
    if (!ok) { const QString reason = m_error; emit changed(); return fail(reason); }
    m_previousOrigin = origin; m_previousDirection = unitDirection; m_previousPoint = hit.point;
    m_hasPreviousRay = true; m_hasPreviousUv = false;
    notify(); return true;
}
QColor MeshRigDocument::texel(const QString &channel, int x, int y) const {
    const int c = channelIndex(channel);
    if (c < 0 || !m_state.textures[c].valid(x, y)) return {};
    return QColor::fromRgba(qUnpremultiply(m_state.textures[c].pixel(x, y)));
}
int MeshRigDocument::faceAtUv(const QPointF &uv, double *l0, double *l1, double *l2) const {
    for (int i = 0; i < m_state.faces.size(); ++i) {
        const QPointF *t = m_state.faces[i].uv;
        const double denom = (t[1].y() - t[2].y()) * (t[0].x() - t[2].x()) + (t[2].x() - t[1].x()) * (t[0].y() - t[2].y());
        if (qAbs(denom) < 1e-15) continue;
        const double a = ((t[1].y() - t[2].y()) * (uv.x() - t[2].x()) + (t[2].x() - t[1].x()) * (uv.y() - t[2].y())) / denom;
        const double b = ((t[2].y() - t[0].y()) * (uv.x() - t[2].x()) + (t[0].x() - t[2].x()) * (uv.y() - t[2].y())) / denom;
        if (a < -1e-9 || b < -1e-9 || 1 - a - b < -1e-9) continue;
        *l0 = a; *l1 = b; *l2 = 1 - a - b;
        return i; // the first face in document order wins where UVs overlap
    }
    return -1;
}
// AGENT-NOTE (D-179): texture-space painting. The dab is centred on the
// surface point whose UV is under the pointer, so TexturePaintView paints the
// texel it shows; the dab is still a surface sphere, so it crosses seams.
bool MeshRigDocument::paintUv(const QPointF &uv, const QColor &color, int radius, qreal opacity, qreal hardness) {
    if (!m_painting || !validUv(uv) || !color.isValid() || radius < 1 || radius > 128
        || !std::isfinite(opacity) || opacity <= 0 || opacity > 1
        || !std::isfinite(hardness) || hardness < 0 || hardness > 1)
        return fail("Invalid paint position or brush");
    QColor source = color.toRgb();
    if (m_paintChannel != BaseColor) {
        const int gray = qGray(source.rgb());
        source = QColor(gray, gray, gray, source.alpha());
    }
    const auto posed = posedPositions();
    const auto hitAt = [&](const QPointF &at) {
        RayHit hit; double l0 = 0, l1 = 0, l2 = 0;
        hit.face = faceAtUv(at, &l0, &l1, &l2);
        if (hit.face < 0) return hit;
        const Face &f = m_state.faces[hit.face];
        hit.point = posed[f.vertices[0]] * float(l0) + posed[f.vertices[1]] * float(l1) + posed[f.vertices[2]] * float(l2);
        hit.uv = at;
        return hit;
    };
    const RayHit hit = hitAt(uv);
    if (hit.face < 0) { m_hasPreviousUv = false; return fail("No UV island under the paint position"); }
    bool ok = true;
    if (m_hasPreviousUv) {
        // Samples join in texture space at the raster spacing (texels).
        const double du = (uv.x() - m_previousUv.x()) * textureWidth();
        const double dv = (uv.y() - m_previousUv.y()) * textureHeight();
        const qint64 count = qint64(std::ceil(std::hypot(du, dv) / std::max(1.0, StrokeSpacing * radius)));
        if (count > MaximumStrokeDabs) { ok = false; m_error = QStringLiteral("Paint stroke work limit reached"); }
        for (qint64 i = 1; ok && i < count; ++i) {
            const double t = double(i) / double(count);
            const RayHit between = hitAt(m_previousUv + (uv - m_previousUv) * t);
            if (between.face >= 0) ok = dab(posed, between, source, radius, opacity, hardness);
        }
    }
    if (ok) ok = dab(posed, hit, source, radius, opacity, hardness);
    if (!ok) { const QString reason = m_error; emit changed(); return fail(reason); }
    m_previousUv = uv; m_hasPreviousUv = true; m_hasPreviousRay = false;
    notify(); return true;
}
bool MeshRigDocument::endPaint() {
    if (!m_painting) return fail("No paint stroke active");
    m_painting = false; m_hasPreviousRay = false; m_hasPreviousUv = false;
    if (!m_paintChanged) { m_beforePaint = {}; notify(); return true; }
    const State after = m_state;
    m_state = m_beforePaint; m_beforePaint = {};
    return commit(after, QStringLiteral("Paint texture"));
}
void MeshRigDocument::cancelPaint() {
    if (!m_painting) return;
    m_state = m_beforePaint; m_beforePaint = {}; m_painting = false;
    m_hasPreviousRay = false; m_hasPreviousUv = false;
    notify();
}
bool MeshRigDocument::exportTexturePng(const QString &path, const QString &channel) const {
    const int c = channelIndex(channel);
    if (c < 0) return fail("Unknown texture channel");
    QSaveFile file(path); if (!file.open(QIODevice::WriteOnly)) return fail(file.errorString());
    return m_state.textures[c].save(&file, "PNG") && file.commit();
}

} // namespace QindaTK
