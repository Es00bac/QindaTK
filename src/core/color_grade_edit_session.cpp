// SPDX-License-Identifier: LGPL-3.0-or-later
#include "color_grade_edit_session.h"

#include <QBuffer>

namespace QindaTK {

ColorGradeEditSession::ColorGradeEditSession(QObject *parent) : QObject(parent)
{
    const QVariantMap defaults = toMap({});
    m_document.defineProperty({QStringLiteral("grade"), QMetaType::fromType<QVariantMap>(),
                               QVariant::fromValue(QVariantMap{}), {}, true, false});
    m_document.addEntity(m_entity, {{QStringLiteral("grade"), defaults}});
    m_undo.addDocument(&m_document);
    m_transaction.setDocument(&m_document);
    m_transaction.setUndoService(&m_undo);
    m_transaction.setTargetIds({m_entity.toString()});
    m_transaction.setPropertyId(QStringLiteral("grade"));
    m_transaction.setLabel(QStringLiteral("Color grade"));
    m_transaction.setValidator([](const QVector<PropertyChange> &changes) {
        if (changes.isEmpty()) return OperationError{};
        OperationError error;
        if (!ColorGradeParameters::fromVariantMap(changes.last().after.toMap(), &error)) return error;
        return OperationError{};
    });
    m_context.document = ColorProfile::sRgb();
    m_context.working = ColorProfile::linearSRgb();
    m_context.display = ColorProfile::sRgb();
    m_context.output = ColorProfile::sRgb();
    connect(&m_document, &PropertyDocument::changed, this, &ColorGradeEditSession::refresh);
}

void ColorGradeEditSession::setClipId(const QString &id)
{
    if (id.isEmpty() || m_clipId == id) return;
    if (editing()) cancelGesture(m_gestureId);
    m_clipId = id;
    resetForTarget();
}

void ColorGradeEditSession::setFrameId(const QString &id)
{
    if (id.isEmpty() || m_frameId == id) return;
    if (editing()) cancelGesture(m_gestureId);
    m_frameId = id;
    resetForTarget();
}

void ColorGradeEditSession::setSourceImage(const QImage &image)
{
    if (m_sourceImage == image) return;
    if (editing()) cancelGesture(m_gestureId);
    m_sourceImage = image;
    m_beforeUrl = pngDataUrl(image);
    m_beforeScopes = m_scopeModel.analyzeFrame(image, m_frameId);
    refresh();
}

void ColorGradeEditSession::setFrame(const QString &clipId, const QString &frameId,
                                     const QImage &taggedImage,
                                     const ColorContextSnapshot &context)
{
    if (editing()) cancelGesture(m_gestureId);
    const bool targetChanged = (!clipId.isEmpty() && clipId != m_clipId)
        || (!frameId.isEmpty() && frameId != m_frameId);
    if (!clipId.isEmpty()) m_clipId = clipId;
    if (!frameId.isEmpty()) m_frameId = frameId;
    m_sourceImage = taggedImage;
    m_beforeUrl = pngDataUrl(taggedImage);
    m_beforeScopes = m_scopeModel.analyzeFrame(taggedImage, m_frameId);
    m_context = context;
    if (targetChanged) resetForTarget();
    else refresh();
}

void ColorGradeEditSession::setColorContext(const ColorContextSnapshot &context)
{
    if (context.generation == m_context.generation && context.output.id() == m_context.output.id()
        && context.working.id() == m_context.working.id()) return;
    if (editing()) cancelGesture(m_gestureId);
    m_context = context;
    refresh();
}

QVariantMap ColorGradeEditSession::parameters() const
{
    return m_document.value(m_entity, QStringLiteral("grade")).toMap();
}

bool ColorGradeEditSession::canUndo() const { return m_undo.canUndo(&m_document); }
bool ColorGradeEditSession::canRedo() const { return m_undo.canRedo(&m_document); }

QVariantMap ColorGradeEditSession::toMap(const ColorGradeParameters &p)
{
    return {{QStringLiteral("exposureEV"), p.exposureEv},
            {QStringLiteral("lift"), QVariantMap{{QStringLiteral("x"), p.lift.x()}, {QStringLiteral("y"), p.lift.y()}}},
            {QStringLiteral("gamma"), QVariantMap{{QStringLiteral("x"), p.gamma.x()}, {QStringLiteral("y"), p.gamma.y()}}},
            {QStringLiteral("gain"), QVariantMap{{QStringLiteral("x"), p.gain.x()}, {QStringLiteral("y"), p.gain.y()}}}};
}

QString ColorGradeEditSession::pngDataUrl(const QImage &image)
{
    if (image.isNull()) return {};
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
}

bool ColorGradeEditSession::render(const QVariantMap &candidate)
{
    OperationError error;
    const auto parsed = ColorGradeParameters::fromVariantMap(candidate, &error);
    if (!parsed) { m_error = error; m_processedImage = {}; m_afterUrl.clear(); emit changed(); return false; }
    const auto result = m_processor.process(m_sourceImage, *parsed, m_context);
    if (!result) { m_error = result.error; m_processedImage = {}; m_afterUrl.clear(); emit changed(); return false; }
    m_processedImage = result.image;
    m_afterUrl = pngDataUrl(result.image);
    m_afterScopes = m_scopeModel.analyzeFrame(result.image, m_frameId + QStringLiteral("-grade"));
    m_error = {};
    emit changed();
    return !m_afterUrl.isEmpty();
}

void ColorGradeEditSession::refresh()
{
    m_beforeUrl = pngDataUrl(m_sourceImage);
    render(parameters());
}

void ColorGradeEditSession::resetForTarget()
{
    m_gestureId.clear();
    m_document.resetModel({{m_entity, {{QStringLiteral("grade"), toMap({})}}}});
}

bool ColorGradeEditSession::reject(const QString &id, const QString &message)
{
    m_error = {ErrorCode::Validation, message, QStringLiteral("gestureId"), QStringLiteral("qindatk.color-grade-session")};
    emit gestureRejected(id, message);
    emit changed();
    return false;
}

bool ColorGradeEditSession::beginGesture(const QString &gestureId)
{
    if (gestureId.isEmpty() || editing()) return reject(gestureId, QStringLiteral("A non-empty gesture ID is required and only one gesture may be open"));
    if (m_sourceImage.isNull()) return reject(gestureId, QStringLiteral("A tagged source frame is required"));
    m_gestureId = gestureId;
    m_error = {};
    if (!m_transaction.begin()) { m_gestureId.clear(); return reject(gestureId, m_transaction.errorMessage()); }
    emit changed();
    return true;
}

bool ColorGradeEditSession::preview(const QString &gestureId, const QVariantMap &candidate)
{
    if (!editing() || gestureId != m_gestureId) return reject(gestureId, QStringLiteral("Preview does not match the active gesture"));
    OperationError parseError;
    if (!ColorGradeParameters::fromVariantMap(candidate, &parseError)) {
        m_transaction.cancel();
        m_gestureId.clear();
        refresh();
        return reject(gestureId, parseError.message);
    }
    if (!m_transaction.update(candidate)) {
        const QString message = m_transaction.errorMessage();
        if (editing()) m_transaction.cancel();
        m_gestureId.clear();
        refresh();
        return reject(gestureId, message);
    }
    if (!render(candidate)) {
        const QString message = m_error.message;
        m_transaction.cancel();
        m_gestureId.clear();
        refresh();
        if (!message.isEmpty()) m_error.message = message;
        emit gestureRejected(gestureId, m_error.message);
        emit changed();
        return false;
    }
    return true;
}

bool ColorGradeEditSession::finishGesture(const QString &gestureId)
{
    if (!editing() || gestureId != m_gestureId) return reject(gestureId, QStringLiteral("Finish does not match the active gesture"));
    const bool ok = m_transaction.commit();
    m_gestureId.clear();
    if (!ok) return reject(gestureId, m_transaction.errorMessage());
    refresh();
    return true;
}

bool ColorGradeEditSession::cancelGesture(const QString &gestureId)
{
    if (!editing() || gestureId != m_gestureId) return reject(gestureId, QStringLiteral("Cancel does not match the active gesture"));
    m_transaction.cancel();
    m_gestureId.clear();
    refresh();
    return true;
}

bool ColorGradeEditSession::undo()
{
    if (editing()) cancelGesture(m_gestureId);
    const auto error = m_undo.undo(&m_document);
    if (error) { m_error = error; emit changed(); return false; }
    refresh();
    return true;
}

bool ColorGradeEditSession::redo()
{
    if (editing()) cancelGesture(m_gestureId);
    const auto error = m_undo.redo(&m_document);
    if (error) { m_error = error; emit changed(); return false; }
    refresh();
    return true;
}

QVariantMap ColorGradeEditSession::committedParameters() const
{
    return m_document.committedValue(m_entity, QStringLiteral("grade")).toMap();
}

QVariantList ColorGradeEditSession::wheels() const
{
    const QVariantMap current = parameters();
    const QList<std::pair<QString, QString>> names{{QStringLiteral("lift"), tr("Lift")},
                                                   {QStringLiteral("gamma"), tr("Gamma")},
                                                   {QStringLiteral("gain"), tr("Gain")}};
    QVariantList result;
    for (const auto &[id, label] : names) {
        const QVariantMap point = current.value(id).toMap();
        result.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("label"), label},
            {QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble()},
            {QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble()},
            {QStringLiteral("minimumX"), -1.0}, {QStringLiteral("maximumX"), 1.0},
            {QStringLiteral("minimumY"), -1.0}, {QStringLiteral("maximumY"), 1.0},
            {QStringLiteral("stepX"), 0.01}, {QStringLiteral("stepY"), 0.01},
            {QStringLiteral("enabled"), !m_sourceImage.isNull()}});
    }
    return result;
}

