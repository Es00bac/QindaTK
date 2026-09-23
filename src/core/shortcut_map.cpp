// SPDX-License-Identifier: LGPL-3.0-or-later
#include "shortcut_map.h"
#include "command_context.h"
#include "command_registry.h"
#include <QEvent>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <algorithm>

namespace QindaTK {
namespace {
QKeySequence parseSequence(const QString &text)
{
    const auto parsed = QKeySequence::fromString(text, QKeySequence::PortableText);
    return parsed.isEmpty() || parsed.toString(QKeySequence::PortableText).isEmpty()
               ? QKeySequence{} : parsed;
}
QKeySequence sequenceOf(const QVector<QKeyCombination> &strokes)
{
    const auto empty = QKeyCombination::fromCombined(0);
    return QKeySequence(strokes.value(0, empty), strokes.value(1, empty),
                        strokes.value(2, empty), strokes.value(3, empty));
}
bool overlap(const QKeySequence &a, const QKeySequence &b)
{
    return a.matches(b) != QKeySequence::NoMatch
        || b.matches(a) != QKeySequence::NoMatch;
}
} // namespace

ShortcutMap::ShortcutMap(QObject *parent) : QObject(parent)
{
    m_chordTimer.setSingleShot(true);
    connect(&m_chordTimer, &QTimer::timeout, this, &ShortcutMap::cancelChord);
}

ShortcutMap::~ShortcutMap()
{
    if (m_window) m_window->removeEventFilter(this);
}

void ShortcutMap::setRegistry(CommandRegistry *registry)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_registry == registry) return;
    QObject::disconnect(m_registryDestroyed);
    QObject::disconnect(m_registryContextChanged);
    QObject::disconnect(m_scopeRemoved);
    clearScopedBindings();
    m_registry = registry;
    if (registry) {
        m_registryContextChanged = connect(registry, &CommandRegistry::contextChanged,
                                           this, [this] {
            clearScopedBindings();
            attachContext();
        });
        m_registryDestroyed = connect(registry, &QObject::destroyed, this, [this] {
            m_registry = nullptr;
            QObject::disconnect(m_scopeRemoved);
            clearScopedBindings();
            cancelChord();
            emit registryChanged();
            emit changed();
        });
        attachContext();
    }
    cancelChord();
    emit registryChanged();
    emit changed();
}

void ShortcutMap::clearScopedBindings()
{
    const auto before = m_bindings.size();
    m_bindings.erase(std::remove_if(m_bindings.begin(), m_bindings.end(),
        [](const Binding &binding) { return !binding.scopeToken.isEmpty(); }),
        m_bindings.end());
    if (m_bindings.size() != before) {
        cancelChord();
        emit changed();
    }
}

void ShortcutMap::attachContext()
{
    QObject::disconnect(m_scopeRemoved);
    if (!m_registry || !m_registry->context()) return;
    m_scopeRemoved = connect(m_registry->context(), &CommandContext::scopeRemoved,
                             this, [this](const QString &token) {
        const auto before = m_bindings.size();
        m_bindings.erase(std::remove_if(m_bindings.begin(), m_bindings.end(),
            [&token](const Binding &binding) { return binding.scopeToken == token; }),
            m_bindings.end());
        if (m_bindings.size() != before) {
            cancelChord();
            emit changed();
        }
    });
}

void ShortcutMap::setWindow(QQuickWindow *window)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_window == window) return;
    if (m_window) m_window->removeEventFilter(this);
    QObject::disconnect(m_windowDestroyed);
    QObject::disconnect(m_focusChanged);
    m_window = window;
    if (window) {
        window->installEventFilter(this);
        m_focusChanged = connect(window, &QQuickWindow::activeFocusItemChanged,
                                 this, &ShortcutMap::cancelChord);
        m_windowDestroyed = connect(window, &QObject::destroyed, this, [this] {
            m_window = nullptr;
            cancelChord();
            emit windowChanged();
        });
    }
    cancelChord();
    emit windowChanged();
}

void ShortcutMap::setChordTimeoutMs(int value)
{
    if (value < 1 || value == m_chordTimeoutMs) return;
    m_chordTimeoutMs = value;
    if (m_chordTimer.isActive()) m_chordTimer.start(value);
    emit chordTimeoutMsChanged();
}

QString ShortcutMap::pendingChord() const
{
    return sequenceOf(m_pending).toString(QKeySequence::NativeText);
}

bool ShortcutMap::bind(const QString &commandId, const QString &portableSequence,
                       const QString &scopeToken, bool allowInTextInput)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (commandId.isEmpty() || portableSequence.isEmpty()) return false;
    const auto sequence = parseSequence(portableSequence);
    if (sequence.isEmpty()) return false;
    if (!scopeToken.isEmpty()
        && (!m_registry || !m_registry->context()
            || !m_registry->context()->contains(scopeToken))) return false;
    for (auto &entry : m_bindings) {
        if (entry.commandId != commandId || entry.scopeToken != scopeToken) continue;
        entry.defaultSequence = sequence;
        entry.allowInTextInput = allowInTextInput;
        cancelChord();
        emit changed();
        return true;
    }
    m_bindings.append({commandId, scopeToken, sequence, {}, false, allowInTextInput});
    cancelChord();
    emit changed();
    return true;
}

