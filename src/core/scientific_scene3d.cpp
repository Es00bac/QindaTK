// SPDX-License-Identifier: LGPL-3.0-or-later
#include "scientific_scene3d.h"
#include "scientific_sampling.h"

#include <QVector4D>
#include <algorithm>
#include <cmath>
#include <limits>

namespace QindaTK {
namespace {
constexpr double Degrees = 3.14159265358979323846 / 180.0;
double normalized(double v, double lo, double hi) { return 2 * (v - lo) / (hi - lo) - 1; }
}

ScientificScene3D::ScientificScene3D(QObject *parent) : QObject(parent)
{
    connect(this, &ScientificScene3D::styleChanged, this, [this] { invalidate(); });
    for (auto signal : {&ScientificScene3D::changed, &ScientificScene3D::cameraChanged,
                        &ScientificScene3D::meshChanged, &ScientificScene3D::selectionChanged})
        connect(this, signal, this, &ScientificScene3D::summaryChanged);
}

void ScientificScene3D::invalidate()
{
    m_dirty = true; ++m_revision;
    if (!m_selectedPoint.isEmpty() && (!m_points || m_points->indexOf(m_selectedPoint) < 0)) {
        m_selectedPoint.clear(); emit selectionChanged();
    }
    emit meshChanged();
}
void ScientificScene3D::setGrid(ScientificGrid *value)
{
    if (m_grid == value) return;
    if (m_grid) disconnect(m_grid, nullptr, this, nullptr);
    m_grid = value;
    if (value) {
        connect(value, &ScientificGrid::changed, this, [this] { invalidate(); emit changed(); });
        connect(value, &QObject::destroyed, this, [this] { invalidate(); emit changed(); });
    }
    invalidate(); emit changed();
}
void ScientificScene3D::setPoints(ScientificDataset *value)
{
    if (m_points == value) return;
    if (m_points) disconnect(m_points, nullptr, this, nullptr);
    m_points = value;
    if (value) {
        connect(value, &ScientificDataset::changed, this, [this] { invalidate(); emit changed(); });
        connect(value, &QObject::destroyed, this, [this] { invalidate(); emit changed(); });
    }
    invalidate(); emit changed();
}
void ScientificScene3D::setInterpolation(ScientificGrid::Interpolation value)
{ if (m_interpolation == value) return; m_interpolation = value; invalidate(); emit changed(); }
void ScientificScene3D::setResolutionX(int value)
{ value = std::clamp(value, 2, ResolutionLimit); if (m_resolutionX == value) return; m_resolutionX = value; invalidate(); emit changed(); }
void ScientificScene3D::setResolutionY(int value)
{ value = std::clamp(value, 2, ResolutionLimit); if (m_resolutionY == value) return; m_resolutionY = value; invalidate(); emit changed(); }
void ScientificScene3D::setPointLimit(int value)
{ value = std::clamp(value, 16, PointLimitMaximum); if (m_pointLimit == value) return; m_pointLimit = value; invalidate(); emit changed(); }
void ScientificScene3D::setDomain(int index, double value)
{
    if (!std::isfinite(value) || m_domain[index] == value) return;
    m_domain[index] = value; invalidate(); emit changed();
}
void ScientificScene3D::setAzimuth(double value)
{
    if (!std::isfinite(value)) return;
    value = std::fmod(value, 360.0); if (value < 0) value += 360;
    if (m_azimuth == value) return;
    m_azimuth = value; emit cameraChanged();
}
void ScientificScene3D::setElevation(double value)
{
    // AGENT-GUARD: +-89 degrees keeps the Euler camera away from gimbal lock
    // where yaw would stop turning the view.
    if (!std::isfinite(value)) return;
    value = std::clamp(value, -89.0, 89.0);
    if (m_elevation == value) return;
    m_elevation = value; emit cameraChanged();
}
void ScientificScene3D::setDistance(double value)
{
    if (!std::isfinite(value)) return;
    value = std::clamp(value, sceneExtent() * 0.5, sceneExtent() * 100);
    if (m_distance == value) return;
    m_distance = value; emit cameraChanged();
}
void ScientificScene3D::setFieldOfView(double value)
{
    if (!std::isfinite(value)) return;
    value = std::clamp(value, 10.0, 120.0);
    if (m_fieldOfView == value) return;
    m_fieldOfView = value; emit cameraChanged();
}
void ScientificScene3D::orbit(double az, double el) { setAzimuth(m_azimuth + az); setElevation(m_elevation + el); }
void ScientificScene3D::dolly(double factor) { if (factor > 0) setDistance(m_distance * factor); }

QVector3D ScientificScene3D::cameraPosition() const
{
    const double az = m_azimuth * Degrees, el = m_elevation * Degrees;
    return QVector3D(float(m_distance * std::cos(el) * std::sin(az)), float(m_distance * std::sin(el)),
                     float(m_distance * std::cos(el) * std::cos(az)));
}
QQuaternion ScientificScene3D::cameraRotation() const
{ return QQuaternion::fromEulerAngles(float(-m_elevation), float(m_azimuth), 0.0f); }
QMatrix4x4 ScientificScene3D::viewMatrix() const
{
    QMatrix4x4 camera;
    camera.translate(cameraPosition());
    camera.rotate(cameraRotation());
    return camera.inverted();
}
QMatrix4x4 ScientificScene3D::projectionMatrix(double aspect) const
{
    QMatrix4x4 m;
    m.perspective(float(m_fieldOfView), float(aspect), float(clipNear()), float(clipFar()));
    return m;
}

QString ScientificScene3D::problem() const
{
    if (!(m_domain[1] > m_domain[0]) || !(m_domain[3] > m_domain[2]) || !(m_domain[5] > m_domain[4]))
        return tr("Each axis needs minimum < maximum");
    if (m_points && m_points->dimensions() != 3) return tr("Points need a 3D dataset (dimensions: 3)");
    if (m_grid && m_points && (m_grid->xUnit() != m_points->xUnit() || m_grid->yUnit() != m_points->yUnit()
                               || m_grid->zUnit() != m_points->zUnit()))
        return tr("Surface and points must share X, Y and Z units");
    return {};
}

QVector3D ScientificScene3D::toScene(double x, double y, double z) const
{
    const double e = sceneExtent();
    return QVector3D(float(normalized(x, m_domain[0], m_domain[1]) * e), float(normalized(z, m_domain[4], m_domain[5]) * e),
                     float(-normalized(y, m_domain[2], m_domain[3]) * e));
}
QVariantMap ScientificScene3D::fromScene(const QVector3D &s) const
{
    const double e = sceneExtent();
    auto back = [](double n, double lo, double hi) { return lo + (n + 1) * 0.5 * (hi - lo); };
    return {{QStringLiteral("x"), back(s.x() / e, m_domain[0], m_domain[1])},
            {QStringLiteral("y"), back(-s.z() / e, m_domain[2], m_domain[3])},
            {QStringLiteral("z"), back(s.y() / e, m_domain[4], m_domain[5])}};
}
QVector3D ScientificScene3D::projectScene(const QVector3D &scene, const QSizeF &size, bool *visible) const
{
    const QVector4D clip = projectionMatrix(size.width() / std::max(1.0, size.height()))
        * viewMatrix() * QVector4D(scene, 1.0f);
    *visible = clip.w() > 0;
    if (!*visible) return {};
    const QVector3D ndc = clip.toVector3D() / clip.w();
    *visible = ndc.z() >= -1 && ndc.z() <= 1;
    return QVector3D(float((ndc.x() + 1) * 0.5 * size.width()), float((1 - ndc.y()) * 0.5 * size.height()), ndc.z());
}
QVariantMap ScientificScene3D::project(double x, double y, double z, double width, double height) const
{
    if (!(width > 0) || !(height > 0)) return {};
    bool visible = false;
    const QVector3D p = projectScene(toScene(x, y, z), QSizeF(width, height), &visible);
    return {{QStringLiteral("x"), p.x()}, {QStringLiteral("y"), p.y()}, {QStringLiteral("depth"), p.z()},
            {QStringLiteral("visible"), visible}};
}

bool ScientificScene3D::fitDomain()
{
    constexpr double inf = std::numeric_limits<double>::infinity();
    double d[6] = {inf, -inf, inf, -inf, inf, -inf};
    if (m_grid && m_grid->hasValues()) {
        d[0] = m_grid->xs().first(); d[1] = m_grid->xs().last();
        d[2] = m_grid->ys().first(); d[3] = m_grid->ys().last();
        d[4] = m_grid->zMinimum(); d[5] = m_grid->zMaximum();
    }
    if (m_points && m_points->dimensions() == 3)
        for (const ScientificSample &s : m_points->samples()) {
            if (s.missingY) continue;
            d[0] = std::min(d[0], s.x); d[1] = std::max(d[1], s.x);
            d[2] = std::min(d[2], s.y); d[3] = std::max(d[3], s.y);
            d[4] = std::min(d[4], s.z); d[5] = std::max(d[5], s.z);
        }
    for (int i = 0; i < 6; i += 2) {
        if (!std::isfinite(d[i]) || !std::isfinite(d[i + 1])) return false;
        // A flat axis gets a unit span so the cube stays well defined.
        if (!(d[i + 1] > d[i])) { d[i] -= 0.5; d[i + 1] += 0.5; }
    }
    std::copy(std::begin(d), std::end(d), std::begin(m_domain));
    invalidate(); emit changed();
    return true;
}

QColor ScientificScene3D::rampColor(double z) const
{
    if (m_surfaceColors.isEmpty()) return m_frameColor;
    if (m_surfaceColors.size() == 1) return m_surfaceColors.first();
    const double t = std::clamp((z - m_domain[4]) / (m_domain[5] - m_domain[4]), 0.0, 1.0);
    const double position = t * (m_surfaceColors.size() - 1);
    const int i = std::min(int(position), int(m_surfaceColors.size()) - 2);
    const double f = position - i;
    const QColor a = m_surfaceColors[i], b = m_surfaceColors[i + 1];
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * f), float(a.greenF() + (b.greenF() - a.greenF()) * f),
                            float(a.blueF() + (b.blueF() - a.blueF()) * f));
}

