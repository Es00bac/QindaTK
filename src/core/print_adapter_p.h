// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "print_adapter.h"
#include "rich_text_document.h"
#include "rich_text_layout.h"
#include "rich_text_objects.h"

namespace QindaTK {

// A running print job. AGENT-GUARD (D-131): the job prints `snapshot`, a copy
// of the document taken when print() was called, through its own layout, so
// edits during the job cannot shift its pages. Member order matters: the
// device is destroyed (and aborted) before the backend that made it, and the
// layout before the document it reads.
struct PrintAdapter::Job {
    QString id;
    QString status;                  // pending, running, cancelling
    QString printerId;
    QString outputPath;
    Print::Ticket ticket;
    Print::BackendPtr backend;
    std::unique_ptr<RichTextDocument> snapshot;
    std::unique_ptr<RichTextLayout> layout;
    std::unique_ptr<RichTextImageRenderer> images;
    std::unique_ptr<Print::Device> device;
    QVector<Print::Sheet> sheets;
    int next = 0;                    // the sheet the next step paints
    bool cancelRequested = false;
};

namespace PrintDetail {
QString errorCodeName(ErrorCode code);
QVariantMap idleJob();
// Zero-based pages as one-based ranges in output order ("2-3, 5").
QString pagesText(const QVector<int> &pages);
} // namespace PrintDetail

} // namespace QindaTK
