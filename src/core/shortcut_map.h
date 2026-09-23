// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "command_registry.h"
#include <QKeySequence>
#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQml/qqmlregistration.h>

class QKeyEvent;
namespace QindaTK {

// Window-local keyboard routing for semantic commands. Bindings use portable
// QKeySequence text for storage and native text only for presentation. A
// scopeToken is a live CommandContext token; an empty token is window-global.
class ShortcutMap : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QindaTK::CommandRegistry *registry READ registry WRITE setRegistry NOTIFY registryChanged)
    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(int chordTimeoutMs READ chordTimeoutMs WRITE setChordTimeoutMs NOTIFY chordTimeoutMsChanged)
    Q_PROPERTY(QString pendingChord READ pendingChord NOTIFY pendingChordChanged)
    Q_PROPERTY(QVariantList bindings READ bindings NOTIFY changed)
public:
    explicit ShortcutMap(QObject *parent = nullptr);
    ~ShortcutMap() override;
    CommandRegistry *registry() const { return m_registry; }
    void setRegistry(CommandRegistry *registry);
    QQuickWindow *window() const { return m_window; }
    void setWindow(QQuickWindow *window);
    int chordTimeoutMs() const { return m_chordTimeoutMs; }
    void setChordTimeoutMs(int value);
    QString pendingChord() const;

    Q_INVOKABLE bool bind(const QString &commandId, const QString &portableSequence,
                          const QString &scopeToken = {}, bool allowInTextInput = false);
    Q_INVOKABLE bool bindCommand(const QString &commandId,
                                 const QString &scopeToken = {},
                                 bool allowInTextInput = false);
    Q_INVOKABLE bool unbind(const QString &commandId, const QString &scopeToken = {});
    Q_INVOKABLE bool setOverride(const QString &commandId, const QString &portableSequence);
    Q_INVOKABLE void resetOverride(const QString &commandId);
    Q_INVOKABLE QVariantMap overrides() const;
    Q_INVOKABLE bool loadOverrides(const QVariantMap &values);
    Q_INVOKABLE QString displaySequence(const QString &commandId) const;
    Q_INVOKABLE bool hasBinding(const QString &commandId) const;
    Q_INVOKABLE QVariantList conflicts() const;
    Q_INVOKABLE QVariantList bindings() const;
    Q_INVOKABLE QVariantMap validateOverrides(const QVariantMap &values) const;
    Q_INVOKABLE bool dispatchPortable(const QString &portableStroke,
                                      bool textInput = false, bool composing = false);
    Q_INVOKABLE void cancelChord();
signals:
    void registryChanged();
    void windowChanged();
    void chordTimeoutMsChanged();
    void pendingChordChanged();
    void changed();
    void conflict(const QString &portableSequence, const QStringList &commandIds);
    void triggered(const QString &commandId);
private:
    struct Binding {
        QString commandId;
        QString scopeToken;
        QKeySequence defaultSequence;
        QKeySequence overrideSequence;
        bool hasOverride = false;
        bool allowInTextInput = false;
        QKeySequence effective() const { return hasOverride ? overrideSequence : defaultSequence; }
    };
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool dispatchStroke(QKeyCombination stroke, bool textInput, bool composing,
                        const QKeyEvent *event = nullptr);
    bool active(const Binding &binding) const;
    int rank(const Binding &binding) const;
    bool isTextEditingKey(const QKeyEvent &event) const;
    bool focusIsComposing() const;
    bool focusIsTextInput() const;
    void attachContext();
    void clearScopedBindings();
    QPointer<CommandRegistry> m_registry;
    QPointer<QQuickWindow> m_window;
    QVector<Binding> m_bindings;
    QVector<QKeyCombination> m_pending;
    QTimer m_chordTimer;
    int m_chordTimeoutMs = 1200;
    QMetaObject::Connection m_registryDestroyed;
    QMetaObject::Connection m_registryContextChanged;
    QMetaObject::Connection m_scopeRemoved;
    QMetaObject::Connection m_windowDestroyed;
    QMetaObject::Connection m_focusChanged;
};
} // namespace QindaTK
