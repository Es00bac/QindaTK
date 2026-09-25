// SPDX-License-Identifier: LGPL-3.0-or-later
#include "print_preview_renderer_p.h"
#include "rich_text_document.h"
#include "rich_text_layout.h"
#include "rich_text_objects.h"

#include <limits>

namespace QindaTK::Print {

// The worker's own snapshot of one document generation.
class PreviewWorker final : public QObject {
    Q_OBJECT
public:
    explicit PreviewWorker(std::shared_ptr<std::atomic<int>> latest) : m_latest(std::move(latest)) {}

    void render(int generation, const QString &json, const QString &key, const Sheet &sheet, double zoom,
                bool grayscale)
    {
        if (generation < m_latest->load()) return; // superseded before its turn
        if (!json.isNull()) load(generation, json);
        QImage image;
        if (generation == m_generation && m_layout)
            image = renderSheet(*m_layout, sheet, zoom, grayscale, m_images.get());
        emit rendered(generation, key, image);
    }

signals:
    void rendered(int generation, const QString &key, const QImage &image);

private:
    void load(int generation, const QString &json)
    {
        m_layout.reset();
        m_document = std::make_unique<RichTextDocument>();
        m_generation = generation;
        if (!m_document->loadJson(json)) {
            m_document.reset();
            return;
        }
        m_layout = std::make_unique<RichTextLayout>();
        m_layout->setMode(RichTextLayout::Mode::Paged);
        m_layout->setDocument(m_document.get());
        m_layout->paginateAll();
        m_images = std::make_unique<RichTextImageRenderer>();
    }

    std::shared_ptr<std::atomic<int>> m_latest;
    int m_generation = -1;
    std::unique_ptr<RichTextDocument> m_document; // declared before the layout that reads it
    std::unique_ptr<RichTextLayout> m_layout;
    std::unique_ptr<RichTextImageRenderer> m_images;
};

PreviewRenderer::PreviewRenderer(QObject *parent)
    : QObject(parent), m_latest(std::make_shared<std::atomic<int>>(0))
{
    m_worker = new PreviewWorker(m_latest);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    // Queued: the image crosses from the worker thread to this object's.
    connect(m_worker, &PreviewWorker::rendered, this, &PreviewRenderer::rendered, Qt::QueuedConnection);
    m_thread.setObjectName(QStringLiteral("QindaTK print preview"));
    m_thread.start(QThread::LowPriority);
}

PreviewRenderer::~PreviewRenderer()
{
    m_latest->store(std::numeric_limits<int>::max()); // skip whatever is still queued
    m_thread.quit();
    m_thread.wait();
}

void PreviewRenderer::setGeneration(int generation)
{
    m_latest->store(generation);
}

void PreviewRenderer::request(int generation, const std::function<QString()> &json, const QString &key,
                              const Sheet &sheet, double zoom, bool grayscale)
{
    QString snapshot;
    if (generation != m_sentGeneration) {
        snapshot = json();
        if (snapshot.isNull()) snapshot = QStringLiteral(""); // a load that fails still replaces the old one
        m_sentGeneration = generation;
    }
    PreviewWorker *worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, generation, snapshot, key, sheet, zoom, grayscale] {
        worker->render(generation, snapshot, key, sheet, zoom, grayscale);
    }, Qt::QueuedConnection);
}

} // namespace QindaTK::Print

#include "print_preview_renderer.moc"
