// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_document.h"

#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/vector-authoring.md#appearance, D-106): the ordered
// effect stack of one canvas object in AppearanceStack's record shape
// ({id, name, kind, enabled, supported, parameters, masked,
// unsupportedReason}), refreshed after every document change. Each request is
// one undoable document edit; a kind the reference renderer does not draw is
// kept with its parameters and reported unsupported.
class CanvasAppearanceModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::CanvasDocumentAdapter *document READ document WRITE setDocument NOTIFY documentChanged)
    // Decimal-string object id (the key object of a selection).
    Q_PROPERTY(QString objectId READ objectId WRITE setObjectId NOTIFY objectIdChanged)
    Q_PROPERTY(QVariantList effects READ effects NOTIFY effectsChanged)
    Q_PROPERTY(bool editable READ editable NOTIFY effectsChanged)
    Q_PROPERTY(QStringList supportedKinds READ supportedKinds CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit CanvasAppearanceModel(QObject *parent = nullptr) : QObject(parent) {}
    CanvasDocumentAdapter *document() const { return m_document; }
    void setDocument(CanvasDocumentAdapter *document);
    QString objectId() const { return m_objectId; }
    void setObjectId(const QString &id);
    QVariantList effects() const { return m_effects; }
    bool editable() const { return m_editable; }
    QStringList supportedKinds() const;
    QString lastError() const { return m_lastError; }

    // Appends an effect with an id "<kind>-<n>" unique within the object and
    // returns it, or "" with lastError set.
    Q_INVOKABLE QString addEffect(const QString &kind, const QVariantMap &parameters = {});
    Q_INVOKABLE bool removeEffect(const QString &effectId);
    // `index` is the target position in stack order (0 renders first).
    Q_INVOKABLE bool moveEffect(const QString &effectId, int index);
    Q_INVOKABLE bool setEffectEnabled(const QString &effectId, bool enabled);
    // Replaces the parameters; a supported kind fills missing keys with defaults.
    Q_INVOKABLE bool setEffectParameters(const QString &effectId, const QVariantMap &parameters);
    Q_INVOKABLE QVariantMap effect(const QString &effectId) const;

signals:
    void documentChanged();
    void objectIdChanged();
    void effectsChanged();
    void errorChanged();
    void applied();

private:
    const CanvasObject *target() const;
    qsizetype indexOf(const QString &effectId) const;
    bool commit(const QVector<CanvasEffect> &effects, const QString &label);
    bool fail(const QString &message);
    void refresh();

    QPointer<CanvasDocumentAdapter> m_document;
    QString m_objectId;
    QVariantList m_effects;
    bool m_editable = false;
    QString m_lastError;
};

} // namespace QindaTK