double ColorGradeEditSession::exposure() const
{
    return parameters().value(QStringLiteral("exposureEV")).toDouble();
}

QString ColorGradeEditSession::gradeSpace() const
{
    return tr("Linear sRGB working space; output tagged in the context's output profile. "
              "Wheels are opponent offsets (x: red against green, y: blue against yellow) "
              "that sum to zero across R, G and B, so they change chroma and never add luma; "
              "exposure is the luma control.");
}

std::optional<QVariantMap> ColorGradeEditSession::withWheel(const QString &wheelId, double x, double y) const
{
    QVariantMap next = parameters();
    if (wheelId == QLatin1String("exposure")) {
        next.insert(QStringLiteral("exposureEV"), x);
        return next;
    }
    if (wheelId != QLatin1String("lift") && wheelId != QLatin1String("gamma") && wheelId != QLatin1String("gain"))
        return std::nullopt;
    next.insert(wheelId, QVariantMap{{QStringLiteral("x"), x}, {QStringLiteral("y"), y}});
    return next;
}

bool ColorGradeEditSession::previewWheel(const QString &gestureId, const QString &wheelId, double x, double y)
{
    const auto candidate = withWheel(wheelId, x, y);
    if (!candidate) return reject(gestureId, tr("Unknown grade wheel %1").arg(wheelId));
    if (!editing() && !beginGesture(gestureId)) return false;
    return preview(gestureId, *candidate);
}

