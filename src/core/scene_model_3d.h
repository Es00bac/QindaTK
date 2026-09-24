// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <QObject>
#include <QColor>
#include <QQuaternion>
#include <QVector3D>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
// Scene coordinates are right-handed world units. Uniform positive scales make
// hierarchical rotations and keep-world reparenting exact without hidden shear.
class SceneModel3D : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList entities READ entities NOTIFY changed)
    Q_PROPERTY(QString selectedId READ selectedId WRITE select NOTIFY selectionChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(bool editing READ editing NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)
public:
    explicit SceneModel3D(QObject *parent = nullptr) : QObject(parent) {}
    QVariantList entities() const;
    QString selectedId() const { return m_selected; }
    QString lastError() const { return m_error; }
    bool editing() const { return !m_editId.isEmpty(); }
    bool canUndo() const { return !m_undo.isEmpty() && !editing(); }
    bool canRedo() const { return !m_redo.isEmpty() && !editing(); }
    Q_INVOKABLE QString addEntity(const QString &name, const QString &primitive,
                                  const QVector3D &position, const QColor &color);
    Q_INVOKABLE bool removeEntity(const QString &id);
    Q_INVOKABLE bool select(const QString &id);
    Q_INVOKABLE bool setTransform(const QString &id, const QVector3D &position,
                                  const QVector3D &eulerDegrees, float scale);
    Q_INVOKABLE bool setAppearance(const QString &id, const QColor &color, bool visible);
    Q_INVOKABLE bool reparent(const QString &id, const QString &parentId, bool keepWorld);
    Q_INVOKABLE QVariantMap worldTransform(const QString &id) const;
    Q_INVOKABLE bool beginTransform(const QString &id);
    // Each preview is a world-space offset FROM BEGIN, never a cumulative delta.
    Q_INVOKABLE bool previewWorldTranslation(const QVector3D &delta, float snap = 0);
    Q_INVOKABLE bool commitTransform();
    Q_INVOKABLE bool cancelTransform();
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE QString toJson() const;
    Q_INVOKABLE bool fromJson(const QString &json);
    Q_INVOKABLE bool saveToFile(const QString &path);
    Q_INVOKABLE bool loadFromFile(const QString &path);
signals:
    void changed();
    void selectionChanged();
    void errorChanged();
private:
    struct Entity {
        QString id, parent, name, primitive;
        QVector3D position;
        QQuaternion rotation;
        float scale = 1;
        QColor color;
        bool visible = true;
        bool operator==(const Entity &) const = default;
    };
    struct Transform { QVector3D position; QQuaternion rotation; float scale = 1; };
    int index(const QString &id) const;
    Transform world(int i) const;
    bool validWorld() const;
    bool effectivelyVisible(int i) const;
    bool fail(const QString &message);
    void remember();
    void notify();
    void restore(const QVector<Entity> &state);
    QVector<Entity> m_entities, m_before;
    QVector<QVector<Entity>> m_undo, m_redo;
    QString m_selected, m_editId, m_error;
    Transform m_startWorld;
};
} // namespace QindaTK
