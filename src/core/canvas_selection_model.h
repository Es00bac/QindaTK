// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "canvas_document.h"

#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
// One selection for all views of a document. Tools may propose IDs; the model
// retains only live, visible and unlocked objects and owns the final order.
class CanvasSelectionModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::CanvasDocumentAdapter *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QVariantList selectedIds READ selectedIds WRITE setSelectedIds NOTIFY selectionChanged)
public:
    explicit CanvasSelectionModel(QObject *parent = nullptr) : QObject(parent) {}
    CanvasDocumentAdapter *document() const { return m_document; }
    void setDocument(CanvasDocumentAdapter *document);
    const QVector<CanvasObjectId> &selection() const { return m_selection; }
    void setSelection(const QVector<CanvasObjectId> &ids);
    QVariantList selectedIds() const;
    void setSelectedIds(const QVariantList &ids);
    Q_INVOKABLE void clear() { setSelection({}); }
signals:
    void documentChanged();
    void selectionChanged();
private:
    void prune();
    QPointer<CanvasDocumentAdapter> m_document;
    QVector<CanvasObjectId> m_selection;
};
} // namespace QindaTK
