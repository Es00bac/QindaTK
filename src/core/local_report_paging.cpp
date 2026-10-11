// SPDX-License-Identifier: LGPL-3.0-or-later
#include "local_report_groups.h"
#include "rich_text_codec.h"
#include "rich_text_document.h"
#include "rich_text_layout.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QHash>
#include <algorithm>

namespace QindaTK::LocalReport {
namespace {
QUuid blockId(const QJsonValue &value)
{
    const QJsonObject block = value.toObject();
    return QUuid(block.value(block.contains(QStringLiteral("paragraph")) ? QStringLiteral("paragraph")
                                                                         : QStringLiteral("table"))
                     .toObject().value(QStringLiteral("id")).toString());
}
struct Extent { int first = -1, last = -1; qreal top = 0, bottom = 0; };
}
QJsonObject keepReportGroups(QJsonObject json, const QVector<QPair<int, int>> &ranges)
{
    // AGENT-GUARD: only request an earlier page when the entire span fits
    // on a fresh page. Otherwise long groups must flow, never loop forever.
    // One pass per requested group suffices; earlier breaks only shift later
    // content. Rendering uses the same embedded font/default/page settings.
    QJsonArray blocks = json.value(QStringLiteral("blocks")).toArray();
    QVector<QPair<int, int>> ordered = ranges;
    std::sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) {
        return a.first == b.first ? a.second > b.second : a.first < b.first;
    });
    QHash<QUuid, Extent> extents;
    bool dirty = true;
    for (const auto &[first, last] : ordered) {
        if (first < 0 || last >= blocks.size() || first >= last) continue;
        if (dirty) {
        extents.clear();
        RichTextDocument document;
        if (!RichTextCodec::decode(QJsonDocument(json).toJson(QJsonDocument::Compact), document)) return json;
        RichTextLayout layout;
        layout.setDocument(&document);
        layout.setMode(RichTextLayout::Mode::Paged);
        layout.paginateAll();
        for (int page = 0; page < layout.pageCount(); ++page) {
            for (const auto &item : layout.page(page).items) {
                if (item.headerRepeat) continue;
                Extent &extent = extents[item.block];
                if (extent.first < 0) { extent.first = page; extent.top = item.y; }
                extent.last = page;
                extent.bottom = item.y + item.height;
            }
        }
        dirty = false;
        }
        const Extent start = extents.value(blockId(blocks[first]));
        const Extent end = extents.value(blockId(blocks[last]));
        if (start.first < 0 || end.last <= start.first || start.top <= 0) continue;
        // Measure the exact span alone. This includes wrapped text, table
        // headers and margins between blocks, rather than estimating rows.
        QJsonObject isolated = json;
        QJsonArray span;
        for (int i = first; i <= last; ++i) span.append(blocks[i]);
        if (!span.last().toObject().contains(QStringLiteral("paragraph"))) {
            QJsonObject closing{{QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                                {QStringLiteral("text"), QString()}, {QStringLiteral("spans"), QJsonArray{}},
                                {QStringLiteral("mark"), 0}};
            span.append(QJsonObject{{QStringLiteral("paragraph"), closing}});
        }
        isolated.insert(QStringLiteral("blocks"), span);
        RichTextDocument probe;
        if (!RichTextCodec::decode(QJsonDocument(isolated).toJson(QJsonDocument::Compact), probe)) continue;
        RichTextLayout measure;
        measure.setDocument(&probe); measure.setMode(RichTextLayout::Mode::Paged); measure.paginateAll();
        if (measure.pageCount() != 1) continue;
        QJsonObject block = blocks[first].toObject();
        QJsonObject paragraph = block.value(QStringLiteral("paragraph")).toObject();
        if (paragraph.isEmpty()) continue;
        QJsonObject format = paragraph.value(QStringLiteral("format")).toObject();
        format.insert(QStringLiteral("pageBreakBefore"), true);
        paragraph.insert(QStringLiteral("format"), format);
        block.insert(QStringLiteral("paragraph"), paragraph);
        blocks[first] = block;
        json.insert(QStringLiteral("blocks"), blocks);
        dirty = true;
    }
    return json;
}
}
