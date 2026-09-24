// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "automation_model.h"

#include <QObject>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Deterministic transform evaluator for a host-owned 2D layer hierarchy.
// Source pixels, effect rendering and persistence remain outside this type.
class MotionComposition : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(AutomationModel *animationModel READ animationModel WRITE setAnimationModel NOTIFY animationModelChanged)
    Q_PROPERTY(QVariantList layers READ layers WRITE setLayers NOTIFY layersChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY evaluationChanged)
public:
    explicit MotionComposition(QObject *parent = nullptr);

    AutomationModel *animationModel() const { return m_animationModel; }
    void setAnimationModel(AutomationModel *model);
    QVariantList layers() const { return m_layers; }
    void setLayers(const QVariantList &layers);
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE QVariantMap evaluate(const QString &timeTicks);

signals:
    void animationModelChanged();
    void layersChanged();
    void evaluationChanged();

private:
    AutomationModel *m_animationModel = nullptr;
    QVariantList m_layers;
    QString m_lastError;
};

} // namespace QindaTK
