// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "color_engine.h"
#include "edit_transaction.h"
#include <QColor>
#include <QJsonObject>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {

// Owns typed document color targets while Tk.ColorPicker owns only gestures.
class ColorPickerModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList targetIds READ targetIds WRITE setTargetIds NOTIFY changed)
    Q_PROPERTY(QColor previewColor READ previewColor NOTIFY changed)
    Q_PROPERTY(QVariantMap typedValue READ typedValue NOTIFY changed)
    Q_PROPERTY(bool editing READ editing NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
public:
    explicit ColorPickerModel(QObject *parent = nullptr);
    QStringList targetIds() const { return m_targetIds; }
    void setTargetIds(const QStringList &ids);
    QColor previewColor() const;
    QVariantMap typedValue() const;
    bool editing() const { return m_transaction.active(); }
    QString errorMessage() const { return m_error; }
    PropertyDocument *document() { return &m_document; }
    UndoService *undoService() { return &m_undo; }
    ColorContextSnapshot colorContext() const { return m_context; }
    void setColorContext(const ColorContextSnapshot &context);
    bool addTarget(const QString &id, const ColorValue &value);
    ColorValue colorValue(const QString &id) const;
    Q_INVOKABLE bool previewSrgb(const QColor &color);
    Q_INVOKABLE bool commitEdit();
    Q_INVOKABLE void cancelEdit();
    Q_INVOKABLE bool undo() { return m_undo.undo(); }
    Q_INVOKABLE bool redo() { return m_undo.redo(); }
    Q_INVOKABLE QByteArray saveJson() const;
    Q_INVOKABLE bool loadJson(const QByteArray &bytes);
signals:
    void changed();
private:
    bool resolveTargets() const;
    void setError(const OperationError &error);
    ColorValue readValue(const QString &id) const;
    PropertyDocument m_document;
    UndoService m_undo;
    EditTransaction m_transaction;
    mutable ColorEngine m_engine;
    ColorContextSnapshot m_context;
    QStringList m_targetIds;
    QString m_error;
    QColor m_preview;
    quint64 m_editGeneration = 0;
};

} // namespace QindaTK
