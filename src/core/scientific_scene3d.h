// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "scientific_dataset.h"
#include "scientific_grid.h"

#include <QColor>
#include <QImage>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QPointer>
#include <QQuaternion>
#include <QQuickPaintedItem>
#include <QVector3D>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Data-to-scene transform, camera and meshes for 3D scientific plots. One
// object feeds three consumers that must agree: the Qt Quick 3D renderer
// (ScientificGeometry3D + ScientificPlot3DRenderer.qml), the software view
// (ScientificScene3DView) and PNG/SVG export (renderImage()).
// AGENT-CONTRACT: data (x, y, z) maps to scene (X, Y, Z) = (nx, nz, -ny) * sceneExtent
// with n = 2 (v - min) / (max - min) - 1, so data Z is up in Qt Quick 3D's
// Y-up world. The camera orbits the origin: position = distance * (cos el
// sin az, sin el, cos el cos az), rotation = fromEulerAngles(-el, az, 0),
// vertical fieldOfView, OpenGL clip space. ScientificPlot3DRenderer.qml binds
// PerspectiveCamera to cameraPosition/cameraRotation/fieldOfView/clipNear/clipFar.
class ScientificScene3D : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(ScientificGrid *grid READ grid WRITE setGrid NOTIFY changed)
    Q_PROPERTY(ScientificDataset *points READ points WRITE setPoints NOTIFY changed)
    Q_PROPERTY(QindaTK::ScientificGrid::Interpolation interpolation READ interpolation WRITE setInterpolation NOTIFY changed)
    Q_PROPERTY(int resolutionX READ resolutionX WRITE setResolutionX NOTIFY changed)
    Q_PROPERTY(int resolutionY READ resolutionY WRITE setResolutionY NOTIFY changed)
    Q_PROPERTY(double xMinimum READ xMinimum WRITE setXMinimum NOTIFY changed)
    Q_PROPERTY(double xMaximum READ xMaximum WRITE setXMaximum NOTIFY changed)
    Q_PROPERTY(double yMinimum READ yMinimum WRITE setYMinimum NOTIFY changed)
    Q_PROPERTY(double yMaximum READ yMaximum WRITE setYMaximum NOTIFY changed)
    Q_PROPERTY(double zMinimum READ zMinimum WRITE setZMinimum NOTIFY changed)
    Q_PROPERTY(double zMaximum READ zMaximum WRITE setZMaximum NOTIFY changed)
    Q_PROPERTY(double sceneExtent READ sceneExtent CONSTANT)
    Q_PROPERTY(double azimuth READ azimuth WRITE setAzimuth NOTIFY cameraChanged)
    Q_PROPERTY(double elevation READ elevation WRITE setElevation NOTIFY cameraChanged)
    Q_PROPERTY(double distance READ distance WRITE setDistance NOTIFY cameraChanged)
    Q_PROPERTY(double fieldOfView READ fieldOfView WRITE setFieldOfView NOTIFY cameraChanged)
    Q_PROPERTY(double clipNear READ clipNear CONSTANT)
    Q_PROPERTY(double clipFar READ clipFar CONSTANT)
    Q_PROPERTY(QVector3D cameraPosition READ cameraPosition NOTIFY cameraChanged)
    Q_PROPERTY(QQuaternion cameraRotation READ cameraRotation NOTIFY cameraChanged)
    Q_PROPERTY(int meshRevision READ meshRevision NOTIFY meshChanged)
    Q_PROPERTY(int triangleCount READ triangleCount NOTIFY meshChanged)
    Q_PROPERTY(int holeCount READ holeCount NOTIFY meshChanged)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY meshChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    Q_PROPERTY(QString selectedPointId READ selectedPointId NOTIFY selectionChanged)
    Q_PROPERTY(QList<QColor> surfaceColors MEMBER m_surfaceColors NOTIFY styleChanged)
    Q_PROPERTY(QColor pointColor MEMBER m_pointColor NOTIFY styleChanged)
    Q_PROPERTY(QColor frameColor MEMBER m_frameColor NOTIFY styleChanged)
    Q_PROPERTY(QColor backgroundColor MEMBER m_background NOTIFY styleChanged)
    Q_PROPERTY(QColor textColor MEMBER m_text NOTIFY styleChanged)
    Q_PROPERTY(QColor selectionColor MEMBER m_selection NOTIFY styleChanged)
    // fontFamily, fontSize, pointRadius, lineWidth, hitRadius, labelPadding (pixels).
    Q_PROPERTY(QVariantMap metrics MEMBER m_metrics NOTIFY styleChanged)
    Q_PROPERTY(QString title MEMBER m_title NOTIFY styleChanged)
    // The textual alternative (textSummary()); notifies on data, style and camera changes.
    Q_PROPERTY(QString summary READ textSummary NOTIFY summaryChanged)
