// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QImage>
#include <QPointer>
#include <QtQuick3D/qquick3dgeometry.h>
#include <QtQuick3D/qquick3dtexturedata.h>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

class MeshRigDocument;

// AGENT-CONTRACT: MeshRigDocument owns stable mesh/UV/pose/texture state; this
// optional Quick 3D adapter only uploads its current posed triangle snapshot
// and its three texture channels to the GPU. The upload bytes come from the
// public static pack functions, which the headless tests (tst_mesh_rig_paint)
// compare against the document's CPU state byte for byte (D-154), so the GPU
// draws exactly the data the software reference checks.
class MeshRigGeometry : public QQuick3DGeometry {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MeshRigDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(int triangleCount READ triangleCount NOTIFY geometryUpdated)
    Q_PROPERTY(QQuick3DTextureData *textureData READ textureData CONSTANT)
    Q_PROPERTY(QQuick3DTextureData *roughnessData READ roughnessData CONSTANT)
    Q_PROPERTY(QQuick3DTextureData *metalnessData READ metalnessData CONSTANT)
public:
    // Interleaved per-corner vertex: position xyz, face normal xyz, uv (v flipped).
    static constexpr int FloatsPerVertex = 8;
    explicit MeshRigGeometry(QQuick3DObject *parent = nullptr);
    MeshRigDocument *document() const { return m_document; }
    void setDocument(MeshRigDocument *document);
    int triangleCount() const { return m_triangleCount; }
    QQuick3DTextureData *textureData() const { return m_textureData; }
    QQuick3DTextureData *roughnessData() const { return m_roughnessData; }
    QQuick3DTextureData *metalnessData() const { return m_metalnessData; }

    // Vertex buffer for the document's posed triangles; `triangles` receives
    // the count of non-degenerate faces packed.
    static QByteArray packVertices(const MeshRigDocument *document, int *triangles = nullptr);
    // Straight-alpha RGBA8 rows, top row first, exactly as uploaded.
    static QByteArray packTexture(const QImage &image);
signals:
    void documentChanged();
    void geometryUpdated();
private:
    void rebuild();
    static void upload(QQuick3DTextureData *target, const QImage &image);
    QPointer<MeshRigDocument> m_document;
    QQuick3DTextureData *m_textureData;
    QQuick3DTextureData *m_roughnessData;
    QQuick3DTextureData *m_metalnessData;
    int m_triangleCount = 0;
};

} // namespace QindaTK
