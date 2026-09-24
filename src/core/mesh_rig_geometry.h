// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QPointer>
#include <QtQuick3D/qquick3dgeometry.h>
#include <QtQuick3D/qquick3dtexturedata.h>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

class MeshRigDocument;

// AGENT-CONTRACT: MeshRigDocument owns stable mesh/UV/pose state; this optional
// Quick 3D adapter only uploads its current posed triangle snapshot to the GPU.
class MeshRigGeometry : public QQuick3DGeometry {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MeshRigDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(int triangleCount READ triangleCount NOTIFY geometryUpdated)
    Q_PROPERTY(QQuick3DTextureData *textureData READ textureData CONSTANT)
public:
    explicit MeshRigGeometry(QQuick3DObject *parent = nullptr);
    MeshRigDocument *document() const { return m_document; }
    void setDocument(MeshRigDocument *document);
    int triangleCount() const { return m_triangleCount; }
    QQuick3DTextureData *textureData() const { return m_textureData; }
signals:
    void documentChanged();
    void geometryUpdated();
private:
    void rebuild();
    QPointer<MeshRigDocument> m_document;
    QQuick3DTextureData *m_textureData;
    int m_triangleCount = 0;
};

} // namespace QindaTK
