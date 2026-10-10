// SPDX-License-Identifier: LGPL-3.0-or-later
// RichTextArea command IDs shared by keys, FormatBar, menus and CommandRegistry.
#include "rich_text_area.h"
#include "command_registry.h"
#include "rich_text_clipboard.h"
#include "rich_text_codec.h"
#include "rich_text_codec_p.h"
#include "rich_text_format_state.h"

namespace QindaTK {
using namespace RichText;

namespace {

struct CommandInfo {
    const char *id;
    const char *label;
    const char *icon;
    const char *shortcut;
    bool checkable;
    bool edits;
};

// AGENT-CONTRACT (docs/rich-text.md#commands, FormatBar.qml): these IDs are
// public; renaming one breaks FormatBar and host menus.
const CommandInfo Commands[] = {
    {"text.bold", "Bold", "bold", "Ctrl+B", true, true},
    {"text.italic", "Italic", "italic", "Ctrl+I", true, true},
    {"text.underline", "Underline", "underline", "Ctrl+U", true, true},
    {"text.strike", "Strikethrough", "strikethrough", "", true, true},
    {"text.superscript", "Superscript", "superscript", "", true, true},
    {"text.subscript", "Subscript", "subscript", "", true, true},
    {"text.fontFamily", "Font", "type", "", false, true},
    {"text.fontSize", "Font size", "a-large-small", "", false, true},
    {"text.color", "Text colour", "palette", "", false, true},
    {"text.highlight", "Highlight", "highlighter", "", false, true},
    {"text.link", "Link", "link", "Ctrl+K", true, true},
    {"text.clearFormatting", "Clear formatting", "eraser", "", false, true},
    {"paragraph.align", "Align", "text-align-start", "", false, true},
    {"paragraph.heading", "Heading", "heading", "", false, true},
    {"paragraph.bulletList", "Bulleted list", "list", "", true, true},
    {"paragraph.numberedList", "Numbered list", "list-ordered", "", true, true},
    {"paragraph.indent", "Increase list level", "list-indent-increase", "", false, true},
    {"paragraph.outdent", "Decrease list level", "list-indent-decrease", "", false, true},
    {"insert.table", "Insert table", "table", "", false, true},
    {"table.delete", "Delete table", "trash-2", "", false, true},
    {"insert.pageBreak", "Page break", "file-plus", "Ctrl+Return", false, true},
    {"insert.object", "Insert object", "image", "", false, true},
    {"edit.undo", "Undo", "undo-2", "Ctrl+Z", false, true},
    {"edit.redo", "Redo", "redo-2", "Ctrl+Shift+Z", false, true},
    {"edit.cut", "Cut", "scissors", "Ctrl+X", false, true},
    {"edit.copy", "Copy", "copy", "Ctrl+C", false, false},
    {"edit.paste", "Paste", "clipboard-paste", "Ctrl+V", false, true},
    {"edit.selectAll", "Select all", "text-cursor-input", "Ctrl+A", false, false},
    {"paragraph.style", "Paragraph style", "pilcrow", "", false, true},
    {"text.style", "Character style", "case-sensitive", "", false, true},
    {"navigate.nextFrame", "Next text frame", "arrow-down", "Ctrl+Alt+PgDown", false, false},
    {"navigate.previousFrame", "Previous text frame", "arrow-up", "Ctrl+Alt+PgUp", false, false},
};

const CommandInfo *findCommand(const QString &id)
{
    for (const CommandInfo &info : Commands)
        if (id == QLatin1String(info.id)) return &info;
    return nullptr;
}

} // namespace

QStringList RichTextArea::commandIds() const
{
    QStringList ids;
    for (const CommandInfo &info : Commands) ids.append(QLatin1String(info.id));
    return ids;
}

QVariantMap RichTextArea::commandState(const QString &commandId) const
{
    const CommandInfo *info = findCommand(commandId);
    if (!info) return {};
    const RichTextFormatState *s = m_formatState;
    const bool editable = s->editable();
    bool enabled = m_document && (!info->edits || editable);
    int check = 0;
    QVariant value;
    if (commandId == u"text.bold") check = s->bold();
    else if (commandId == u"text.italic") check = s->italic();
    else if (commandId == u"text.underline") check = s->underline();
    else if (commandId == u"text.strike") check = s->strike();
    else if (commandId == u"text.superscript") check = s->superscript();
    else if (commandId == u"text.subscript") check = s->subscript();
    else if (commandId == u"text.fontFamily") value = s->fontFamily();
    else if (commandId == u"text.fontSize") value = s->fontSize();
    else if (commandId == u"text.color") value = s->color();
    else if (commandId == u"text.highlight") value = s->highlightMixed() ? QVariant() : QVariant(s->highlight());
    else if (commandId == u"text.link") { check = s->linkMixed() ? 1 : s->link().isEmpty() ? 0 : 2; value = s->link(); }
    else if (commandId == u"paragraph.align") value = s->alignment();
    else if (commandId == u"paragraph.heading") value = s->heading();
    else if (commandId == u"paragraph.bulletList") check = s->list() == u"bullet" ? 2 : s->list() == u"mixed" ? 1 : 0;
    else if (commandId == u"paragraph.numberedList") check = s->list() == u"number" ? 2 : s->list() == u"mixed" ? 1 : 0;
    else if (commandId == u"paragraph.indent" || commandId == u"paragraph.outdent") enabled = enabled && s->list() != u"none";
    // With externalUndo the host decides (D-321): the command asks it.
    else if (commandId == u"edit.undo") enabled = m_document && (m_document->canUndo() || m_document->forwardsHistory());
    else if (commandId == u"edit.redo") enabled = m_document && (m_document->canRedo() || m_document->forwardsHistory());
    else if (commandId == u"edit.cut" || commandId == u"edit.copy") enabled = enabled && hasSelection();
    else if (commandId == u"insert.pageBreak" || commandId == u"insert.table") {
        const Paragraph *p = m_document ? m_document->paragraph(m_focus.position.paragraph) : nullptr;
        enabled = enabled && p && !p->table;
    } else if (commandId == u"table.delete") {
        const Paragraph *p = m_document ? m_document->paragraph(m_focus.position.paragraph) : nullptr;
        enabled = enabled && p && p->table;
    } else if (commandId == u"paragraph.style" || commandId == u"text.style") {
        const Paragraph *p = m_document ? m_document->paragraph(m_focus.position.paragraph) : nullptr;
        if (p) value = commandId == u"paragraph.style" ? p->format.styleId
                                                      : m_document->charFormat(m_document->formatIndexAt(m_focus.position)).styleId;
    } else if (commandId == u"navigate.nextFrame" || commandId == u"navigate.previousFrame") {
        const QStringList story = m_document ? m_document->frameSet().storyChain() : QStringList{};
        const qsizetype at = story.indexOf(caretFrameId());
        enabled = enabled && paged() && !story.isEmpty()
            && (commandId == u"navigate.nextFrame" ? at + 1 < story.size() : at != 0);
    }
    return {{QStringLiteral("enabled"), enabled},
            {QStringLiteral("checked"), check == 2},
            {QStringLiteral("mixed"), check == 1},
            {QStringLiteral("checkState"), check},
            {QStringLiteral("value"), value},
            {QStringLiteral("label"), tr(info->label)},
            {QStringLiteral("iconName"), QLatin1String(info->icon)},
            {QStringLiteral("shortcut"), QLatin1String(info->shortcut)}};
}

bool RichTextArea::invokeCommand(const QString &id, const QVariantMap &args)
{
    const CommandInfo *info = findCommand(id);
    if (!info || !m_document || !commandState(id).value(QStringLiteral("enabled")).toBool()) return false;
    m_commandInFlight = true;
    const auto done = qScopeGuard([this] { m_commandInFlight = false; });
    const QVariant value = args.value(QStringLiteral("value"));
    const RichTextFormatState *s = m_formatState;
    CharFormatPatch patch;
    auto toggle = [&](int state) { return state != 2; }; // mixed or off -> on
    if (id == u"text.bold") patch.weight = toggle(s->bold()) ? 700 : 400;
    else if (id == u"text.italic") patch.italic = qint8(toggle(s->italic()));
    else if (id == u"text.underline") patch.underline = qint8(toggle(s->underline()));
    else if (id == u"text.strike") patch.strike = qint8(toggle(s->strike()));
    else if (id == u"text.superscript") patch.verticalAlign = toggle(s->superscript()) ? VerticalAlign::Superscript : VerticalAlign::Normal;
    else if (id == u"text.subscript") patch.verticalAlign = toggle(s->subscript()) ? VerticalAlign::Subscript : VerticalAlign::Normal;
    else if (id == u"text.fontFamily" && !value.toString().isEmpty()) patch.family = value.toString();
    else if (id == u"text.fontSize" && value.toDouble() >= 1 && value.toDouble() <= 1638) patch.sizePt = value.toDouble();
    else if (id == u"text.color") patch.color = value.toString().isEmpty() ? QColor() : QColor::fromString(value.toString());
    else if (id == u"text.highlight") patch.background = value.toString().isEmpty() ? QColor() : QColor::fromString(value.toString());
    else if (id == u"text.link") patch.href = args.value(QStringLiteral("href"), value).toString();
    else if (id == u"text.style") {
        // Only a character style of this document (or none) can be applied.
        const NamedStyle *style = m_document->style(value.toString());
        if (!value.toString().isEmpty() && (!style || style->kind != StyleKind::Character)) return false;
        patch.styleId = value.toString();
    }
    else if (id == u"text.clearFormatting") {
        patch.family = QString(); patch.sizePt = 0.0; patch.weight = 0; patch.italic = patch.underline = patch.strike = qint8(-1);
        patch.color = QColor(); patch.background = QColor(); patch.verticalAlign = VerticalAlign::Normal;
    }
    if (!patch.isEmpty()) return formatSelection(patch, tr(info->label));
    if (id == u"paragraph.align") {
        const qsizetype index = Codec::alignmentNames().indexOf(value.toString());
        if (index < 0) return false;
        ParagraphFormatPatch p;
        p.alignment = Alignment(index);
        return formatParagraphs(p, tr("Align"));
    }
    if (id == u"paragraph.heading") {
        ParagraphFormatPatch p;
        p.headingLevel = qBound(0, value.toInt(), 6);
        return formatParagraphs(p, tr("Heading"));
    }
    const Selection kept = selection();
    auto keep = [&](const std::function<bool(RichTextDocument &)> &op) {
        const bool ok = edit(tr(info->label), {}, [&](RichTextDocument &doc) -> std::optional<Position> {
            if (!op(doc)) return std::nullopt;
            return kept.focus;
        });
        if (ok) setSelection(kept, false);
        return ok;
    };
    if (id == u"paragraph.bulletList" || id == u"paragraph.numberedList") {
        const bool bullet = id == u"paragraph.bulletList";
        const bool on = commandState(id).value(QStringLiteral("checked")).toBool();
        const std::optional<ListKind> kind = on ? std::nullopt : std::optional<ListKind>(bullet ? ListKind::Disc : ListKind::Decimal);
        return keep([&](RichTextDocument &doc) { return doc.setList(kept.anchor, kept.focus, kind); });
    }
    if (id == u"paragraph.indent" || id == u"paragraph.outdent")
        return keep([&](RichTextDocument &doc) { return doc.changeListLevel(kept.anchor, kept.focus, id == u"paragraph.indent" ? 1 : -1); });
    if (id == u"paragraph.style")
        return keep([&](RichTextDocument &doc) { return doc.applyParagraphStyle(kept.anchor, kept.focus, value.toString()); });
    if (id == u"navigate.nextFrame" || id == u"navigate.previousFrame") {
        // The story's frames in thread order; without a caret frame, the first one.
        const QStringList story = m_document->frameSet().storyChain();
        const qsizetype at = story.indexOf(caretFrameId());
        const qsizetype target = at < 0 ? 0 : at + (id == u"navigate.nextFrame" ? 1 : -1);
        return target >= 0 && target < story.size() && goToFrame(story.at(target));
    }
    if (id == u"insert.table")
        return edit(tr(info->label), {}, [&](RichTextDocument &doc) {
            return doc.insertTable(m_focus.position, qMax(1, args.value(QStringLiteral("rows"), 2).toInt()),
                                   qMax(1, args.value(QStringLiteral("columns"), 2).toInt()),
                                   args.value(QStringLiteral("headerRows"), 0).toInt());
        });
    if (id == u"table.delete") {
        const Paragraph *p = m_document->paragraph(m_focus.position.paragraph);
        const QUuid tableId = p && p->table ? p->table->id : QUuid();
        return edit(tr(info->label), {}, [&](RichTextDocument &doc) { return doc.deleteTable(tableId); });
    }
    if (id == u"insert.pageBreak")
        return edit(tr(info->label), {}, [this](RichTextDocument &doc) { return doc.insertPageBreak(m_focus.position); });
    if (id == u"insert.object") {
        RichObject object;
        object.source = args.value(QStringLiteral("source")).toString();
        object.kind = args.value(QStringLiteral("kind"), QStringLiteral("image")).toString();
        object.widthPt = args.value(QStringLiteral("widthPt"), 72).toDouble();
        object.heightPt = args.value(QStringLiteral("heightPt"), 72).toDouble();
        object.altText = args.value(QStringLiteral("alt")).toString();
        const qsizetype placement = Codec::placementNames().indexOf(args.value(QStringLiteral("placement"), QStringLiteral("inline")).toString());
        object.placement = placement < 0 ? Placement::Inline : Placement(placement);
        return edit(tr(info->label), {}, [&](RichTextDocument &doc) { return doc.insertObject(m_focus.position, object); });
    }
    if (id == u"edit.undo") return m_document->requestUndo();
    if (id == u"edit.redo") return m_document->requestRedo();
    if (id == u"edit.copy") return copySelection(false);
    if (id == u"edit.cut") return copySelection(true);
    if (id == u"edit.paste") return paste();
    if (id == u"edit.selectAll") return selectAll(), true;
    return false;
}

bool RichTextArea::registerCommands(CommandRegistry *registry, const QString &scopeToken)
{
    if (!registry) return false;
    bool ok = true;
    for (const CommandInfo &info : Commands) {
        const QString id = QLatin1String(info.id);
        ok = registry->registerCommand({id, tr(info.label), QLatin1String(info.icon), QStringLiteral("Text"),
                                        {}, QLatin1String(info.shortcut), info.checkable}) && ok;
        ok = registry->registerHandler(scopeToken, id, this,
            [this, id](const CommandInvocationContext &, const QVariantMap &arguments) -> OperationError {
                if (invokeCommand(id, arguments)) return {};
                return {ErrorCode::Validation, tr("%1 is not available here").arg(id), {}, QStringLiteral("richtext")};
            },
            [this, id](const CommandInvocationContext &) {
                const QVariantMap state = commandState(id);
                return CommandState{state.value(QStringLiteral("enabled")).toBool(),
                                    state.value(QStringLiteral("checked")).toBool(), true, {}};
            }) && ok;
    }
    connect(m_formatState, &RichTextFormatState::changed, registry, [registry] { registry->invalidate(); });
    return ok;
}

} // namespace QindaTK