bool ShortcutMap::bindCommand(const QString &commandId, const QString &scopeToken,
                              bool allowInTextInput)
{
    if (!m_registry) return false;
    const auto presentation = m_registry->resolve(commandId);
    if (presentation.descriptor.label.isEmpty()
        || presentation.descriptor.defaultShortcut.isEmpty()) return false;
    return bind(commandId, presentation.descriptor.defaultShortcut,
                scopeToken, allowInTextInput);
}

bool ShortcutMap::unbind(const QString &commandId, const QString &scopeToken)
{
    for (qsizetype i = 0; i < m_bindings.size(); ++i) {
        if (m_bindings[i].commandId != commandId || m_bindings[i].scopeToken != scopeToken)
            continue;
        m_bindings.removeAt(i);
        cancelChord();
        emit changed();
        return true;
    }
    return false;
}

bool ShortcutMap::setOverride(const QString &commandId, const QString &portableSequence)
{
    const auto sequence = portableSequence.isEmpty() ? QKeySequence{}
                                                 : parseSequence(portableSequence);
    if (!portableSequence.isEmpty() && sequence.isEmpty()) return false;
    bool found = false;
    for (auto &entry : m_bindings) {
        if (entry.commandId != commandId) continue;
        entry.overrideSequence = sequence;
        entry.hasOverride = true;
        found = true;
    }
    if (found) {
        cancelChord();
        emit changed();
    }
    return found;
}

void ShortcutMap::resetOverride(const QString &commandId)
{
    bool changedAny = false;
    for (auto &entry : m_bindings) {
        if (entry.commandId != commandId || !entry.hasOverride) continue;
        entry.hasOverride = false;
        entry.overrideSequence = {};
        changedAny = true;
    }
    if (changedAny) {
        cancelChord();
        emit changed();
    }
}

QVariantMap ShortcutMap::overrides() const
{
    QVariantMap result;
    for (const auto &entry : m_bindings)
        if (entry.hasOverride)
            result.insert(entry.commandId,
                          entry.overrideSequence.toString(QKeySequence::PortableText));
    return result;
}

bool ShortcutMap::loadOverrides(const QVariantMap &values)
{
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        bool known = false;
        for (const auto &entry : m_bindings)
            if (entry.commandId == it.key()) { known = true; break; }
        if (!known || it.value().metaType().id() != QMetaType::QString
            || (!it.value().toString().isEmpty()
                && parseSequence(it.value().toString()).isEmpty())) return false;
    }
    for (auto &entry : m_bindings) {
        const auto it = values.constFind(entry.commandId);
        entry.hasOverride = it != values.cend();
        entry.overrideSequence = entry.hasOverride && !it.value().toString().isEmpty()
            ? parseSequence(it.value().toString()) : QKeySequence{};
    }
    cancelChord();
    emit changed();
    return true;
}

QString ShortcutMap::displaySequence(const QString &commandId) const
{
    int best = 1000002;
    QString result;
    for (const auto &entry : m_bindings) {
        if (entry.commandId != commandId || !active(entry)) continue;
        if (rank(entry) >= best) continue;
        best = rank(entry);
        result = entry.effective().toString(QKeySequence::NativeText);
    }
    return result;
}

bool ShortcutMap::hasBinding(const QString &commandId) const
{
    for (const auto &entry : m_bindings)
        if (entry.commandId == commandId && active(entry)) return true;
    return false;
}

bool ShortcutMap::active(const Binding &binding) const
{
    if (binding.scopeToken.isEmpty()) return true;
    return m_registry && m_registry->context()
        && m_registry->context()->activePath().contains(binding.scopeToken);
}

int ShortcutMap::rank(const Binding &binding) const
{
    if (binding.scopeToken.isEmpty()) return 1000000;
    if (!m_registry || !m_registry->context()) return 1000001;
    const int index = m_registry->context()->activePath().indexOf(binding.scopeToken);
    return index < 0 ? 1000001 : index;
}

QVariantList ShortcutMap::conflicts() const
{
    QVariantList result;
    for (qsizetype i = 0; i < m_bindings.size(); ++i) {
        const auto &a = m_bindings[i];
        if (!active(a) || a.effective().isEmpty()) continue;
        for (qsizetype j = i + 1; j < m_bindings.size(); ++j) {
            const auto &b = m_bindings[j];
            if (!active(b) || b.effective().isEmpty() || rank(a) != rank(b)
                || !overlap(a.effective(), b.effective())) continue;
            result.append(QVariantMap{{QStringLiteral("first"), a.commandId},
                                      {QStringLiteral("second"), b.commandId},
                                      {QStringLiteral("sequence"),
                                       a.effective().toString(QKeySequence::PortableText)},
                                      {QStringLiteral("otherSequence"),
                                       b.effective().toString(QKeySequence::PortableText)}});
        }
    }
    return result;
}

