// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_document.h"
#include "canvas_selection_model.h"
#include "color_picker_model.h"
#include "gradient_editor_model.h"
#include <QHash>
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
// The published styles follow every document change, undo included. With a
// `selectionModel` the selection is the canvas's own (ToolController's).
class CanvasStyleModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::CanvasDocumentAdapter *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
    Q_PROPERTY(QVariantList selectedIds READ selectedIds WRITE setSelectedIds NOTIFY selectedIdsChanged)
    // When set, selectedIds follows it (and writing selectedIds selects there).
    Q_PROPERTY(QindaTK::CanvasSelectionModel *selectionModel READ selectionModel WRITE setSelectionModel NOTIFY selectionModelChanged)
    // The nested gradient editor for fill kind "gradient": it shows the key
    // object's gradient and supplies one for objects that had none.
    Q_PROPERTY(QindaTK::GradientEditorModel *gradientEditor READ gradientEditor WRITE setGradientEditor NOTIFY gradientEditorChanged)
    // Stroke width units: "document", "px", "pt", "mm", "in" (docs/vector-authoring.md#stroke-units).
    Q_PROPERTY(QStringList strokeUnits READ strokeUnits CONSTANT)
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
    CanvasSelectionModel *selectionModel() const { return m_selectionModel; }
    void setSelectionModel(CanvasSelectionModel *model);
    GradientEditorModel *gradientEditor() const { return m_gradientEditor; }
    void setGradientEditor(GradientEditorModel *editor);
    QStringList strokeUnits() const;
    // Document units per `unit` (1 for "document"); 0 for an unknown unit.
    static qreal documentUnitsPer(const QString &unit);
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
    // Panel requests for `selectedIds`. A descriptor's `changed` list names
    // the fields the user set; exactly those apply, even when a value equals
    // the one published for a mixed key (make every object match it).
    // Without `changed`, the fields that differ from the published
    // strokeStyle/fillStyle apply. Untouched fields keep each object's own
    // (possibly mixed) value. `width` is in the descriptor's `unit`; a unit
    // change alone is presentation and edits nothing. A fill kind change
    // restores each object's own earlier settings of that kind when it had
    // some (colour, pattern, gradient), else applies the descriptor's.
    Q_INVOKABLE bool applyStrokeStyle(const QVariantMap &descriptor);
    Q_INVOKABLE bool applyFillStyle(const QVariantMap &descriptor);
signals:
    void documentChanged();
    void errorChanged();
    void applied(int changedObjects);
    void selectedIdsChanged();
    void selectionStyleChanged();
    void selectionModelChanged();
    void gradientEditorChanged();
private:
    bool fail(const QString &message);
    bool succeed(int changedObjects);
    bool applyManaged(const QVariantList &selectedIds, const QByteArray &payload,
                      const QColor &preview);
    void refresh(); // canvas_style_model_selection.cpp
    void adoptSelection(const QVariantList &ids);
    // canvas_style_model_fill.cpp
    struct FillRecall {
        QColor solid;
        QByteArray solidManaged;
        bool hasPattern = false;
        CanvasPattern pattern;
        QColor patternFill;
        QByteArray gradient;
        QColor gradientPreview;
    };
    void observe(const QVector<CanvasObjectId> &ids);
    QVariantMap fillDescriptor(const QVariantMap &style) const;
    bool applyFillKind(const QString &kind, const QVariantMap &settings);
    QVector<CanvasObjectId> selection() const;
    void syncGradientEditor();
    QPointer<CanvasDocumentAdapter> m_document;
    QString m_lastError;
    QVariantList m_selectedIds;
    QVariantMap m_selectionStyle;
    QVariantMap m_strokeStyle;
    QVariantMap m_fillStyle;
    QString m_dashMode; // "custom" was chosen before any custom dash values
    QPointer<CanvasSelectionModel> m_selectionModel;
    QPointer<GradientEditorModel> m_gradientEditor;
    QString m_strokeUnit = QStringLiteral("document");
    // The width last typed, so switching units and back shows it exactly.
    struct WidthIntent { QString unit; double value = 0; double documentWidth = -1; } m_widthIntent;
    // Each object's last seen settings per fill kind (editor state, bounded
    // by the document's objects), so a kind change can restore them.
    QHash<CanvasObjectId, FillRecall> m_recall;
};

namespace CanvasStyleModelMixed {
// The style keys of `style.mixed` listed in `keys` ({style key, field}),
// renamed to the panel descriptor's field names.
QStringList fields(const QVariantMap &style, std::initializer_list<std::pair<const char *, const char *>> keys);
} // namespace CanvasStyleModelMixed

} // namespace QindaTK
