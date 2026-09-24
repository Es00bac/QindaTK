// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "rich_text_objects.h"
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QPointer>

namespace QindaTK {
class AssetProvider;
class RichTextDocument;

// Resolves asset: image objects on the GUI thread and hands immutable decoded
// images to the painted tile. paint() never calls an asynchronous service.
class RichTextAssetRenderer final : public QObject, public RichTextObjectRenderer {
    Q_OBJECT
public:
    explicit RichTextAssetRenderer(QObject *parent = nullptr);
    ~RichTextAssetRenderer() override;
    AssetProvider *provider() const { return m_provider; }
    void setProvider(AssetProvider *provider);
    void setDocument(RichTextDocument *document);
    void paint(QPainter *painter, const QRectF &rect, const RichText::RichObject &object,
               const RichText::PaintContext &context) override;
signals:
    void updated();
private:
    struct Record {
        QString subscription;
        QImage image;
        bool missing = false;
    };
    void clear();
    void synchronize();
    void assetChanged(const QString &subscription);
    static QString resourceId(const QString &source);
    QPointer<AssetProvider> m_provider;
    QPointer<RichTextDocument> m_document;
    QMetaObject::Connection m_providerChanged;
    QMetaObject::Connection m_providerDestroyed;
    QMetaObject::Connection m_documentChanged;
    QMetaObject::Connection m_documentDestroyed;
    QMutex m_mutex;
    QHash<QString, Record> m_records;
    RichTextImageRenderer m_files;
};
} // namespace QindaTK
