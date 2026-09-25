// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QPointer>
#include <QtQuick3D/qquick3dgeometry.h>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

class ScientificScene3D;

// Optional Qt Quick 3D upload of one part of a ScientificScene3D: the
// surface triangles, the scatter points (small octahedra) or the domain frame
// (lines). Built only when Qt Quick 3D is available (src/CMakeLists.txt).
// Vertex layout: position (3 x f32), normal (3 x f32), colour (4 x f32),
// non-indexed; each point is VerticesPerPoint vertices. tst_scientific_geometry3d
// checks the buffer against the scene headless.
// AGENT-CONTRACT: ScientificScene3D owns every position and colour; this
// adapter copies its current snapshot into GPU vertex buffers on meshChanged
// and never transforms data itself, so the GPU image matches the software
// view and export (same scene coordinates, same camera properties).
class ScientificGeometry3D : public QQuick3DGeometry {
    Q_OBJECT
    QML_ELEMENT
    Q_MOC_INCLUDE("scientific_scene3d.h")
    Q_PROPERTY(QindaTK::ScientificScene3D *scene READ scene WRITE setScene NOTIFY sceneChanged)
    Q_PROPERTY(Part part READ part WRITE setPart NOTIFY sceneChanged)
    Q_PROPERTY(int primitiveCount READ primitiveCount NOTIFY geometryUpdated)
public:
    enum Part { Surface, Points, Frame };
    Q_ENUM(Part)
    static constexpr int FloatsPerVertex = 10;
    static constexpr int VerticesPerPoint = 24;
    explicit ScientificGeometry3D(QQuick3DObject *parent = nullptr);
    ScientificScene3D *scene() const { return m_scene; }
    void setScene(ScientificScene3D *value);
    Part part() const { return m_part; }
    void setPart(Part value);
    int primitiveCount() const { return m_primitives; }
signals:
    void sceneChanged();
    void geometryUpdated();
private:
    void rebuild();
    // Points part: rewrites only the old and new selected points' vertices.
    void recolorSelection();
    QPointer<ScientificScene3D> m_scene;
    Part m_part = Surface;
    int m_primitives = 0;
    int m_selectedIndex = -1;
};

} // namespace QindaTK
