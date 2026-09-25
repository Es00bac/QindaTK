// SPDX-License-Identifier: LGPL-3.0-or-later
// Plain-text merge for LocalPropertySyncClient: a replicated growable array
// (RGA) of UTF-16 code units with tombstones. See docs/collaboration.md
// "Text merge" and D-167.
#include "local_property_sync.h"
#include "property_sync_detail_p.h"

namespace QindaTK {
using namespace sync_detail;

namespace {
constexpr int MaxSequences = 1024;
constexpr int MaxWaiting = 1024;

// Total order of elements. Every element inserted after another carries a
// larger Lamport time than its anchor, so it also sorts after it.
template <typename Element>
bool later(const Element &a, const Element &b)
{
    if (a.time != b.time) return a.time > b.time;
    if (a.actorId != b.actorId) return a.actorId > b.actorId;
    if (a.op != b.op) return a.op > b.op;
    return a.index > b.index;
}

template <typename Sequence>
int indexOf(const Sequence &sequence, const QString &id)
{
    for (int i = 0; i < sequence.elements.size(); ++i)
        if (sequence.elements.at(i).id == id) return i;
    return -1;
}
}

LocalPropertySyncClient::TextSequence *LocalPropertySyncClient::sequence(
    const QString &entityId, const QString &propertyId)
{
    const QString key = keyOf(entityId, propertyId);
    auto it = m_text.find(key);
    if (it != m_text.end()) return &it.value();
    if (m_text.size() >= MaxSequences) {
        fail(QStringLiteral("Too many synchronized text properties"));
        return nullptr;
    }
    // AGENT-GUARD: every replica seeds the sequence from the same baseline
    // value (or descriptor default), so "base:<n>" IDs name the same units.
    const QString base = baseValue(entityId, propertyId).toString();
    if (base.size() > MaxTextElements) {
        fail(QStringLiteral("Baseline text is too long to synchronize"));
        return nullptr;
    }
    TextSequence created;
    created.elements.reserve(base.size());
    for (int i = 0; i < base.size(); ++i)
        created.elements.append({elementId(baseElementOp(), i), base.at(i), 0, {},
                                 baseElementOp(), i, false});
    return &m_text.insert(key, created).value();
}

QString LocalPropertySyncClient::visibleText(const TextSequence &sequence) const
{
    QString text;
    for (const auto &element : sequence.elements)
        if (!element.deleted) text.append(element.character);
    return text;
}

bool LocalPropertySyncClient::integrateInsert(TextSequence &sequence,
                                              const PropertySyncOperation &operation)
{
    // Redelivery after compaction (the log no longer names every operation
    // a replica has seen) must not insert the same elements twice.
    if (indexOf(sequence, elementId(operation.operationId, 0)) >= 0) return true;
    for (const auto &waiting : std::as_const(sequence.waiting))
        if (waiting.operationId == operation.operationId) return true;
    int position = -1;
    if (!operation.meta.isEmpty()) {
        position = indexOf(sequence, operation.meta);
        if (position < 0) {
            // Anchor not delivered yet (reordered or dropped): wait for it.
            if (sequence.waiting.size() >= MaxWaiting)
                return fail(QStringLiteral("Too many text insertions await their anchor"));
            sequence.waiting.append(operation);
            return true;
        }
    }
    const QString text = operation.value.toString();
    if (sequence.elements.size() + text.size() > MaxTextElements)
        return fail(QStringLiteral("Synchronized text is too long"));
    for (int k = 0; k < text.size(); ++k) {
        TextElement element{elementId(operation.operationId, k), text.at(k),
                            operation.logicalTime, operation.actorId,
                            operation.operationId, k, false};
        element.deleted = sequence.pendingDeletes.remove(element.id);
        int j = position + 1;
        while (j < sequence.elements.size() && later(sequence.elements.at(j), element)) ++j;
        sequence.elements.insert(j, element);
        position = j;
    }
    // Inserts that were waiting for one of these elements can now land.
    bool progressed = true;
    while (progressed) {
        progressed = false;
        for (int i = 0; i < sequence.waiting.size(); ++i) {
            if (indexOf(sequence, sequence.waiting.at(i).meta) < 0) continue;
            const PropertySyncOperation ready = sequence.waiting.takeAt(i);
            if (!integrateInsert(sequence, ready)) return false;
            progressed = true;
            break;
        }
    }
    return true;
}

bool LocalPropertySyncClient::receiveText(const PropertySyncOperation &operation)
{
    if (!isTextProperty(operation.propertyId))
        return fail(QStringLiteral("A value property received a text operation"));
    const auto descriptor = m_document->descriptor(operation.propertyId);
    if (!descriptor || descriptor->type != QMetaType::fromType<QString>())
        return fail(QStringLiteral("Text operations need a string property"));
    TextSequence *text = sequence(operation.entityId, operation.propertyId);
    if (!text) return false;
    if (operation.kind == Kind::TextInsert) {
        if (!integrateInsert(*text, operation)) return false;
    } else {
        const auto ranges = decodeTextRanges(operation.value.toByteArray());
        if (!ranges) return fail(QStringLiteral("Invalid text delete"));
        QHash<QString, int> positions;
        for (int i = 0; i < text->elements.size(); ++i)
            positions.insert(text->elements.at(i).id, i);
        for (const auto &range : *ranges) {
            for (int i = range.start; i < range.start + range.count; ++i) {
                const QString id = elementId(range.op, i);
                const auto found = positions.constFind(id);
                if (found != positions.cend()) {
                    text->elements[found.value()].deleted = true;
                } else if (text->pendingDeletes.size() < MaxTextElements) {
                    text->pendingDeletes.insert(id);
                } else {
                    return fail(QStringLiteral("Too many pending text deletes"));
                }
            }
        }
    }
    // The newest text operation supports the entity like a property winner.
    const QString key = keyOf(operation.entityId, operation.propertyId);
    const auto winner = m_winners.constFind(key);
    if (winner == m_winners.cend() || outranks(operation, winner.value()))
        m_winners.insert(key, operation);
    if (!reconcileEntity(operation.entityId)) return false;
    return publishText(operation.entityId, operation.propertyId);
}

bool LocalPropertySyncClient::publishText(const QString &entityId, const QString &propertyId)
{
    const EntityId entity = EntityId::parse(entityId);
    if (!m_document->contains(entity)) return true;
    const TextSequence *text = sequence(entityId, propertyId);
    if (!text) return false;
    if (auto error = m_document->applySynchronizedValue(entity, propertyId, visibleText(*text)))
        return fail(error.message);
    m_expectedRevision = m_document->revision().value;
    return true;
}

bool LocalPropertySyncClient::replaceText(const QString &entityId, const QString &propertyId,
                                          int start, int removeCount, const QString &text)
{
    if (!readyToEdit()) return false;
    if (!canonicalUuid(entityId) || !m_document->contains(EntityId::parse(entityId))
        || !isTextProperty(propertyId))
        return fail(QStringLiteral("Entity or text property is invalid"));
    TextSequence *current = sequence(entityId, propertyId);
    if (!current) return false;
    QVector<int> visible;
    for (int i = 0; i < current->elements.size(); ++i)
        if (!current->elements.at(i).deleted) visible.append(i);
    if (start < 0 || removeCount < 0 || start > visible.size()
        || removeCount > visible.size() - start || text.size() > MaxTextElements)
        return fail(QStringLiteral("Text range is invalid"));
    if (removeCount == 0 && text.isEmpty()) return true;
    QStringList ids;
    for (int k = start; k < start + removeCount; ++k)
        ids.append(current->elements.at(visible.at(k)).id);
    const QString anchor = start > 0 ? current->elements.at(visible.at(start - 1)).id
                                     : QString();
    return textEdit(entityId, propertyId, ids, text, anchor, true);
}

bool LocalPropertySyncClient::textEdit(const QString &entityId, const QString &propertyId,
                                       const QStringList &deleteIds, const QString &insertText,
                                       const QString &anchor, bool recordHistory)
{
    TextSequence *current = sequence(entityId, propertyId);
    if (!current) return false;
    QVector<TextRange> ranges;
    QString removedText;
    QHash<QString, int> positions;
    for (int i = 0; i < current->elements.size(); ++i)
        positions.insert(current->elements.at(i).id, i);
    for (const QString &id : deleteIds) {
        const auto parsed = parseElementId(id);
        const int position = positions.value(id, -1);
        if (!parsed || position < 0) return fail(QStringLiteral("Unknown text element"));
        removedText.append(current->elements.at(position).character);
        if (!ranges.isEmpty() && ranges.last().op == parsed->first
            && ranges.last().start + ranges.last().count == parsed->second)
            ++ranges.last().count;
        else
            ranges.append({parsed->first, parsed->second, 1});
    }
    if (ranges.size() > MaxTextRanges) return fail(QStringLiteral("Text delete is too fragmented"));
    // AGENT-GUARD: everything that could refuse a later part is checked
    // before the first part applies (tombstones stay, so a delete never makes
    // room for the insert). A replica must not show a delete it never sends.
    const int inserts = (int(insertText.size()) + MaxTextInsert - 1) / MaxTextInsert;
    const int parts = (ranges.isEmpty() ? 0 : 1) + inserts;
    if (parts == 0) return true;
    if (current->elements.size() + insertText.size() > MaxTextElements)
        return fail(QStringLiteral("Synchronized text is too long"));
    if (!readyToEdit(parts)) return false;
    if (m_seen.size() + parts > MaxOperations * 2)
        return fail(QStringLiteral("Client operation limit reached"));
    QVector<PropertySyncOperation> operations;
    if (!ranges.isEmpty())
        operations.append(makeOperation(Kind::TextDelete, entityId, propertyId,
                                        encodeTextRanges(ranges)));
    // Long text (a restored version) goes out as chained inserts, each at
    // most MaxTextInsert units and anchored on the previous chunk's end.
    QStringList inserted;
    QString chunkAnchor = anchor;
    for (int offset = 0; offset < insertText.size(); offset += MaxTextInsert) {
        const QString chunk = insertText.mid(offset, MaxTextInsert);
        operations.append(makeOperation(Kind::TextInsert, entityId, propertyId, chunk,
                                        chunkAnchor));
        for (int k = 0; k < chunk.size(); ++k)
            inserted.append(elementId(operations.last().operationId, k));
        chunkAnchor = inserted.last();
    }
    QVector<PropertySyncOperation> applied;
    for (const auto &operation : std::as_const(operations)) {
        if (!receive(operation)) break;
        applied.append(operation);
    }
    if (applied.isEmpty()) return false;
    const QString error = m_lastError;
    // Whatever applied is queued, so every replica converges on it.
    if (!queue(applied)) return false;
    if (applied.size() != operations.size()) return fail(error);
    if (recordHistory)
        record(HistoryEntry{Kind::TextInsert, operations.last().operationId,
                            operations.last().logicalTime, entityId, propertyId, {}, inserted,
                            deleteIds, removedText});
    return true;
}

QString LocalPropertySyncClient::textAnchor(const QString &entityId, const QString &propertyId,
                                            int position)
{
    if (!m_document || !canonicalUuid(entityId) || !isTextProperty(propertyId)) return {};
    const TextSequence *text = sequence(entityId, propertyId);
    if (!text || position <= 0) return {};
    QString last;
    int visible = 0;
    for (const auto &element : text->elements) {
        if (element.deleted) continue;
        last = element.id;
        if (++visible == position) return last;
    }
    return last;   // past the end: after the last character
}

int LocalPropertySyncClient::textPosition(const QString &entityId, const QString &propertyId,
                                          const QString &anchor)
{
    if (!m_document || !canonicalUuid(entityId) || !isTextProperty(propertyId)) return -1;
    if (anchor.isEmpty()) return 0;
    const TextSequence *text = sequence(entityId, propertyId);
    if (!text) return -1;
    int visible = 0;
    for (const auto &element : text->elements) {
        if (!element.deleted) ++visible;
        // A deleted anchor collapses to the gap its element left.
        if (element.id == anchor) return visible;
    }
    return -1;
}

} // namespace QindaTK
