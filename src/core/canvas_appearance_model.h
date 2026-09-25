// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_document.h"
#include "canvas_selection_model.h"

#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/vector-authoring.md#appearance, D-106): the ordered
// effect stack of one canvas object in AppearanceStack's record shape
// ({id, name, kind, enabled, supported, parameters, masked,
// unsupportedReason}), refreshed after every document change, and its fixed
// paint entries (`paints`: fill, pattern, stroke, in paint order, below the
// effects). Each request is one undoable document edit; a kind the reference
// renderer does not draw is kept with its parameters and reported
// unsupported. With a `selectionModel`, the object is the selection's key
// (first) object.
class CanvasAppearanceModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::CanvasDocumentAdapter *document READ document WRITE setDocument NOTIFY documentChanged)
    // Decimal-string object id (the key object of a selection).
    Q_PROPERTY(QString objectId READ objectId WRITE setObjectId NOTIFY objectIdChanged)
    Q_PROPERTY(QindaTK::CanvasSelectionModel *selectionModel READ selectionModel WRITE setSelectionModel NOTIFY selectionModelChanged)
    // [{id, kind, name, present, detail}] for "fill", "pattern", "stroke".
    Q_PROPERTY(QVariantList paints READ paints NOTIFY effectsChanged)
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
    CanvasSelectionModel *selectionModel() const { return m_selectionModel; }
    void setSelectionModel(CanvasSelectionModel *model);
    QVariantList paints() const { return m_paints; }
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
    // Sets the given parameters; a supported kind keeps its current value for
    // every key left out. An unsupported kind's map replaces the old one.
    Q_INVOKABLE bool setEffectParameters(const QString &effectId, const QVariantMap &parameters);
    Q_INVOKABLE QVariantMap effect(const QString &effectId) const;

signals:
    void documentChanged();
    void objectIdChanged();
    void selectionModelChanged();
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
    QPointer<CanvasSelectionModel> m_selectionModel;
    QString m_objectId;
    QVariantList m_paints;
    QVariantList m_effects;
    bool m_editable = false;
    QString m_lastError;
};

} // namespace QindaTK
