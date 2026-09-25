// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "print_output.h"
#include <QImage>
#include <QObject>
#include <QThread>
#include <atomic>
#include <functional>
#include <memory>

namespace QindaTK::Print {

class PreviewWorker;

// Renders preview sheets on a thread of its own (OFF-12: "render preview
// asynchronously"). The worker lays out a snapshot of the document, taken
// once per generation, with its own layout and image decoder, and paints
// through Print::renderSheet, the painter jobs and PDF files use (D-130).
// AGENT-GUARD: nothing the worker touches may be shared with the GUI
// thread's document or layout; code it reaches that keeps a static cache
// must lock it or make it thread_local (see objectCharAdvance).
class PreviewRenderer final : public QObject {
    Q_OBJECT
public:
    explicit PreviewRenderer(QObject *parent = nullptr);
    ~PreviewRenderer() override; // stops the thread and waits for it

    // Requests newer than every older generation: queued requests of an
    // older generation are skipped, and their results never arrive.
    void setGeneration(int generation);
    // Queues `sheet` at `zoom`. `json` is the document for `generation`; the
    // renderer sends it to the worker only when the generation is new.
    void request(int generation, const std::function<QString()> &json, const QString &key,
                 const Sheet &sheet, double zoom, bool grayscale);

signals:
    // Delivered on the GUI thread; an empty image means the render failed.
    void rendered(int generation, const QString &key, const QImage &image);

private:
    QThread m_thread;
    PreviewWorker *m_worker = nullptr; // lives on m_thread, deleted when it finishes
    std::shared_ptr<std::atomic<int>> m_latest;
    int m_sentGeneration = -1;
};

} // namespace QindaTK::Print
