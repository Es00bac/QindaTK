// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_document.h"
#include "color_picker_model.h"
#include "gradient_editor_model.h"
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// AGENT-CONTRACT (docs/vector-authoring.md#style-binding): style edits for the
// canvas reference document and the style of `selectedIds` for binding
// StrokeStyleEditor (`strokeStyle`) and FillStyleEditor (`fillStyle`). Every
// selected object changes in one reversible document edit; an invalid or
// unsupported value, or a hidden or locked object, rejects the whole edit.
// The published styles follow every document change, undo included.
class CanvasStyleModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::CanvasDocumentAdapter *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QVariantList selectedIds READ selectedIds WRITE setSelectedIds NOTIFY selectedIdsChanged)
    // styleOf(selectedIds): the first object's values plus `count` and `mixed`.
    Q_PROPERTY(QVariantMap selectionStyle READ selectionStyle NOTIFY selectionStyleChanged)
    // The same selection as StrokeStyleEditor.style and FillStyleEditor.fill.
    Q_PROPERTY(QVariantMap strokeStyle READ strokeStyle NOTIFY selectionStyleChanged)
    Q_PROPERTY(QVariantMap fillStyle READ fillStyle NOTIFY selectionStyleChanged)
    // Built-in pattern swatches as FillStyleEditor.patterns records.
    Q_PROPERTY(QVariantList patterns READ patterns CONSTANT)
    Q_PROPERTY(QStringList blendModes READ blendModes CONSTANT)
public:
    explicit CanvasStyleModel(QObject *parent = nullptr) : QObject(parent) {}
    CanvasDocumentAdapter *document() const { return m_document; }
    void setDocument(CanvasDocumentAdapter *document);
    QString lastError() const { return m_lastError; }
    QVariantList selectedIds() const { return m_selectedIds; }
    void setSelectedIds(const QVariantList &ids);
    QVariantMap selectionStyle() const { return m_selectionStyle; }
    QVariantMap strokeStyle() const { return m_strokeStyle; }
    QVariantMap fillStyle() const { return m_fillStyle; }
    QVariantList patterns() const;
    QStringList blendModes() const;
    // style keys: fill, stroke, strokeWidth, dash, cap, join, fillRule,
    // alignment, miterLimit, opacity, blend and pattern (a swatch id, a
    // {kind, ink, spacing, angle, weight} map, or null to clear). Null colour
    // clears that channel.
    Q_INVOKABLE bool apply(const QVariantList &selectedIds, const QVariantMap &style);
    Q_INVOKABLE bool applyPicker(const QVariantList &selectedIds, ColorPickerModel *picker);
    Q_INVOKABLE bool applyGradient(const QVariantList &selectedIds, GradientEditorModel *gradient);
    Q_INVOKABLE QVariantMap styleOf(const QVariantList &ids) const;
    // Panel requests for `selectedIds`: only the fields that differ from the
    // published strokeStyle/fillStyle are applied, so a value the user did
    // not touch keeps each object's own (possibly mixed) value.
    Q_INVOKABLE bool applyStrokeStyle(const QVariantMap &descriptor);
    Q_INVOKABLE bool applyFillStyle(const QVariantMap &descriptor);
signals:
    void documentChanged();
    void errorChanged();
    void applied(int changedObjects);
    void selectedIdsChanged();
    void selectionStyleChanged();
private:
    bool fail(const QString &message);
    bool succeed(int changedObjects);
    bool applyManaged(const QVariantList &selectedIds, const QByteArray &payload,
                      const QColor &preview);
    void refresh(); // canvas_style_model_selection.cpp
    QPointer<CanvasDocumentAdapter> m_document;
    QString m_lastError;
    QVariantList m_selectedIds;
    QVariantMap m_selectionStyle;
    QVariantMap m_strokeStyle;
    QVariantMap m_fillStyle;
    QString m_dashMode; // "custom" was chosen before any custom dash values
};

} // namespace QindaTK
