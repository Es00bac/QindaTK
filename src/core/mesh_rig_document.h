// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <QColor>
#include <QImage>
#include <QMap>
#include <QObject>
#include <QPointF>
#include <QSet>
#include <QVector3D>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// A bounded triangle-mesh document. Vertex, face, UV-corner and bone IDs are
// stable across pose/UV edits, undo, OBJ export and native save/reopen.
// AGENT-CONTRACT: SceneView3D accepts primitives only; this model exports posed
// OBJ geometry and texture PNG until a host supplies an arbitrary-mesh renderer.
// See docs/mesh-rig.md and docs/scene-3d.md.
class MeshRigDocument : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList vertices READ vertices NOTIFY changed)
    Q_PROPERTY(QVariantList faces READ faces NOTIFY changed)
    Q_PROPERTY(QVariantList bones READ bones NOTIFY changed)
    Q_PROPERTY(QVariantList uvIslands READ uvIslands NOTIFY changed)
    Q_PROPERTY(QString textureDataUrl READ textureDataUrl NOTIFY changed)
    Q_PROPERTY(int textureWidth READ textureWidth NOTIFY changed)
    Q_PROPERTY(int textureHeight READ textureHeight NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit MeshRigDocument(QObject *parent = nullptr);
    QVariantList vertices() const;
    QVariantList faces() const;
    QVariantList bones() const;
    QVariantList uvIslands() const;
    QString textureDataUrl() const;
    int textureWidth() const;
    int textureHeight() const;
    bool canUndo() const;
    bool canRedo() const;
    QString lastError() const { return m_error; }

    Q_INVOKABLE QString addVertex(const QVector3D &position);
    Q_INVOKABLE bool setVertexPosition(const QString &vertexId, const QVector3D &position);
    Q_INVOKABLE bool removeVertex(const QString &vertexId);
    Q_INVOKABLE QString addFace(const QString &v0, const QString &v1, const QString &v2);
    Q_INVOKABLE bool removeFace(const QString &faceId);
    Q_INVOKABLE bool setUV(const QString &faceId, int corner, const QPointF &uv);
    Q_INVOKABLE bool setSeam(const QString &v0, const QString &v1, bool seam);
    Q_INVOKABLE bool translateUvIsland(const QString &faceId, const QPointF &delta);
    Q_INVOKABLE QString addBone(const QString &name, const QString &parentId,
                                const QVector3D &head, const QVector3D &tail);
    Q_INVOKABLE bool setBoneLimits(const QString &boneId, const QVector3D &minimum,
                                   const QVector3D &maximum);
    Q_INVOKABLE bool setBonePose(const QString &boneId, const QVector3D &eulerDegrees);
    Q_INVOKABLE bool resetPose();
    Q_INVOKABLE bool setVertexWeights(const QString &vertexId, const QVariantMap &weights);
    Q_INVOKABLE bool resizeTexture(int width, int height);
    Q_INVOKABLE bool beginPaint();
    Q_INVOKABLE bool paintRay(const QVector3D &origin, const QVector3D &direction,
                              const QColor &color, int radiusPixels = 4,
                              qreal opacity = 1.0);
    Q_INVOKABLE bool endPaint();
    Q_INVOKABLE void cancelPaint();
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE QImage texture() const;
    Q_INVOKABLE bool exportObj(const QString &path) const;
    Q_INVOKABLE bool exportTexturePng(const QString &path) const;
    Q_INVOKABLE QString toJson() const;
    Q_INVOKABLE bool fromJson(const QString &json);
    Q_INVOKABLE bool saveToFile(const QString &path) const;
    Q_INVOKABLE bool loadFromFile(const QString &path);
signals:
    void changed();
    void errorChanged();
private:
    struct Vertex { QString id; QVector3D rest; QMap<QString, double> weights; };
    struct Face { QString id; int vertices[3]; QString uvIds[3]; QPointF uv[3]; };
    struct Bone {
        QString id, name, parentId;
        QVector3D head, tail, minimum{-180, -180, -180}, maximum{180, 180, 180};
        QVector3D pose;
    };
    struct State {
        QVector<Vertex> vertices;
        QVector<Face> faces;
        QVector<Bone> bones;
        QSet<QString> seams;
        QImage texture;
    };
    int vertexIndex(const QString &id) const;
    int faceIndex(const QString &id) const;
    int boneIndex(const QString &id) const;
    QVector<QVector3D> posedPositions() const;
    QVector<QVector<int>> islandIndices() const;
    bool fail(const QString &message) const;
    void remember();
    void notify();
    static QString edgeKey(const QString &a, const QString &b);
    static bool finitePoint(const QVector3D &point);
    static bool validUv(const QPointF &uv);
    bool stateFromJson(const QString &json, State *result) const;

    State m_state;
    QVector<State> m_undo, m_redo;
    State m_beforePaint;
    bool m_painting = false;
    bool m_paintChanged = false;
    int m_paintSamples = 0;
    qint64 m_paintPixelWork = 0;
    mutable QString m_error;
};

} // namespace QindaTK