void ShortcutMap::cancelChord()
{
    if (m_pending.isEmpty()) return;
    m_pending.clear();
    m_chordTimer.stop();
    emit pendingChordChanged();
}

bool ShortcutMap::focusIsComposing() const
{
    const auto *focus = m_window ? m_window->activeFocusItem() : nullptr;
    return focus && focus->property("inputMethodComposing").toBool();
}

bool ShortcutMap::focusIsTextInput() const
{
    const auto *focus = m_window ? m_window->activeFocusItem() : nullptr;
    return focus && (focus->flags() & QQuickItem::ItemAcceptsInputMethod);
}

bool ShortcutMap::isTextEditingKey(const QKeyEvent &event) const
{
    const auto modifiers = event.modifiers() & ~(Qt::KeypadModifier | Qt::GroupSwitchModifier);
    if (modifiers == Qt::NoModifier || modifiers == Qt::ShiftModifier)
        return true;
    constexpr QKeySequence::StandardKey keys[] = {
        QKeySequence::Cut, QKeySequence::Copy, QKeySequence::Paste,
        QKeySequence::Undo, QKeySequence::Redo, QKeySequence::SelectAll,
        QKeySequence::Delete, QKeySequence::Backspace,
        QKeySequence::MoveToNextWord, QKeySequence::MoveToPreviousWord,
        QKeySequence::MoveToStartOfLine, QKeySequence::MoveToEndOfLine,
        QKeySequence::MoveToStartOfDocument, QKeySequence::MoveToEndOfDocument,
        QKeySequence::SelectNextWord, QKeySequence::SelectPreviousWord,
        QKeySequence::DeleteStartOfWord, QKeySequence::DeleteEndOfWord};
    return std::any_of(std::begin(keys), std::end(keys),
                       [&event](auto key) { return event.matches(key); });
}

bool ShortcutMap::dispatchStroke(QKeyCombination stroke, bool textInput,
                                 bool composing, const QKeyEvent *event)
{
    if (composing || !m_registry || stroke.key() == Qt::Key_unknown) {
        cancelChord();
        return false;
    }
    if (stroke.key() == Qt::Key_Escape && !m_pending.isEmpty()) {
        cancelChord();
        return true;
    }
    if (m_pending.size() >= 4) cancelChord();
    auto attempt = [this, stroke, textInput, event]() -> int {
        QVector<QKeyCombination> typed = m_pending;
        typed.append(stroke);
        const auto sequence = sequenceOf(typed);
        int bestRank = 1000002;
        QVector<const Binding *> exact;
        QVector<const Binding *> partial;
        for (const auto &binding : m_bindings) {
            if (!active(binding) || binding.effective().isEmpty()) continue;
            if (textInput && m_pending.isEmpty() && !binding.allowInTextInput
                && (!event || isTextEditingKey(*event))) continue;
            const auto presentation = m_registry->resolve(binding.commandId);
            if (!presentation.enabled) continue;
            const auto match = sequence.matches(binding.effective());
            if (match == QKeySequence::NoMatch) continue;
            const int candidateRank = rank(binding);
            if (candidateRank > bestRank) continue;
            if (candidateRank < bestRank) {
                bestRank = candidateRank;
                exact.clear();
                partial.clear();
            }
            (match == QKeySequence::ExactMatch ? exact : partial).append(&binding);
        }
        if (exact.isEmpty() && partial.isEmpty()) return 0;
        if (exact.size() > 1 || (!exact.isEmpty() && !partial.isEmpty())) {
            QStringList ids;
            for (const auto *entry : exact) ids.append(entry->commandId);
            for (const auto *entry : partial) ids.append(entry->commandId);
            emit conflict(sequence.toString(QKeySequence::PortableText), ids);
            cancelChord();
            return 1;
        }
        if (exact.size() == 1) {
            const QString id = exact.first()->commandId;
            cancelChord();
            if (m_registry->trigger(id)) emit triggered(id);
            return 1;
        }
        m_pending = typed;
        m_chordTimer.start(m_chordTimeoutMs);
        emit pendingChordChanged();
        return 1;
    };
    if (attempt()) return true;
    if (!m_pending.isEmpty()) {
        cancelChord();
        return attempt() != 0;
    }
    return false;
}

bool ShortcutMap::dispatchPortable(const QString &portableStroke,
                                   bool textInput, bool composing)
{
    const auto sequence = parseSequence(portableStroke);
    if (sequence.count() != 1) return false;
    const auto stroke = sequence[0];
    QKeyEvent synthetic(QEvent::KeyPress, stroke.key(), stroke.keyboardModifiers());
    return dispatchStroke(stroke, textInput, composing, &synthetic);
}

bool ShortcutMap::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_window || event->type() != QEvent::KeyPress)
        return QObject::eventFilter(watched, event);
    auto *key = static_cast<QKeyEvent *>(event);
    if (key->isAutoRepeat()) return false;
    const bool used = dispatchStroke(key->keyCombination(), focusIsTextInput(),
                                     focusIsComposing(), key);
    if (used) key->accept();
    return used;
}
} // namespace QindaTK
