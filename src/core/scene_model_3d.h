// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include "document_model.h"
#include <QObject>
#include <QColor>
#include <QJsonObject>
#include <QQuaternion>
#include <QVector3D>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class UndoService;
class SceneDocumentCodec;
// Scene coordinates are right-handed world units. Uniform positive scales make
// hierarchical rotations and keep-world reparenting exact without hidden shear.
class SceneModel3D : public QObject, public DocumentModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList entities READ entities NOTIFY changed)
    Q_PROPERTY(QString selectedId READ selectedId WRITE select NOTIFY selectionChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(bool editing READ editing NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
    Q_PROPERTY(QString documentId READ documentId CONSTANT)
    Q_PROPERTY(QString revision READ revision NOTIFY committedChanged)
    Q_PROPERTY(QindaTK::UndoService *undoService READ undoService NOTIFY historyChanged)
public:
    explicit SceneModel3D(QObject *parent = nullptr);
    ~SceneModel3D() override;
    QVariantList entities() const;
    QString selectedId() const { return m_selected; }
    QString lastError() const { return m_error; }
    bool editing() const { return !m_editId.isEmpty(); }
    bool canUndo() const;
    bool canRedo() const;
    QString documentId() const { return m_documentId; }
    QString revision() const { return QString::number(m_revision); }
    UndoService *undoService() const;
    Q_INVOKABLE QString addEntity(const QString &name, const QString &primitive,
                                  const QVector3D &position, const QColor &color);
    Q_INVOKABLE bool removeEntity(const QString &id);
    Q_INVOKABLE bool select(const QString &id);
    Q_INVOKABLE bool setTransform(const QString &id, const QVector3D &position,
                                  const QVector3D &eulerDegrees, float scale);
    Q_INVOKABLE bool setAppearance(const QString &id, const QColor &color, bool visible);
    Q_INVOKABLE bool setMaterial(const QString &id, const QColor &baseColor,
                                float roughness, float metalness, const QColor &emissiveColor);
    Q_INVOKABLE QVariantMap material(const QString &id) const;
    Q_INVOKABLE bool reparent(const QString &id, const QString &parentId, bool keepWorld);
    Q_INVOKABLE QVariantMap worldTransform(const QString &id) const;
    Q_INVOKABLE bool beginTransform(const QString &id);
    // Each preview is a world-space offset FROM BEGIN, never a cumulative delta.
    Q_INVOKABLE bool previewWorldTranslation(const QVector3D &delta, float snap = 0);
    // Constraint is free, x/y/z, xy/xz/yz; space is world or local.
    // Every preview is relative to beginTransform, including after another mode preview.
    Q_INVOKABLE bool previewConstrainedTranslation(const QVector3D &delta,
        const QString &constraint, const QString &space, float snap = 0);
    Q_INVOKABLE bool previewAxisTranslation(float distance, const QString &axis,
        const QString &space, float snap = 0);
    Q_INVOKABLE bool previewRotation(float degrees, const QString &axis,
        const QString &space, float snapDegrees = 0);
    Q_INVOKABLE bool previewUniformScale(float factor, float snap = 0);
    Q_INVOKABLE bool commitTransform();
    Q_INVOKABLE bool cancelTransform();
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE QString toJson() const;
    Q_INVOKABLE bool fromJson(const QString &json);
    Q_INVOKABLE bool saveToFile(const QString &path);
    Q_INVOKABLE bool loadFromFile(const QString &path);
    // DocumentModel: the private standalone history moves into a session.
    QObject *modelObject() const override { return const_cast<SceneModel3D *>(this); }
    DocumentId modelId() const override { return DocumentId::parse(m_documentId); }
    QString modelKind() const override { return QStringLiteral("scene3d"); }
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
    void selectionChanged();
    void errorChanged();
private:
    friend class SceneDocumentCodec;
    struct Entity {
        QString id, parent, name, primitive;
        QVector3D position;
        QQuaternion rotation;
        float scale = 1;
        QColor color;
        float roughness = 0.6f;
        float metalness = 0;
        QColor emissiveColor = QColor(Qt::black);
        bool visible = true;
        bool operator==(const Entity &) const = default;
    };
    struct Transform { QVector3D position; QQuaternion rotation; float scale = 1; };
    int index(const QString &id) const;
    Transform world(int i) const;
    bool validWorld() const;
    bool effectivelyVisible(int i) const;
    bool fail(const QString &message);
    struct SceneCommand;
    UndoService *history() const;
    void historyAttached(UndoService *service) override;
    bool commitState(const QVector<Entity> &before, const QVector<Entity> &after,
                     const QString &label);
    void remember();
    bool notify();
    void restore(const QVector<Entity> &state);
    static bool decodeNative(const QByteArray &bytes, QVector<Entity> *entities,
                             SnapshotHeader *header, QJsonObject *extensions, QString *problem);
    bool previewWorldTransform(const Transform &target);
    QVector<Entity> m_entities, m_before, m_pendingBefore;
    bool m_pending = false;
    UndoService *m_localHistory = nullptr;
    QMetaObject::Connection m_historyConnection;
    QString m_documentId;
    quint64 m_revision = 1;
    quint64 m_resetGeneration = 0;
    QString m_selected, m_editId, m_error;
    Transform m_startWorld;
};
} // namespace QindaTK
