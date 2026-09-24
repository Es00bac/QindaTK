// SPDX-License-Identifier: LGPL-3.0-or-later
#include "rich_text_asset_renderer.h"

#include "asset_provider.h"
#include "rich_text_document.h"
#include <QMutexLocker>
#include <QPainter>
#include <algorithm>

namespace QindaTK {

RichTextAssetRenderer::RichTextAssetRenderer(QObject *parent) : QObject(parent) {}
RichTextAssetRenderer::~RichTextAssetRenderer() { clear(); }

QString RichTextAssetRenderer::resourceId(const QString &source)
{
    if (!source.startsWith(QLatin1String("asset:"))) return {};
    QString id = source.mid(6);
    if (id.startsWith(QLatin1String("//"))) id.remove(0, 2);
    return id;
}

void RichTextAssetRenderer::clear()
{
    QStringList subscriptions;
    {
        QMutexLocker lock(&m_mutex);
        for (const Record &record : std::as_const(m_records))
            if (!record.subscription.isEmpty()) subscriptions.append(record.subscription);
        m_records.clear();
    }
    if (m_provider)
        for (const QString &id : std::as_const(subscriptions)) m_provider->release(id);
    emit updated();
}

void RichTextAssetRenderer::setProvider(AssetProvider *provider)
{
    if (m_provider == provider) return;
    disconnect(m_providerChanged);
    disconnect(m_providerDestroyed);
    clear();
    m_provider = provider;
    if (provider) {
        m_providerChanged = connect(provider, &AssetProvider::assetChanged,
                                    this, &RichTextAssetRenderer::assetChanged);
        m_providerDestroyed = connect(provider, &QObject::destroyed, this, [this] {
            m_provider = nullptr;
            clear();
        });
    }
    synchronize();
}

void RichTextAssetRenderer::setDocument(RichTextDocument *document)
{
    if (m_document == document) return;
    disconnect(m_documentChanged);
    disconnect(m_documentDestroyed);
    clear();
    m_document = document;
    if (document) {
        m_documentChanged = connect(document, &RichTextDocument::contentChanged,
                                    this, [this] { synchronize(); });
        m_documentDestroyed = connect(document, &QObject::destroyed, this, [this] {
            m_document = nullptr;
            clear();
        });
    }
    synchronize();
}

void RichTextAssetRenderer::synchronize()
{
    if (!m_provider || !m_document) return;
    QHash<QString, QSize> wanted;
    for (const RichText::RichObject &object : m_document->objects()) {
        if (object.kind != QLatin1String("image")) continue;
        const QString id = resourceId(object.source);
        if (id.isEmpty()) continue;
        const QSize size(std::clamp(int(object.widthPt * 4.0 / 3.0), 1, 4096),
                         std::clamp(int(object.heightPt * 4.0 / 3.0), 1, 4096));
        const QSize previous = wanted.value(object.source);
        wanted.insert(object.source, previous.expandedTo(size));
    }
    QStringList retired;
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_records.begin(); it != m_records.end();) {
            if (wanted.contains(it.key())) { ++it; continue; }
            if (!it->subscription.isEmpty()) retired.append(it->subscription);
            it = m_records.erase(it);
        }
    }
    for (const QString &subscription : std::as_const(retired)) m_provider->release(subscription);
    for (auto it = wanted.cbegin(); it != wanted.cend(); ++it) {
        {
            QMutexLocker lock(&m_mutex);
            if (m_records.contains(it.key())) continue;
            m_records.insert(it.key(), {});
        }
        const QVariantMap options{{QStringLiteral("size"),
                                   QVariantMap{{QStringLiteral("width"), it.value().width()},
                                               {QStringLiteral("height"), it.value().height()}}}};
        const QString subscription = m_provider->requestAsset(resourceId(it.key()),
                                                               QStringLiteral("thumbnail"), options);
        {
            QMutexLocker lock(&m_mutex);
            auto found = m_records.find(it.key());
            if (found != m_records.end()) {
                found->subscription = subscription;
                found->missing = subscription.isEmpty();
            }
        }
        if (!subscription.isEmpty()) assetChanged(subscription); // cached result may already be ready
    }
    emit updated();
}

void RichTextAssetRenderer::assetChanged(const QString &subscription)
{
    if (!m_provider || subscription.isEmpty()) return;
    QString source;
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_records.cbegin(); it != m_records.cend(); ++it)
            if (it->subscription == subscription) { source = it.key(); break; }
    }
    if (source.isEmpty()) return;
    const QVariantMap state = m_provider->asset(subscription);
    const QString status = state.value(QStringLiteral("state")).toString();
    if (status == QLatin1String("stale")) {
        {
            QMutexLocker lock(&m_mutex);
            m_records.remove(source);
        }
        m_provider->release(subscription);
        emit updated();
        QMetaObject::invokeMethod(this, [this] { synchronize(); }, Qt::QueuedConnection);
        return;
    }
    if (status != QLatin1String("ready") && status != QLatin1String("failed")
        && status != QLatin1String("missing")) return;
    QImage decoded;
    if (status == QLatin1String("ready")) {
        const QByteArray bytes = state.value(QStringLiteral("data")).toMap()
                                    .value(QStringLiteral("bytes")).toByteArray();
        decoded.loadFromData(bytes);
        if (decoded.sizeInBytes() > 64 * 1024 * 1024) decoded = {};
    }
    {
        QMutexLocker lock(&m_mutex);
        auto found = m_records.find(source);
        if (found == m_records.end() || found->subscription != subscription) return;
        found->image = decoded;
        found->missing = decoded.isNull();
    }
    emit updated();
}

void RichTextAssetRenderer::paint(QPainter *painter, const QRectF &rect,
                                  const RichText::RichObject &object,
                                  const RichText::PaintContext &context)
{
    if (object.kind != QLatin1String("image") || resourceId(object.source).isEmpty()) {
        m_files.paint(painter, rect, object, context);
        return;
    }
    QImage image;
    bool missing = false;
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_records.constFind(object.source);
        if (it != m_records.cend()) { image = it->image; missing = it->missing; }
    }
    if (image.isNull()) {
        RichTextImageRenderer::paintPlaceholder(painter, rect, object, context, missing);
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawImage(rect, image);
    painter->restore();
}

} // namespace QindaTK