bool ColorGradeEditSession::finishWheel(const QString &gestureId, const QString &wheelId, double x, double y)
{
    if (!previewWheel(gestureId, wheelId, x, y)) return false;
    return finishGesture(gestureId);
}

bool ColorGradeEditSession::applyNumeric(const QString &label, const QVariantMap &candidate)
{
    if (editing()) cancelGesture(m_gestureId);
    const QString gestureId = QStringLiteral("numeric-%1-%2").arg(label).arg(++m_serial);
    if (!beginGesture(gestureId) || !preview(gestureId, candidate)) return false;
    return finishGesture(gestureId);
}

bool ColorGradeEditSession::setWheelValue(const QString &wheelId, double x, double y)
{
    const auto candidate = withWheel(wheelId, x, y);
    if (!candidate || wheelId == QLatin1String("exposure"))
        return reject(QString(), tr("Unknown grade wheel %1").arg(wheelId));
    return applyNumeric(wheelId, *candidate);
}

bool ColorGradeEditSession::setExposure(double exposureEv)
{
    return applyNumeric(QStringLiteral("exposure"), *withWheel(QStringLiteral("exposure"), exposureEv, 0.0));
}

bool ColorGradeEditSession::resetWheel(const QString &wheelId)
{
    if (wheelId == QLatin1String("exposure")) return setExposure(0.0);
    return setWheelValue(wheelId, 0.0, 0.0);
}

} // namespace QindaTK
