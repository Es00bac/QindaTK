// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "document_model.h"

#include <QColor>
#include <QImage>
#include <QMap>
#include <QObject>
#include <QPointF>
#include <QSet>
#include <QSizeF>
#include <QStringList>
#include <QVector3D>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <array>

namespace QindaTK {
class UndoService;

// A bounded triangle-mesh document. Vertex, face, UV-corner, bone and
// constraint IDs are stable across pose/UV edits, undo, OBJ export and native
// save/reopen. See docs/mesh-rig.md.
// AGENT-CONTRACT: SceneView3D's optional Quick 3D adapter (MeshRigGeometry)
// reads only the public posed/UV snapshots and texture(channel); this model
// owns the editable mesh, rig, animation keys and texture channels, and
// exports posed OBJ geometry plus channel PNGs independently of the renderer.
// AGENT-CONTRACT (docs/document-models.md#commands): every committed edit is
// one DocumentCommand in a single UndoService history; a private service is
// used until a host adopts the model into a shared one (D-157).
class MeshRigDocument : public QObject, public DocumentModel {
    Q_OBJECT
    QML_ELEMENT
    Q_MOC_INCLUDE("undo_service.h")
    Q_PROPERTY(QVariantList vertices READ vertices NOTIFY changed)
    Q_PROPERTY(QVariantList faces READ faces NOTIFY changed)
    Q_PROPERTY(QVariantList bones READ bones NOTIFY changed)
    Q_PROPERTY(QVariantList uvIslands READ uvIslands NOTIFY changed)
    Q_PROPERTY(QString textureDataUrl READ textureDataUrl NOTIFY changed)
    Q_PROPERTY(QVariantMap textureDataUrls READ textureDataUrls NOTIFY changed)
    Q_PROPERTY(QStringList textureChannels READ textureChannels CONSTANT)
    Q_PROPERTY(int textureWidth READ textureWidth NOTIFY changed)
    Q_PROPERTY(int textureHeight READ textureHeight NOTIFY changed)
    Q_PROPERTY(int currentFrame READ currentFrame WRITE setCurrentFrame NOTIFY changed)
    Q_PROPERTY(bool painting READ painting NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString documentId READ documentId CONSTANT)
    Q_PROPERTY(QString revision READ revision NOTIFY changed)
    Q_PROPERTY(QindaTK::UndoService *undoService READ undoService NOTIFY historyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    enum Channel { BaseColor = 0, Roughness = 1, Metalness = 2, ChannelCount = 3 };
    Q_ENUM(Channel)

    explicit MeshRigDocument(QObject *parent = nullptr);
    ~MeshRigDocument() override;
    QVariantList vertices() const;
    QVariantList faces() const;
    QVariantList bones() const;
    QVariantList uvIslands() const;
    QString textureDataUrl() const;
    QVariantMap textureDataUrls() const;
    static QStringList textureChannels();
    int textureWidth() const;
    int textureHeight() const;
    int currentFrame() const { return m_frame; }
    void setCurrentFrame(int frame);
    bool painting() const { return m_painting; }
    bool canUndo() const;
    bool canRedo() const;
    QString documentId() const { return m_documentId; }
    QString revision() const { return QString::number(m_revision); }
    UndoService *undoService() const;
    QString lastError() const { return m_error; }

    // Geometry and weights (mesh_rig_document.cpp).
    Q_INVOKABLE QString addVertex(const QVector3D &position);
    Q_INVOKABLE bool setVertexPosition(const QString &vertexId, const QVector3D &position);
    Q_INVOKABLE bool removeVertex(const QString &vertexId);
    Q_INVOKABLE QString addFace(const QString &v0, const QString &v1, const QString &v2);
    Q_INVOKABLE bool removeFace(const QString &faceId);
    Q_INVOKABLE bool setVertexWeights(const QString &vertexId, const QVariantMap &weights);
    // UV topology (mesh_rig_uv.cpp).
    Q_INVOKABLE bool setUV(const QString &faceId, int corner, const QPointF &uv);
    Q_INVOKABLE bool setSeam(const QString &v0, const QString &v1, bool seam);
    Q_INVOKABLE bool translateUvIsland(const QString &faceId, const QPointF &delta);
    Q_INVOKABLE bool transformUvIsland(const QString &faceId, const QPointF &translation,
                                       qreal rotationDegrees, qreal scale);
    Q_INVOKABLE bool stitchSeam(const QString &v0, const QString &v1);
    Q_INVOKABLE bool unwrapUvIsland(const QString &faceId);
    Q_INVOKABLE QVariantList packUvLayout(qreal margin = 0.02) const;
    Q_INVOKABLE bool packUvIslands(qreal margin = 0.02);
    // Rig, constraints and animation (mesh_rig_pose.cpp).
    Q_INVOKABLE QString addBone(const QString &name, const QString &parentId,
                                const QVector3D &head, const QVector3D &tail);
    Q_INVOKABLE bool setBoneLimits(const QString &boneId, const QVector3D &minimum,
                                   const QVector3D &maximum);
    Q_INVOKABLE bool setBonePose(const QString &boneId, const QVector3D &eulerDegrees);
    Q_INVOKABLE bool resetPose();
    Q_INVOKABLE QString addConstraint(const QString &boneId, const QString &type,
                                      const QString &targetId, qreal influence = 1.0);
    Q_INVOKABLE bool removeConstraint(const QString &constraintId);
    Q_INVOKABLE bool setPoseKey(const QString &boneId, int frame, const QVector3D &eulerDegrees);
    Q_INVOKABLE bool removePoseKey(const QString &boneId, int frame);
    Q_INVOKABLE QVector3D evaluatedPose(const QString &boneId) const;
    // Texture channels, paint and picking (mesh_rig_paint.cpp).
    Q_INVOKABLE bool resizeTexture(int width, int height);
    Q_INVOKABLE bool beginPaint(const QString &channel = QStringLiteral("baseColor"));
    Q_INVOKABLE bool paintRay(const QVector3D &origin, const QVector3D &direction,
                              const QColor &color, int radiusPixels = 4,
                              qreal opacity = 1.0, qreal hardness = 1.0);
    Q_INVOKABLE bool endPaint();
    Q_INVOKABLE void cancelPaint();
    Q_INVOKABLE QVariantMap pickFace(const QVector3D &origin, const QVector3D &direction) const;
    Q_INVOKABLE QVariantMap cameraRay(const QVector3D &cameraPosition,
                                      const QVector3D &cameraEulerDegrees,
                                      qreal verticalFieldOfViewDegrees,
                                      const QSizeF &viewportSize, const QPointF &point) const;
    Q_INVOKABLE QImage texture(const QString &channel = QStringLiteral("baseColor")) const;
    Q_INVOKABLE QString channelDataUrl(const QString &channel) const;
    // History, persistence and output.
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE bool exportObj(const QString &path) const;
    Q_INVOKABLE bool exportTexturePng(const QString &path,
                                      const QString &channel = QStringLiteral("baseColor")) const;
    Q_INVOKABLE QString toJson() const;
    Q_INVOKABLE bool fromJson(const QString &json);
    Q_INVOKABLE bool saveToFile(const QString &path) const;
    Q_INVOKABLE bool loadFromFile(const QString &path);

    // Qt Quick 3D's default PerspectiveCamera convention: looks down -Z,
    // QQuaternion::fromEulerAngles rotation, vertical field of view. Returns
    // false for an empty viewport or invalid input.
    static bool perspectiveRay(const QVector3D &cameraPosition, const QVector3D &cameraEulerDegrees,
                               qreal verticalFieldOfViewDegrees, const QSizeF &viewportSize,
                               const QPointF &point, QVector3D *origin, QVector3D *direction);
    static int channelIndex(const QString &channel);

    // DocumentModel.
    QObject *modelObject() const override { return const_cast<MeshRigDocument *>(this); }
    DocumentId modelId() const override { return DocumentId::parse(m_documentId); }
    QString modelKind() const override { return QStringLiteral("meshrig"); }
    SnapshotHeader modelHeader() const override;
    QMetaObject::Connection connectModelChanged(QObject *receiver, std::function<void()> slot) override;
    OperationError prepareCommand(const DocumentCommand &command, bool forward) const override;
    void publishCommand(const DocumentCommand &command, bool forward) override;
    DocumentCommandPtr composeCommands(const DocumentCommand &first,
                                       const DocumentCommand &second) const override;
signals:
    void changed();
    void committedChanged();
    void historyChanged();
    void errorChanged();
private:
    struct Vertex { QString id; QVector3D rest; QMap<QString, double> weights; };
    struct Face { QString id; int vertices[3]; QString uvIds[3]; QPointF uv[3]; };
    struct Constraint { QString id, type, targetId; double influence = 1; };
    struct PoseKey { int frame = 0; QVector3D pose; };
    struct Bone {
        QString id, name, parentId;
        QVector3D head, tail, minimum{-180, -180, -180}, maximum{180, 180, 180};
        QVector3D pose;
        QVector<Constraint> constraints;
        QVector<PoseKey> keys; // sorted by frame, unique frames
    };
    struct State {
        QVector<Vertex> vertices;
        QVector<Face> faces;
        QVector<Bone> bones;
        QSet<QString> seams;
        std::array<QImage, ChannelCount> textures;
    };
    struct RayHit { int face = -1; float distance = 0; QVector3D point; QPointF uv; };
    struct MeshCommand;

    int vertexIndex(const QString &id) const;
    int faceIndex(const QString &id) const;
    int boneIndex(const QString &id) const;
    int boneIndexIn(const State &state, const QString &id) const;
    static QVector3D sourcePose(const Bone &bone, int frame);
    static QVector<QVector3D> evaluatePoses(const State &state, int frame);
    QVector<QVector3D> posedPositions() const;
    QVector<QVector<int>> islandIndices() const;
    RayHit castRay(const QVector<QVector3D> &posed, const QVector3D &origin,
                   const QVector3D &direction) const;
    bool dab(const QVector<QVector3D> &posed, const RayHit &hit, const QColor &color,
             int radius, qreal opacity, qreal hardness);
    bool fail(const QString &message) const;
    void notify();
    bool commit(const State &next, const QString &label);
    UndoService *history() const;
    void historyAttached(UndoService *service) override;
    static QString edgeKey(const QString &a, const QString &b);
    static bool finitePoint(const QVector3D &point);
    static bool validUv(const QPointF &uv);
    static bool withinLimits(const Bone &bone, const QVector3D &pose);
    static bool constraintReaches(const State &state, const QString &fromBoneId,
                                  const QString &toBoneId);
    static QImage blankChannel(int channel, int width, int height);
    bool stateFromJson(const QString &json, State *result) const;

    State m_state;
    State m_beforePaint;
    UndoService *m_localHistory = nullptr;
    QMetaObject::Connection m_historyConnection;
    QString m_documentId;
    quint64 m_revision = 1;
    quint64 m_token = 1;     // identifies the committed content m_state holds
    quint64 m_nextToken = 1;
    quint64 m_resetGeneration = 0;
    int m_frame = 0;
    bool m_painting = false;
    bool m_paintChanged = false;
    int m_paintChannel = BaseColor;
    int m_paintSamples = 0;
    qint64 m_paintPixelWork = 0;
    bool m_hasPreviousRay = false;
    QVector3D m_previousOrigin, m_previousDirection, m_previousPoint;
    mutable QString m_error;
};

} // namespace QindaTK
