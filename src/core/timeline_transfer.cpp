// SPDX-License-Identifier: LGPL-3.0-or-later
#include "timeline_transfer.h"

#include "timeline_codec.h"
#include "timeline_planner.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonObject>
#include <QMimeData>

namespace QindaTK {
namespace {
QString tr(const char *text)
{
    return QCoreApplication::translate("TimelineModel", text);
}

OperationError failure(ErrorCode code, const QString &message)
{
    return {code, message, {}, {}};
}

class SystemAdapter final : public ClipboardAdapter {
public:
    explicit SystemAdapter(QObject *parent = nullptr)
        : ClipboardAdapter(parent), m_clipboard(QGuiApplication::clipboard())
    {
        if (m_clipboard) connect(m_clipboard, &QClipboard::dataChanged, this, &ClipboardAdapter::changed);
    }
    const QMimeData *mimeData() const override { return m_clipboard ? m_clipboard->mimeData() : nullptr; }
    bool setMimeData(QMimeData *data) override
    {
        if (!m_clipboard) {
            delete data;
            return false;
        }
        m_clipboard->setMimeData(data);
        return true;
    }

private:
    QPointer<QClipboard> m_clipboard;
};
} // namespace

namespace TimelineTransfer {

TransferPayload payloadFor(const TimelineModel &model, const QVector<QUuid> &clipIds,
                           OperationError *error)
{
    TransferPayload payload;
    payload.sourceDocumentId = DocumentId::parse(model.documentId());
    payload.sourceRevision = RevisionId{model.revisionValue()};
    const QVector<QUuid> order = model.visibleTrackOrder();
    QVector<const TimelineClip *> clips;
    qint64 earliest = std::numeric_limits<qint64>::max();
    qsizetype topTrack = order.size();
    for (const auto &id : clipIds) {
        const auto *clip = model.clipRecord(id);
        const qsizetype track = clip ? order.indexOf(clip->trackId) : -1;
        if (!clip || track < 0) {
            if (error) *error = failure(ErrorCode::InvalidArgument, tr("Only visible clips can be copied"));
            return {};
        }
        if (std::find(clips.cbegin(), clips.cend(), clip) != clips.cend()) continue;
        clips.push_back(clip);
        earliest = std::min(earliest, clip->start);
        topTrack = std::min(topTrack, track);
    }
    if (clips.isEmpty() || clips.size() > TransferPayloadCodec::MaximumItems) {
        if (error) *error = failure(ErrorCode::InvalidArgument, tr("Copy between 1 and 4096 clips"));
        return {};
    }
    for (const auto *clip : clips) {
        const auto offset = TimelineTime::subtract(clip->start, earliest);
        if (!offset) {
            if (error) *error = failure(ErrorCode::InvalidArgument, tr("The copied clips span too much time"));
            return {};
        }
        QVariantMap record = TimelineCodec::toMap(*clip);
        record.remove(QStringLiteral("end"));
        record.remove(QStringLiteral("trackId"));
        QJsonObject item{{QStringLiteral("schema"), QString::fromLatin1(ClipSchema)},
                         {QStringLiteral("clip"), QJsonObject::fromVariantMap(record)},
                         {QStringLiteral("startOffset"), QString::number(*offset)},
                         {QStringLiteral("trackOffset"), int(order.indexOf(clip->trackId) - topTrack)},
                         {QStringLiteral("timebaseNumerator"), model.timebaseNumeratorText()},
                         {QStringLiteral("timebaseDenominator"), model.timebaseDenominatorText()}};
        payload.items.push_back({EntityId{clip->id}, TransferItemKind::Object, item});
    }
    if (error) *error = {};
    return payload;
}

std::optional<TimelineEditRequest> insertRequest(const TimelineModel &model,
                                                 const TransferPayload &payload,
                                                 const QUuid &trackId, bool allowForeignResources,
                                                 OperationError *error)
{
    const auto reject = [&](ErrorCode code, const QString &message) {
        if (error) *error = failure(code, message);
        return std::optional<TimelineEditRequest>();
    };
    if (payload.items.isEmpty()) return reject(ErrorCode::InvalidArgument, tr("There are no clips to insert"));
    const bool foreign = payload.sourceDocumentId.toString() != model.documentId();
    TimelineEditRequest request;
    request.kind = TimelineEditKind::InsertClips;
    request.trackId = trackId;
    request.label = tr("Paste clips");
    for (const auto &item : payload.items) {
        const QJsonObject &object = item.payload;
        if (item.kind != TransferItemKind::Object
            || object.value(QStringLiteral("schema")).toString() != QLatin1String(ClipSchema))
            return reject(ErrorCode::Unsupported, tr("The transfer does not hold timeline clips"));
        if (object.value(QStringLiteral("timebaseNumerator")).toString() != model.timebaseNumeratorText()
            || object.value(QStringLiteral("timebaseDenominator")).toString() != model.timebaseDenominatorText())
            return reject(ErrorCode::Unsupported,
                          tr("Clips from a timeline with another timebase cannot be inserted exactly"));
        QVariantMap record = object.value(QStringLiteral("clip")).toObject().toVariantMap();
        record.insert(QStringLiteral("id"), item.entityId.toString());
        record.insert(QStringLiteral("trackId"), trackId.toString(QUuid::WithoutBraces));
        record.insert(QStringLiteral("start"), object.value(QStringLiteral("startOffset")).toString());
        QString problem;
        const auto clip = TimelineCodec::clipFromMap(record, &problem);
        if (!clip) return reject(ErrorCode::InvalidArgument, problem);
        if (foreign && !allowForeignResources && !clip->resourceId.isEmpty())
            return reject(ErrorCode::Unsupported,
                          tr("Clips from another timeline need the host to resolve their media first"));
        request.templates.push_back({*clip, object.value(QStringLiteral("trackOffset")).toInt()});
    }
    if (error) *error = {};
    return request;
}

} // namespace TimelineTransfer

TimelineClipboard::TimelineClipboard(QObject *parent) : QObject(parent)
{
    m_ownedAdapter = std::make_unique<SystemAdapter>();
    setAdapter(m_ownedAdapter.get());
}

TimelineClipboard::~TimelineClipboard() = default;

void TimelineClipboard::setAdapter(ClipboardAdapter *adapter)
{
    if (m_adapter) disconnect(m_adapter, nullptr, this, nullptr);
    m_adapter = adapter;
    if (adapter) connect(adapter, &ClipboardAdapter::changed, this, &TimelineClipboard::refresh);
    refresh();
}

void TimelineClipboard::setAllowForeignResources(bool allow)
{
    if (m_allowForeign == allow) return;
    m_allowForeign = allow;
    emit changed();
}

bool TimelineClipboard::fail(const QString &message)
{
    m_lastError = message;
    emit changed();
    emit rejected(message);
    return false;
}

std::optional<TransferPayload> TimelineClipboard::current(OperationError *error) const
{
    const QMimeData *data = m_adapter ? m_adapter->mimeData() : nullptr;
    const QString mime = QString::fromLatin1(TransferPayloadCodec::InternalMime);
    if (!data || !data->hasFormat(mime)) {
        if (error) *error = failure(ErrorCode::Unavailable, tr("The clipboard holds no timeline clips"));
        return std::nullopt;
    }
    return TransferPayloadCodec::decode(data->data(mime), error);
}

void TimelineClipboard::refresh()
{
    const auto payload = current(nullptr);
    int count = 0;
    if (payload) {
        for (const auto &item : payload->items)
            if (item.payload.value(QStringLiteral("schema")).toString() == QLatin1String(TimelineTransfer::ClipSchema))
                ++count;
    }
    const QString source = payload && count > 0 ? payload->sourceDocumentId.toString() : QString();
    if (count == m_clipCount && source == m_sourceDocument) return;
    m_clipCount = count;
    m_sourceDocument = source;
    emit changed();
}

bool TimelineClipboard::copyClips(TimelineModel *model, const QStringList &clipIds)
{
    if (!model || !m_adapter) return fail(tr("No timeline or clipboard is available"));
    QVector<QUuid> ids;
    for (const auto &id : clipIds) ids.push_back(QUuid(id));
    OperationError error;
    const TransferPayload payload = TimelineTransfer::payloadFor(*model, ids, &error);
    if (error) return fail(error.message);
    const QByteArray bytes = TransferPayloadCodec::encode(payload, &error);
    if (error) return fail(error.message);
    auto *data = new QMimeData;
    data->setData(QString::fromLatin1(TransferPayloadCodec::InternalMime), bytes);
    if (!m_adapter->setMimeData(data)) return fail(tr("The clipboard rejected the clips"));
    m_lastError.clear();
    refresh();
    return true;
}

QVariantMap TimelineClipboard::paste(TimelineModel *model, const QString &trackId,
                                     const QString &atTicks)
{
    const auto rejectMap = [this](const QString &message) {
        fail(message);
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
    };
    const auto at = TimelineTime::parseDecimal(atTicks);
    if (!model || !at) return rejectMap(tr("Paste needs a timeline and an exact position"));
    OperationError error;
    const auto payload = current(&error);
    if (!payload) return rejectMap(error.message);
    const auto request = TimelineTransfer::insertRequest(*model, *payload, QUuid(trackId),
                                                         m_allowForeign, &error);
    if (!request) return rejectMap(error.message);
    TimelineEditInput input;
    input.pointerTicks = *at;
    input.snap = false;
    QString transaction;
    if (auto failed = model->apply(*request, input, &transaction)) return rejectMap(failed.message);
    QStringList created;
    for (const auto &item : request->templates)
        created.push_back(QUuid::createUuidV5(QUuid(transaction), item.clip.id.toRfc4122())
                              .toString(QUuid::WithoutBraces));
    m_lastError.clear();
    return {{QStringLiteral("ok"), true}, {QStringLiteral("transactionId"), transaction},
            {QStringLiteral("clipIds"), created}};
}

TimelineDropTarget::TimelineDropTarget(TimelineModel *model, QString targetId, QString viewId,
                                       QObject *parent)
    : DropTarget(parent), m_model(model), m_targetId(std::move(targetId)), m_viewId(std::move(viewId))
{
}

SnapshotHeader TimelineDropTarget::snapshot() const
{
    return m_model ? m_model->snapshotHeader() : SnapshotHeader{};
}

DropPreview TimelineDropTarget::previewDrop(const QString &sessionId, const TransferPayload &payload,
                                            TransferOperation operation, const QVariantMap &location)
{
    Q_UNUSED(sessionId);
    DropPreview preview;
    const auto at = TimelineCodec::ticks(location.value(QStringLiteral("ticks")));
    if (!m_model || !at) {
        preview.error = tr("The drop needs a timeline and an exact position");
        preview.errorCode = ErrorCode::InvalidArgument;
        return preview;
    }
    if (operation != TransferOperation::Copy) {
        preview.error = tr("Timeline drops copy clips; move clips with the timeline's own drag");
        preview.errorCode = ErrorCode::Unsupported;
        return preview;
    }
    OperationError error;
    const auto request = TimelineTransfer::insertRequest(
        *m_model, payload, TimelineCodec::uuid(location.value(QStringLiteral("trackId"))), m_allowForeign, &error);
    Plan plan{m_model->snapshotHeader(), request.value_or(TimelineEditRequest{}), {}};
    plan.input.pointerTicks = *at;
    plan.input.snap = false;
    const TimelinePlan planned = request ? TimelinePlanner::plan(*m_model, *request, plan.input,
                                                                 QUuid::createUuid().toString(QUuid::WithoutBraces))
                                         : TimelinePlan{};
    if (!request || planned.error) {
        preview.error = request ? planned.error.message : error.message;
        preview.errorCode = request ? planned.error.code : error.code;
        return preview;
    }
    preview.accepted = true;
    preview.summary = tr("Insert %n clips", "", int(request->templates.size()));
    preview.token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_plans.insert(preview.token, plan);
    return preview;
}

OperationError TimelineDropTarget::commitDrop(const QString &sessionId, const QString &previewToken)
{
    Q_UNUSED(sessionId);
    const auto it = m_plans.constFind(previewToken);
    if (it == m_plans.cend()) return failure(ErrorCode::InvalidArgument, tr("Unknown drop preview"));
    const Plan plan = *it;
    m_plans.remove(previewToken);
    if (!m_model) return failure(ErrorCode::Unavailable, tr("The timeline is gone"));
    const SnapshotHeader current = m_model->snapshotHeader();
    if (current.documentId != plan.snapshot.documentId || current.revision != plan.snapshot.revision
        || current.resetGeneration != plan.snapshot.resetGeneration)
        return failure(ErrorCode::StaleRevision, tr("The timeline changed after the drop preview"));
    return m_model->apply(plan.request, plan.input);
}

void TimelineDropTarget::cancelDrop(const QString &sessionId, const QString &previewToken)
{
    Q_UNUSED(sessionId);
    m_plans.remove(previewToken);
}

} // namespace QindaTK