public:
    static constexpr int ResolutionLimit = 256;
    struct Mesh {
        QVector<QVector3D> positions, normals;
        QVector<QColor> colors;
        QVector<quint32> indices;   // triangles
    };
    explicit ScientificScene3D(QObject *parent = nullptr);
    ScientificGrid *grid() const { return m_grid; }
    void setGrid(ScientificGrid *value);
    ScientificDataset *points() const { return m_points; }
    void setPoints(ScientificDataset *value);
    ScientificGrid::Interpolation interpolation() const { return m_interpolation; }
    void setInterpolation(ScientificGrid::Interpolation value);
    int resolutionX() const { return m_resolutionX; }
    void setResolutionX(int value);
    int resolutionY() const { return m_resolutionY; }
    void setResolutionY(int value);
    double xMinimum() const { return m_domain[0]; }
    double xMaximum() const { return m_domain[1]; }
    double yMinimum() const { return m_domain[2]; }
    double yMaximum() const { return m_domain[3]; }
    double zMinimum() const { return m_domain[4]; }
    double zMaximum() const { return m_domain[5]; }
    void setXMinimum(double v) { setDomain(0, v); }
    void setXMaximum(double v) { setDomain(1, v); }
    void setYMinimum(double v) { setDomain(2, v); }
    void setYMaximum(double v) { setDomain(3, v); }
    void setZMinimum(double v) { setDomain(4, v); }
    void setZMaximum(double v) { setDomain(5, v); }
    double sceneExtent() const { return 100; }
    double azimuth() const { return m_azimuth; }
    void setAzimuth(double value);
    double elevation() const { return m_elevation; }
    void setElevation(double value);
    double distance() const { return m_distance; }
    void setDistance(double value);
    double fieldOfView() const { return m_fieldOfView; }
    void setFieldOfView(double value);
    double clipNear() const { return 1; }
    double clipFar() const { return 100000; }
    QVector3D cameraPosition() const;
    QQuaternion cameraRotation() const;
    QMatrix4x4 viewMatrix() const;
    QMatrix4x4 projectionMatrix(double aspect) const;
    int meshRevision() const { return m_revision; }
    int triangleCount() const { ensureMesh(); return int(m_mesh.indices.size() / 3); }
    int holeCount() const { ensureMesh(); return m_holes; }
    int pointCount() const { ensureMesh(); return int(m_pointPositions.size()); }
    QString problem() const;
    QString selectedPointId() const { return m_selectedPoint; }
    const Mesh &surfaceMesh() const { ensureMesh(); return m_mesh; }
    const QVector<QVector3D> &pointPositions() const { ensureMesh(); return m_pointPositions; }
    const QStringList &pointIds() const { ensureMesh(); return m_pointIds; }
    QVector<QVector3D> frameSegments() const;
    Q_INVOKABLE QVector3D toScene(double x, double y, double z) const;
    Q_INVOKABLE QVariantMap fromScene(const QVector3D &scene) const;
    // Pixel position in a width x height view: {x, y, depth (NDC z), visible}.
    Q_INVOKABLE QVariantMap project(double x, double y, double z, double width, double height) const;
    QVector3D projectScene(const QVector3D &scene, const QSizeF &size, bool *visible) const;
    Q_INVOKABLE void orbit(double azimuthDegrees, double elevationDegrees);
    Q_INVOKABLE void dolly(double factor);
    // Sets the six domain limits from the grid and points (with 3D extents).
    Q_INVOKABLE bool fitDomain();
    // Corner labels {text, x, y, visible}: anchor pixels of the domain corners
    // (min corner, +x, +y, +z) in a width x height view. Painters clamp them.
    Q_INVOKABLE QVariantList axisLabels(double width, double height) const;
    Q_INVOKABLE QString hitPoint(double pixelX, double pixelY, double width, double height) const;
    Q_INVOKABLE bool selectPoint(const QString &id);
    // Surface value of the displayed lattice node {row, column, x, y, z, missing}.
    Q_INVOKABLE QVariantMap latticeNode(int row, int column) const;
    Q_INVOKABLE QString csvData() const;
    Q_INVOKABLE QString textSummary() const;
    Q_INVOKABLE QString metadataJson(int width, int height) const;
    Q_INVOKABLE QImage renderImage(const QSize &size) const;
    Q_INVOKABLE bool exportPng(const QString &path, const QSize &size) const;
    Q_INVOKABLE bool exportSvg(const QString &path, const QSize &size) const;
    void drawScene(QPainter *painter, const QSizeF &size) const;
    QColor rampColor(double z) const;
signals:
    void changed();
    void cameraChanged();
    void meshChanged();
    void styleChanged();
    void selectionChanged();
    void pointSelected(const QString &id);
    void summaryChanged();
private:
    void setDomain(int index, double value);
    void invalidate();
    void ensureMesh() const;
    double metric(const char *name) const { return m_metrics.value(QLatin1String(name)).toDouble(); }
    QPointer<ScientificGrid> m_grid;
    QPointer<ScientificDataset> m_points;
    ScientificGrid::Interpolation m_interpolation = ScientificGrid::Bilinear;
    int m_resolutionX = 48, m_resolutionY = 48;
    double m_domain[6] = {0, 1, 0, 1, 0, 1};
    double m_azimuth = 35, m_elevation = 25, m_distance = 420, m_fieldOfView = 45;
    int m_revision = 0;
    QList<QColor> m_surfaceColors;
    QColor m_pointColor, m_frameColor, m_background, m_text, m_selection;
    QVariantMap m_metrics;
    QString m_title, m_selectedPoint;
    // Lazily rebuilt; ensureMesh() is const so read-only consumers can use it.
    mutable bool m_dirty = true;
    mutable Mesh m_mesh;
    mutable QVector<double> m_latticeX, m_latticeY, m_latticeZ;
    mutable int m_holes = 0;
    mutable QVector<QVector3D> m_pointPositions;
    mutable QStringList m_pointIds;
};

// Software-rendered view of a ScientificScene3D (the same painter as export).
// ScientificPlot3D shows it when no RHI graphics backend is available.
class ScientificScene3DView : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(ScientificScene3D *scene READ scene WRITE setScene NOTIFY sceneChanged)
public:
    explicit ScientificScene3DView(QQuickItem *parent = nullptr);
    ScientificScene3D *scene() const { return m_scene; }
    void setScene(ScientificScene3D *value);
    void paint(QPainter *painter) override;
signals:
    void sceneChanged();
private:
    QPointer<ScientificScene3D> m_scene;
};

} // namespace QindaTK
