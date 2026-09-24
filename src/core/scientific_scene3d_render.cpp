// SPDX-License-Identifier: LGPL-3.0-or-later
// Software projection, export and text alternative for ScientificScene3D.
#include "scientific_scene3d.h"
#include "scientific_sampling.h"

#include <QBuffer>
#include <QFontMetricsF>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPolygonF>
#include <QSaveFile>
#include <QSvgGenerator>
#include <algorithm>
#include <cmath>

namespace QindaTK {
namespace {
// AGENT-NOTE: headlight shading (light at the camera) keeps every face
// readable from any orbit; ambient + diffuse * |n . view| stays in [0.4, 1].
constexpr double Ambient = 0.4, Diffuse = 0.6;
QColor shaded(const QColor &c, double factor)
{
    return QColor::fromRgbF(float(c.redF() * factor), float(c.greenF() * factor), float(c.blueF() * factor), float(c.alphaF()));
}
QString interpolationName(ScientificGrid::Interpolation mode)
{ return mode == ScientificGrid::Nearest ? QStringLiteral("nearest") : QStringLiteral("bilinear"); }
}

void ScientificScene3D::drawScene(QPainter *p, const QSizeF &size) const
{
    using ScientificSampling::tickLabel;
    p->fillRect(QRectF(QPointF(), size), m_background);
    p->setRenderHint(QPainter::Antialiasing, true);
    QFont font = p->font();
    font.setFamily(m_metrics.value(QStringLiteral("fontFamily")).toString());
    font.setPixelSize(std::max(1, m_metrics.value(QStringLiteral("fontSize")).toInt()));
    p->setFont(font);
    const QFontMetricsF fm(font);
    p->setPen(m_text);
    if (!m_title.isEmpty()) p->drawText(QRectF(0, 0, size.width(), fm.height() * 1.5), Qt::AlignCenter, m_title);
    const QString issue = problem();
    if (!issue.isEmpty()) { p->drawText(QRectF(QPointF(), size), Qt::AlignCenter | Qt::TextWordWrap, issue); return; }
    ensureMesh();
    const double line = metric("lineWidth");
    // Frame behind everything else (the cube is drawn as edges only).
    const QVector<QVector3D> frame = frameSegments();
    p->setPen(QPen(m_frameColor, line));
    for (int i = 0; i + 1 < frame.size(); i += 2) {
        bool va = false, vb = false;
        const QVector3D a = projectScene(frame[i], size, &va), b = projectScene(frame[i + 1], size, &vb);
        if (va && vb) p->drawLine(a.toPointF(), b.toPointF());
    }
    // Surface: painter's algorithm, far triangles first.
    struct Face { QPolygonF polygon; double depth; QColor color; };
    QVector<Face> faces;
    faces.reserve(m_mesh.indices.size() / 3);
    const QVector3D eye = cameraPosition();
    for (int i = 0; i + 2 < m_mesh.indices.size(); i += 3) {
        const quint32 t[3] = {m_mesh.indices[i], m_mesh.indices[i + 1], m_mesh.indices[i + 2]};
        Face face; face.depth = 0;
        bool visible = true;
        double r = 0, g = 0, b = 0;
        for (quint32 v : t) {
            bool ok = false;
            const QVector3D s = projectScene(m_mesh.positions[v], size, &ok);
            visible = visible && ok;
            face.polygon << s.toPointF(); face.depth += s.z() / 3;
            r += m_mesh.colors[v].redF() / 3; g += m_mesh.colors[v].greenF() / 3; b += m_mesh.colors[v].blueF() / 3;
        }
        if (!visible) continue;
        const QVector3D centre = (m_mesh.positions[t[0]] + m_mesh.positions[t[1]] + m_mesh.positions[t[2]]) / 3;
        const QVector3D normal = QVector3D::crossProduct(m_mesh.positions[t[1]] - m_mesh.positions[t[0]],
                                                         m_mesh.positions[t[2]] - m_mesh.positions[t[0]]).normalized();
        const double light = Ambient + Diffuse * std::abs(QVector3D::dotProduct(normal, (eye - centre).normalized()));
        face.color = shaded(QColor::fromRgbF(float(r), float(g), float(b)), light);
        faces.append(face);
    }
    std::stable_sort(faces.begin(), faces.end(), [](const Face &a, const Face &b) { return a.depth > b.depth; });
    for (const Face &f : faces) {
        p->setPen(QPen(f.color, line * 0.5)); p->setBrush(f.color);
        p->drawPolygon(f.polygon);
    }
    // Points: far first, selected ring last.
    struct Dot { QPointF at; double depth; int index; };
    QVector<Dot> dots;
    for (int i = 0; i < m_pointPositions.size(); ++i) {
        bool ok = false;
        const QVector3D s = projectScene(m_pointPositions[i], size, &ok);
        if (ok) dots.append({s.toPointF(), s.z(), i});
    }
    std::stable_sort(dots.begin(), dots.end(), [](const Dot &a, const Dot &b) { return a.depth > b.depth; });
    const double radius = metric("pointRadius");
    p->setPen(QPen(m_background, line)); p->setBrush(m_pointColor);
    for (const Dot &d : dots) p->drawEllipse(d.at, radius, radius);
    for (const Dot &d : dots)
        if (m_pointIds[d.index] == m_selectedPoint) {
            p->setPen(QPen(m_selection, line * 2)); p->setBrush(Qt::NoBrush);
            p->drawEllipse(d.at, radius * 2, radius * 2);
        }
    // Axis end labels at the domain corners, with units.
    const QString xu = m_grid ? m_grid->xUnit() : m_points ? m_points->xUnit() : QString();
    const QString yu = m_grid ? m_grid->yUnit() : m_points ? m_points->yUnit() : QString();
    const QString zu = m_grid ? m_grid->zUnit() : m_points ? m_points->zUnit() : QString();
    const struct { double x, y, z; QString text; } labels[] = {
        {m_domain[0], m_domain[2], m_domain[4], QStringLiteral("x %1, y %2, z %3").arg(tickLabel(m_domain[0]), tickLabel(m_domain[2]), tickLabel(m_domain[4]))},
        {m_domain[1], m_domain[2], m_domain[4], QStringLiteral("x %1 %2").arg(tickLabel(m_domain[1]), xu)},
        {m_domain[0], m_domain[3], m_domain[4], QStringLiteral("y %1 %2").arg(tickLabel(m_domain[3]), yu)},
        {m_domain[0], m_domain[2], m_domain[5], QStringLiteral("z %1 %2").arg(tickLabel(m_domain[5]), zu)}};
    p->setPen(m_text);
    for (const auto &l : labels) {
        bool ok = false;
        const QVector3D s = projectScene(toScene(l.x, l.y, l.z), size, &ok);
        if (ok) p->drawText(s.toPointF() + QPointF(radius, -radius), l.text);
    }
    const QString footer = tr("%1 interpolation, %2 × %3 lattice · %4 triangles · %5 holes · %6 points")
        .arg(interpolationName(m_interpolation)).arg(m_resolutionX).arg(m_resolutionY)
        .arg(m_mesh.indices.size() / 3).arg(m_holes).arg(m_pointPositions.size());
    p->drawText(QRectF(0, size.height() - fm.height() * 1.5, size.width(), fm.height() * 1.5), Qt::AlignCenter, footer);
}

QString ScientificScene3D::csvData() const
{
    ensureMesh();
    QString out = QStringLiteral("row,column,x,y,z,missing,interpolation\n");
    for (int r = 0; r < m_latticeY.size(); ++r)
        for (int c = 0; c < m_latticeX.size(); ++c) {
            const double z = m_latticeZ[r * m_latticeX.size() + c];
            out += QStringLiteral("%1,%2,%3,%4,%5,%6,%7\n").arg(r).arg(c)
                .arg(QString::number(m_latticeX[c], 'g', 17), QString::number(m_latticeY[r], 'g', 17),
                     std::isnan(z) ? QString() : QString::number(z, 'g', 17),
                     std::isnan(z) ? QStringLiteral("true") : QStringLiteral("false"), interpolationName(m_interpolation));
        }
    return out;
}

QString ScientificScene3D::textSummary() const
{
    using ScientificSampling::tickLabel;
    QStringList parts;
    if (!m_title.isEmpty()) parts << m_title + '.';
    if (!problem().isEmpty()) return (parts << problem() + '.').join(' ');
    parts << tr("Domain X %1 to %2, Y %3 to %4, Z %5 to %6.").arg(tickLabel(m_domain[0]), tickLabel(m_domain[1]),
        tickLabel(m_domain[2]), tickLabel(m_domain[3]), tickLabel(m_domain[4]), tickLabel(m_domain[5]));
    if (m_grid && m_grid->count())
        parts << tr("Surface %1 sampled on a %2 by %3 lattice with %4 interpolation, %5 holes from missing values.")
            .arg(m_grid->label()).arg(m_resolutionX).arg(m_resolutionY).arg(interpolationName(m_interpolation)).arg(holeCount());
    if (m_points) parts << tr("%1 points from %2.").arg(pointCount()).arg(m_points->label());
    parts << tr("Camera azimuth %1°, elevation %2°.").arg(tickLabel(m_azimuth), tickLabel(m_elevation));
    return parts.join(' ');
}

QString ScientificScene3D::metadataJson(int width, int height) const
{
    ensureMesh();
    const QVector3D position = cameraPosition();
    const QQuaternion rotation = cameraRotation();
    const QJsonObject camera{{QStringLiteral("azimuth"), m_azimuth}, {QStringLiteral("elevation"), m_elevation},
        {QStringLiteral("distance"), m_distance}, {QStringLiteral("fieldOfView"), m_fieldOfView},
        {QStringLiteral("clipNear"), clipNear()}, {QStringLiteral("clipFar"), clipFar()},
        {QStringLiteral("position"), QJsonArray{position.x(), position.y(), position.z()}},
        {QStringLiteral("rotation"), QJsonArray{rotation.scalar(), rotation.x(), rotation.y(), rotation.z()}}};
    const QJsonObject metadata{{QStringLiteral("schema"), QStringLiteral("QindaTK.scientific3d.v1")},
        {QStringLiteral("title"), m_title}, {QStringLiteral("width"), width}, {QStringLiteral("height"), height},
        {QStringLiteral("surface"), m_grid ? QJsonValue(QJsonObject::fromVariantMap(m_grid->metadata())) : QJsonValue()},
        {QStringLiteral("points"), m_points ? QJsonValue(QJsonObject::fromVariantMap(m_points->metadata())) : QJsonValue()},
        {QStringLiteral("domain"), QJsonArray{m_domain[0], m_domain[1], m_domain[2], m_domain[3], m_domain[4], m_domain[5]}},
        {QStringLiteral("sceneExtent"), sceneExtent()},
        {QStringLiteral("mapping"), QStringLiteral("scene = (nx, nz, -ny) * extent, n = 2(v-min)/(max-min)-1")},
        {QStringLiteral("interpolation"), interpolationName(m_interpolation)},
        {QStringLiteral("resolution"), QJsonArray{m_resolutionX, m_resolutionY}},
        {QStringLiteral("triangleCount"), int(m_mesh.indices.size() / 3)}, {QStringLiteral("holeCount"), m_holes},
        {QStringLiteral("pointCount"), int(m_pointPositions.size())}, {QStringLiteral("camera"), camera},
        {QStringLiteral("problem"), problem()}};
    return QString::fromUtf8(QJsonDocument(metadata).toJson(QJsonDocument::Compact));
}

QImage ScientificScene3D::renderImage(const QSize &size) const
{
    if (size.width() < 1 || size.height() < 1 || size.width() > 8192 || size.height() > 8192) return {};
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image); drawScene(&painter, QSizeF(size)); painter.end();
    image.setText(QStringLiteral("QindaTK.Scientific3D"), metadataJson(size.width(), size.height()));
    return image;
}
bool ScientificScene3D::exportPng(const QString &path, const QSize &size) const
{
    const QImage image = renderImage(size);
    if (image.isNull()) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG")) return false;
    return file.commit();
}
bool ScientificScene3D::exportSvg(const QString &path, const QSize &size) const
{
    if (size.width() < 1 || size.height() < 1 || size.width() > 8192 || size.height() > 8192) return false;
    QByteArray bytes; QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly)) return false;
    QSvgGenerator svg; svg.setOutputDevice(&buffer); svg.setSize(size); svg.setViewBox(QRect(QPoint(), size));
    svg.setTitle(m_title);
    QPainter painter(&svg); drawScene(&painter, QSizeF(size)); painter.end(); buffer.close();
    const qsizetype end = bytes.lastIndexOf("</svg>");
    if (end < 0) return false;
    bytes.insert(end, "<metadata id=\"qindatk-scientific-3d\" data-encoding=\"base64-json\">"
                 + metadataJson(size.width(), size.height()).toUtf8().toBase64() + "</metadata>");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return false;
    return file.commit();
}

ScientificScene3DView::ScientificScene3DView(QQuickItem *parent) : QQuickPaintedItem(parent) {}
void ScientificScene3DView::setScene(ScientificScene3D *value)
{
    if (m_scene == value) return;
    if (m_scene) disconnect(m_scene, nullptr, this, nullptr);
    m_scene = value;
    if (value) {
        connect(value, &ScientificScene3D::meshChanged, this, [this] { update(); });
        connect(value, &ScientificScene3D::cameraChanged, this, [this] { update(); });
        connect(value, &ScientificScene3D::changed, this, [this] { update(); });
    }
    emit sceneChanged(); update();
}
void ScientificScene3DView::paint(QPainter *painter)
{
    if (m_scene) m_scene->drawScene(painter, QSizeF(width(), height()));
}

} // namespace QindaTK
