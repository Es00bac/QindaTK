// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "automation_model.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT: MotionComposition saves layer records and AutomationModel
// keys; MotionFrameRenderer's decoded pixels stay external behind sourceId.
// This type records effect descriptors but does not execute them.
class MotionComposition : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(AutomationModel *animationModel READ animationModel WRITE setAnimationModel NOTIFY animationModelChanged)
    Q_PROPERTY(QVariantList layers READ layers WRITE setLayers NOTIFY layersChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY evaluationChanged)
public:
    static constexpr int kSchemaVersion = 1;
    static QString schemaId() { return QStringLiteral("qindatk.motion-composition"); }

    explicit MotionComposition(QObject *parent = nullptr);
    AutomationModel *animationModel() const { return m_animationModel; }
    void setAnimationModel(AutomationModel *model);
    QVariantList layers() const { return m_layers; }
    void setLayers(const QVariantList &layers);
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE QVariantMap evaluate(const QString &timeTicks);
    Q_INVOKABLE QVariantMap contentMap() const;
    Q_INVOKABLE QVariantMap loadContent(const QVariantMap &content);
    Q_INVOKABLE QString toJson() const;
    Q_INVOKABLE QVariantMap loadJson(const QString &json);
    Q_INVOKABLE bool saveToFile(const QUrl &url);
    Q_INVOKABLE QVariantMap loadFromFile(const QUrl &url);

signals:
    void animationModelChanged();
    void layersChanged();
    void evaluationChanged();

private:
    QVariantMap reject(const QString &message);
    void setError(const QString &message);
    AutomationModel *m_animationModel = nullptr;
    QVariantList m_layers;
    QString m_lastError;
};

} // namespace QindaTK
