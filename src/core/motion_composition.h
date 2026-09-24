// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "automation_model.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT: MotionComposition saves layer records and AutomationModel
// keys; MotionFrameRenderer keeps decoded pixels external behind sourceId and
// executes each layer's ordered effects before transform/composition.
// AGENT-CONTRACT: every edit gesture is one transaction. Key, path and
// tangent edits are AutomationModel transactions (applyBatch spans x and y);
// layer-record edits (insertEffect) use this object's bounded history, which
// a host replacement through setLayers()/loadContent() clears. The host
// orders the two histories (docs/controls.md, "Motion and keyframe surfaces").
class MotionComposition : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::AutomationModel *animationModel READ animationModel WRITE setAnimationModel NOTIFY animationModelChanged)
    Q_PROPERTY(QVariantList layers READ layers WRITE setLayers NOTIFY layersChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY evaluationChanged)
    Q_PROPERTY(QString revision READ revisionText NOTIFY layersChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
public:
    static constexpr int kSchemaVersion = 1;
    static constexpr int kHistoryLimit = 128;
    static constexpr int kMaxRemapPairs = 1024;
    static QString schemaId() { return QStringLiteral("qindatk.motion-composition"); }

    explicit MotionComposition(QObject *parent = nullptr);
    AutomationModel *animationModel() const { return m_animationModel; }
    void setAnimationModel(AutomationModel *model);
    QVariantList layers() const { return m_layers; }
    void setLayers(const QVariantList &layers);
    QString lastError() const { return m_lastError; }
    QString revisionText() const { return QString::number(m_revision); }
    bool canUndo() const { return m_cursor > 0; }
    bool canRedo() const { return m_cursor < m_history.size(); }
    // Empty when the records pass the structural checks loadContent applies.
    static QString layerRecordError(const QVariantList &layers);

    // Layer time = timeRemap(timeTicks - timeOffsetTicks); each evaluated
    // layer reports its exact decimal localTicks.
    Q_INVOKABLE QVariantMap evaluate(const QString &timeTicks);
    Q_INVOKABLE QVariantMap contentMap() const;
    Q_INVOKABLE QVariantMap loadContent(const QVariantMap &content);
    Q_INVOKABLE QString toJson() const;
    Q_INVOKABLE QVariantMap loadJson(const QString &json);
    Q_INVOKABLE bool saveToFile(const QUrl &url);
    Q_INVOKABLE QVariantMap loadFromFile(const QUrl &url);

    // Layer-record transactions (motion_composition_edit.cpp).
    Q_INVOKABLE QVariantMap insertEffect(const QString &layerId, const QVariantMap &effect, int index = -1,
                                         const QString &expectedRevision = QString());
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    // Spatial path editing of a layer's animated x/y curves at an exact
    // composition time. World coordinates are frame pixels; edits convert
    // them into the parent's space and target the keys at the layer time.
    Q_INVOKABLE QVariantMap pathHandles(const QString &layerId, const QString &timeTicks);
    Q_INVOKABLE QVariantMap movePathKey(const QString &layerId, const QString &timeTicks,
                                        double worldX, double worldY);
    Q_INVOKABLE QVariantMap setPathTangent(const QString &layerId, const QString &timeTicks,
                                           const QString &side, double worldX, double worldY);

signals:
    void animationModelChanged();
    void layersChanged();
    void evaluationChanged();
    void historyChanged();
    void layerEditCommitted(const QString &transactionId, const QString &label);

private:
    struct LayerEdit {
        QString transactionId;
        QString label;
        QVariantList before;
        QVariantList after;
    };
    QVariantMap reject(const QString &message);
    void setError(const QString &message);
    void replaceLayers(const QVariantList &layers);
    QVariantMap commitLayers(const QVariantList &after, const QString &label);
    AutomationModel *m_animationModel = nullptr;
    QVariantList m_layers;
    QString m_lastError;
    quint64 m_revision = 0;
    QVector<LayerEdit> m_history;
    qsizetype m_cursor = 0;
};

} // namespace QindaTK