void ScientificScene3D::ensureMesh() const
{
    if (!m_dirty) return;
    m_dirty = false;
    m_mesh = {}; m_latticeX.clear(); m_latticeY.clear(); m_latticeZ.clear(); m_holes = 0;
    m_pointPositions.clear(); m_pointIds.clear(); m_sourcePoints = 0;
    if (!problem().isEmpty()) return;
    if (m_grid && m_grid->count()) {
        const int rx = m_resolutionX, ry = m_resolutionY;
        const auto &gx = m_grid->xs(), &gy = m_grid->ys();
        // AGENT-GUARD: the last lattice line is the last grid line exactly;
        // rounding past it would make bilinear sampling report "outside".
        for (int c = 0; c < rx; ++c)
            m_latticeX.append(c == rx - 1 ? gx.last() : std::clamp(gx.first() + (gx.last() - gx.first()) * c / (rx - 1), gx.first(), gx.last()));
        for (int r = 0; r < ry; ++r)
            m_latticeY.append(r == ry - 1 ? gy.last() : std::clamp(gy.first() + (gy.last() - gy.first()) * r / (ry - 1), gy.first(), gy.last()));
        m_latticeZ.resize(rx * ry);
        m_mesh.positions.resize(rx * ry); m_mesh.normals.fill(QVector3D(), rx * ry); m_mesh.colors.resize(rx * ry);
        for (int r = 0; r < ry; ++r)
            for (int c = 0; c < rx; ++c) {
                const int k = r * rx + c;
                const double z = m_grid->sampleAt(m_latticeX[c], m_latticeY[r], m_interpolation);
                m_latticeZ[k] = z;
                if (std::isnan(z)) continue;
                m_mesh.positions[k] = toScene(m_latticeX[c], m_latticeY[r], z);
                m_mesh.colors[k] = rampColor(z);
            }
        for (int r = 0; r + 1 < ry; ++r)
            for (int c = 0; c + 1 < rx; ++c) {
                const quint32 a = quint32(r * rx + c), b = a + 1, d = a + quint32(rx), e = d + 1;
                if (std::isnan(m_latticeZ[a]) || std::isnan(m_latticeZ[b]) || std::isnan(m_latticeZ[d]) || std::isnan(m_latticeZ[e])) {
                    ++m_holes; continue; // a missing corner leaves a hole, never a zero
                }
                // Counter-clockwise seen from +Z data (scene +Y): normals point up.
                const quint32 triangles[2][3] = {{a, b, e}, {a, e, d}};
                for (const auto &t : triangles) {
                    const QVector3D n = QVector3D::crossProduct(m_mesh.positions[t[1]] - m_mesh.positions[t[0]],
                                                                m_mesh.positions[t[2]] - m_mesh.positions[t[0]]);
                    for (quint32 v : t) { m_mesh.indices.append(v); m_mesh.normals[v] += n; }
                }
            }
        for (QVector3D &n : m_mesh.normals) n = n.isNull() ? QVector3D(0, 1, 0) : n.normalized();
    }
    if (!m_points) return;
    const QVector<ScientificSample> &samples = m_points->samples();
    QVector<int> drawn;
    drawn.reserve(samples.size());
    for (int i = 0; i < samples.size(); ++i) if (!samples[i].missingY) drawn.append(i);
    m_sourcePoints = int(drawn.size());
    if (drawn.size() > m_pointLimit) {
        // Stride with X/Y extremes, then the Z extremes and the selection.
        int zLow = drawn.first(), zHigh = drawn.first();
        for (int i : drawn) {
            if (samples[i].z < samples[zLow].z) zLow = i;
            if (samples[i].z > samples[zHigh].z) zHigh = i;
        }
        const int selected = m_points->indexOf(m_selectedPoint);
        drawn = ScientificSampling::scatterStride(samples, drawn, m_pointLimit - 3);
        drawn << zLow << zHigh;
        if (selected >= 0 && !samples[selected].missingY) drawn << selected;
        std::sort(drawn.begin(), drawn.end());
        drawn.erase(std::unique(drawn.begin(), drawn.end()), drawn.end());
    }
    m_pointPositions.reserve(drawn.size()); m_pointIds.reserve(drawn.size());
    for (int i : drawn) {
        m_pointPositions.append(toScene(samples[i].x, samples[i].y, samples[i].z));
        m_pointIds.append(samples[i].id);
    }
}

