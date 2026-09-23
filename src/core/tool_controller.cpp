// SPDX-License-Identifier: LGPL-3.0-or-later
#include "tool_controller.h"

#include <algorithm>

namespace QindaTK {

ToolController::ToolController(QObject *parent) : QObject(parent)
{
    for (auto &tool : createStandardCanvasTools()) {
        registerTool(std::move(tool));
    }
}

ToolController::~ToolController()
{
    // Destruction mid-gesture cancels: the tool drops its transient state
    // and the document receives nothing.
    if (m_tool) {
        CanvasToolContext ctx = context();
        m_tool->cancel(ctx);
        m_tool = nullptr;
    }
}

bool ToolController::registerTool(std::unique_ptr<CanvasTool> tool)
{
    if (!tool || tool->id().isEmpty() || hasTool(tool->id())) {
        return false;
    }
    m_tools.push_back(std::move(tool));
    emit configurationChanged();
    return true;
}

bool ToolController::hasTool(const QString &id) const
{
    return findTool(id) != nullptr;
}

CanvasTool *ToolController::findTool(const QString &id) const
{
    for (const auto &tool : m_tools) {
        if (tool->id() == id) {
            return tool.get();
        }
    }
    return nullptr;
}

QVariantList ToolController::tools() const
{
    QVariantList out;
    for (const auto &tool : m_tools) {
        out << QVariantMap{{QStringLiteral("id"), tool->id()},
                           {QStringLiteral("label"), tool->label()},
                           {QStringLiteral("icon"), tool->icon()},
                           {QStringLiteral("iconName"), tool->icon()}, // Tk.ToolShelf model key
                           {QStringLiteral("tooltip"), tool->label()},
                           {QStringLiteral("editsDocument"), tool->editsDocument()}};
    }
    return out;
}

void ToolController::setDocument(CanvasDocumentAdapter *document)
{
    if (m_document == document) {
        return;
    }
    cancel(QStringLiteral("document-switch"));
    if (m_document) {
        disconnect(m_document, nullptr, this, nullptr);
    }
    m_document = document;
    m_selection.clear();
    if (document) {
        connect(document, &CanvasDocumentAdapter::edited, this, &ToolController::onDocumentEdited);
        connect(document, &CanvasDocumentAdapter::reset, this, [this] {
            if (m_tool && m_tool->editsDocument()) {
                cancel(QStringLiteral("document-reset"));
            }
            pruneSelection();
        });
        // AGENT-GUARD: clear the pointer BEFORE canceling so no tool can reach
        // a half-destroyed provider through the context.
        connect(document, &QObject::destroyed, this, [this] {
            m_document = nullptr;
            cancel(QStringLiteral("provider-destroyed"));
            m_selection.clear();
            emit selectionChanged();
            emit documentChanged();
        });
    }
    emit selectionChanged();
    emit documentChanged();
}

void ToolController::setActiveTool(const QString &id)
{
    if (id == m_activeTool || !hasTool(id)) {
        return;
    }
    cancel(QStringLiteral("tool-switch"));
    m_activeTool = id;
    emit toolChanged();
}

QString ToolController::effectiveTool() const
{
    return m_temporary.isEmpty() ? m_activeTool : m_temporary.constLast();
}

bool ToolController::pushTemporaryTool(const QString &id)
{
    if (!hasTool(id)) {
        return false;
    }
    cancel(QStringLiteral("tool-switch"));
    m_temporary.append(id);
    emit toolChanged();
    return true;
}

bool ToolController::popTemporaryTool()
{
    if (m_temporary.isEmpty()) {
        return false;
    }
    cancel(QStringLiteral("tool-switch"));
    m_temporary.removeLast();
    emit toolChanged();
    return true;
}

Qt::CursorShape ToolController::cursor() const
{
    const CanvasTool *tool = m_tool ? m_tool : findTool(effectiveTool());
    return tool ? tool->cursor() : Qt::ArrowCursor;
}

CanvasToolContext ToolController::context()
{
    CanvasToolContext ctx;
    ctx.document = m_document;
    ctx.navigator = m_navigatorOwner ? m_navigator : nullptr;
    ctx.selection = &m_selection;
    ctx.hitTolerancePixels = m_hitTolerance;
    ctx.fillColor = m_fillColor;
    ctx.strokeColor = m_strokeColor;
    ctx.strokeWidth = m_strokeWidth;
    return ctx;
}

bool ToolController::beginGesture(const QString &viewId, const CanvasToolInput &input,
                                  CanvasNavigator *navigator, QObject *navigatorOwner)
{
    if (m_tool) {
        m_lastOutcome = QStringLiteral("rejected:busy");
        emit stateChanged();
        return false;
    }
    CanvasTool *tool = findTool(effectiveTool());
    if (!tool || viewId.isEmpty() || (tool->editsDocument() && !m_document)) {
        m_lastOutcome = QStringLiteral("rejected:unavailable");
        emit stateChanged();
        return false;
    }
    m_navigator = navigator;
    m_navigatorOwner = navigatorOwner;
    const QVector<CanvasObjectId> selectionBefore = m_selection;
    CanvasToolContext ctx = context();
    QString declined;
    if (!tool->begin(ctx, input, &declined)) {
        m_navigator = nullptr;
        m_navigatorOwner = nullptr;
        m_lastOutcome = QStringLiteral("declined:") + declined;
        emit stateChanged();
        return false;
    }
    m_tool = tool;
    m_owner = viewId;
    m_baseRevision = m_document ? m_document->revision() : 0;
    m_lastOutcome = QStringLiteral("active");
    emit stateChanged();
    emit previewChanged();
    if (m_selection != selectionBefore) {
        emit selectionChanged();
    }
    return true;
}

bool ToolController::updateGesture(const QString &viewId, const CanvasToolInput &input)
{
    if (!m_tool || viewId != m_owner) {
        return false;
    }
    CanvasToolContext ctx = context();
    m_tool->update(ctx, input);
    emit previewChanged();
    return true;
}

bool ToolController::endGesture(const QString &viewId, const CanvasToolInput &input)
{
    if (!m_tool || viewId != m_owner) {
        return false;
    }
    CanvasTool *tool = m_tool;
    const QVector<CanvasObjectId> selectionBefore = m_selection;
    CanvasToolContext ctx = context();
    const CanvasEdit edit = tool->finish(ctx, input);
    QString outcome = QStringLiteral("completed");
    if (!edit.isEmpty()) {
        if (!m_document || m_document->revision() != m_baseRevision) {
            outcome = QStringLiteral("rejected:stale");
        } else {
            m_committing = true;
            const bool ok = m_document->commit(edit);
            m_committing = false;
            outcome = ok ? QStringLiteral("committed") : QStringLiteral("rejected:stale");
        }
    }
    finishGesture(outcome);
    if (m_selection != selectionBefore) {
        pruneSelection();
        emit selectionChanged();
    }
    if (outcome == QLatin1String("committed")) {
        emit gestureCommitted(tool->id(), m_document ? m_document->revision() : 0);
    }
    return true;
}

void ToolController::cancel(const QString &reason)
{
    if (!m_tool) {
        return;
    }
    CanvasTool *tool = m_tool;
    const QVector<CanvasObjectId> selectionBefore = m_selection;
    CanvasToolContext ctx = context();
    tool->cancel(ctx);
    finishGesture(QStringLiteral("canceled:") + reason);
    if (m_selection != selectionBefore) {
        pruneSelection();
        emit selectionChanged();
    }
    emit gestureCanceled(tool->id(), reason);
}

void ToolController::lostGrab(const QString &viewId)
{
    if (m_tool && viewId == m_owner) {
        cancel(QStringLiteral("lost-grab"));
    }
}

void ToolController::viewDetached(const QString &viewId)
{
    if (m_tool && viewId == m_owner) {
        cancel(QStringLiteral("view-detached"));
    }
}

void ToolController::finishGesture(const QString &outcome)
{
    m_tool = nullptr;
    m_owner.clear();
    m_navigator = nullptr;
    m_navigatorOwner = nullptr;
    m_lastOutcome = outcome;
    emit stateChanged();
    emit previewChanged();
}

void ToolController::onDocumentEdited(quint64)
{
    if (m_committing) {
        return;
    }
    // An edit from elsewhere (undo, another view, a script) makes the
    // gesture's base state stale; navigation gestures are unaffected.
    if (m_tool && m_tool->editsDocument()) {
        cancel(QStringLiteral("document-changed"));
    }
    pruneSelection();
}

void ToolController::pruneSelection()
{
    if (!m_document) {
        return;
    }
    const qsizetype before = m_selection.size();
    m_selection.erase(std::remove_if(m_selection.begin(), m_selection.end(),
                                     [this](CanvasObjectId id) { return !m_document->object(id); }),
                      m_selection.end());
    if (m_selection.size() != before) {
        emit selectionChanged();
    }
}

CanvasToolPreview ToolController::preview() const
{
    return m_tool ? m_tool->preview() : CanvasToolPreview{};
}

QVariantMap ToolController::previewMap() const
{
    const CanvasToolPreview p = preview();
    static const char *const kinds[] = {"none", "marquee", "rect", "ellipse", "polyline", "move"};
    QVariantList points;
    for (const QPointF &pt : p.points) {
        points << pt;
    }
    QVariantList ids;
    for (CanvasObjectId id : p.ids) {
        ids << qulonglong(id);
    }
    return {{QStringLiteral("kind"), QString::fromLatin1(kinds[p.kind])},
            {QStringLiteral("rect"), p.rect},
            {QStringLiteral("points"), points},
            {QStringLiteral("offset"), p.offset},
            {QStringLiteral("ids"), ids}};
}

void ToolController::setSelection(const QVector<CanvasObjectId> &ids)
{
    QVector<CanvasObjectId> next;
    for (CanvasObjectId id : ids) {
        if ((!m_document || m_document->object(id)) && !next.contains(id)) {
            next.append(id);
        }
    }
    if (next != m_selection) {
        m_selection = next;
        emit selectionChanged();
    }
}

QVariantList ToolController::selectionList() const
{
    QVariantList out;
    for (CanvasObjectId id : m_selection) {
        out << qulonglong(id);
    }
    return out;
}

void ToolController::setSelectionList(const QVariantList &ids)
{
    QVector<CanvasObjectId> next;
    for (const QVariant &v : ids) {
        next << v.toULongLong();
    }
    setSelection(next);
}

bool ToolController::begin(const QString &viewId, qreal x, qreal y, Device device,
                           int modifiers, qreal pressure)
{
    CanvasToolInput input;
    input.documentPoint = input.viewPoint = QPointF(x, y);
    input.device = CanvasDevice(device);
    input.modifiers = Qt::KeyboardModifiers(modifiers);
    input.pressure = pressure;
    return beginGesture(viewId, input);
}

bool ToolController::update(const QString &viewId, qreal x, qreal y, qreal pressure)
{
    CanvasToolInput input;
    input.documentPoint = input.viewPoint = QPointF(x, y);
    input.pressure = pressure;
    return updateGesture(viewId, input);
}

bool ToolController::end(const QString &viewId, qreal x, qreal y)
{
    CanvasToolInput input;
    input.documentPoint = input.viewPoint = QPointF(x, y);
    return endGesture(viewId, input);
}

} // namespace QindaTK
