// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_document.h"
#include "canvas_selection_model.h"

#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
// Applies AlignDistributePanel requests to a document in one CanvasEdit.
// All coordinates are document units; IDs arrive as decimal strings to keep
// 64-bit object identity exact across QML's Number representation.
//
// AGENT-CONTRACT (docs/canvas.md#alignment-commands, D-095): the reference
// choices come from this domain model, not the panel. `referenceOptions`
// lists selection, key object and page, plus the "artboard" reference (the
// board named by `artboardId`) when the document is a CanvasDocument with
// artboards; AlignDistributePanel.referenceOptions binds to it.
class CanvasAlignmentModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::CanvasDocumentAdapter *document READ document WRITE setDocument NOTIFY documentChanged)
    // Optional shared selection; `selectedIds` mirrors it as decimal strings.
    Q_PROPERTY(QindaTK::CanvasSelectionModel *selectionModel READ selectionModel WRITE setSelectionModel NOTIFY selectionModelChanged)
    Q_PROPERTY(QStringList selectedIds READ selectedIds NOTIFY selectionChanged)
    Q_PROPERTY(QString artboardId READ artboardId WRITE setArtboardId NOTIFY referencesChanged)
    // [{value, text, available}] in menu order.
    Q_PROPERTY(QVariantList referenceOptions READ referenceOptions NOTIFY referencesChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    explicit CanvasAlignmentModel(QObject *parent = nullptr) : QObject(parent) {}
    CanvasDocumentAdapter *document() const { return m_document; }
    void setDocument(CanvasDocumentAdapter *document);
    CanvasSelectionModel *selectionModel() const { return m_selectionModel; }
    void setSelectionModel(CanvasSelectionModel *model);
    QStringList selectedIds() const;
    QString artboardId() const { return m_artboardId; }
    void setArtboardId(const QString &id);
    QVariantList referenceOptions() const;
    QString lastError() const { return m_lastError; }
    // referenceMode: "selection", "keyObject", "page" or "artboard".
    Q_INVOKABLE bool apply(const QString &operation, const QVariantList &selectedIds,
                           const QString &referenceMode = QStringLiteral("selection"),
                           const QString &keyObjectId = {}, qreal spacing = 0);
signals:
    void documentChanged();
    void selectionModelChanged();
    void selectionChanged();
    void referencesChanged();
    void errorChanged();
    void applied(const QString &operation, int changedObjects);
private:
    bool fail(const QString &message);
    const CanvasArtboard *referenceBoard() const;
    QPointer<CanvasDocumentAdapter> m_document;
    QPointer<CanvasSelectionModel> m_selectionModel;
    QString m_artboardId;
    QString m_lastError;
};
} // namespace QindaTK