QVector<QVector3D> ScientificScene3D::frameSegments() const
{
    const float e = float(sceneExtent());
    QVector<QVector3D> out;
    for (int axis = 0; axis < 3; ++axis)
        for (int k = 0; k < 4; ++k) {
            QVector3D a, b;
            const float u = (k & 1) ? e : -e, v = (k & 2) ? e : -e;
            a[axis] = -e; b[axis] = e;
            a[(axis + 1) % 3] = b[(axis + 1) % 3] = u;
            a[(axis + 2) % 3] = b[(axis + 2) % 3] = v;
            out << a << b;
        }
    return out;
}

QString ScientificScene3D::hitPoint(double px, double py, double width, double height) const
{
    const auto &positions = pointPositions();
    const double radius = metric("hitRadius");
    double best = radius, bestDepth = 2; QString id;
    for (int i = 0; i < positions.size(); ++i) {
        bool visible = false;
        const QVector3D p = projectScene(positions[i], QSizeF(width, height), &visible);
        if (!visible) continue;
        const double d = std::hypot(p.x() - px, p.y() - py);
        // Nearest on screen; ties go to the point closer to the camera.
        if (d < best || (d == best && p.z() < bestDepth)) { best = d; bestDepth = p.z(); id = m_pointIds[i]; }
    }
    return id;
}
bool ScientificScene3D::selectPoint(const QString &id)
{
    const int index = m_points && problem().isEmpty() ? m_points->indexOf(id) : -1;
    if (index < 0 || m_points->samples()[index].missingY) return false;
    if (m_selectedPoint == id) return true;
    // AGENT-GUARD: a selection among drawn points is not a mesh change; only
    // ScientificGeometry3D's point colours and the painter follow it. A point
    // thinned away (picked from the table) is added by one re-thinning.
    const bool drawn = pointIds().contains(id);
    m_selectedPoint = id;
    if (!drawn) invalidate();
    emit selectionChanged(); emit pointSelected(id);
    return true;
}
QVariantMap ScientificScene3D::latticeNode(int row, int column) const
{
    ensureMesh();
    if (row < 0 || column < 0 || row >= m_latticeY.size() || column >= m_latticeX.size()) return {};
    const double z = m_latticeZ[row * m_latticeX.size() + column];
    return {{QStringLiteral("row"), row}, {QStringLiteral("column"), column}, {QStringLiteral("x"), m_latticeX[column]},
            {QStringLiteral("y"), m_latticeY[row]}, {QStringLiteral("z"), std::isnan(z) ? QVariant() : QVariant(z)},
            {QStringLiteral("missing"), std::isnan(z)}};
}

} // namespace QindaTK
