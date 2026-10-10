// SPDX-License-Identifier: LGPL-3.0-or-later
// RichTextDocument: per-paragraph host data (D-325, docs/rich-text.md#paragraph-data).
// The data lives in ParagraphFormat, so it is saved by the codec, undone by
// StepParagraphFormat, copied with a fragment and inherited by a split.
#include "rich_text_codec.h"
#include "rich_text_document.h"
#include "rich_text_ops_p.h"

#include <QJsonDocument>

namespace QindaTK {
using namespace RichText;
using namespace RichText::Detail;

QVariantMap RichTextDocument::paragraphData(const QString &paragraphId) const
{
    const Paragraph *p = paragraph(QUuid(paragraphId));
    return p ? p->format.data.toVariantMap() : QVariantMap{};
}

bool RichTextDocument::setParagraphData(const QString &paragraphId, const QVariantMap &data)
{
    Paragraph *p = paragraph(QUuid(paragraphId));
    if (!p) return !reject(ErrorCode::InvalidArgument, QStringLiteral("Unknown paragraph"));
    const QJsonObject next = QJsonObject::fromVariantMap(data);
    if (QJsonDocument(next).toJson(QJsonDocument::Compact).size() > RichTextCodec::MaximumParagraphData)
        return !reject(ErrorCode::InvalidArgument, QStringLiteral("Paragraph data exceeds 64 KiB"));
    if (p->format.data == next) return true;
    // AGENT-NOTE: host data is not content, so it is applied directly even
    // while tracking changes (a reviewer has nothing to accept or reject).
    AutoTransaction transaction(this, tr("Paragraph data"));
    if (!transaction.valid) return false;
    ParagraphFormat after = p->format;
    after.data = next;
    const bool ok = !apply(StepParagraphFormat{p->id, p->format, after});
    return transaction.finish(ok, Position{p->id, 0});
}

} // namespace QindaTK
