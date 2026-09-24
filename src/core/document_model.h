// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "application_contracts.h"
#include <QMetaObject>
#include <QPointer>
#include <functional>
#include <memory>

namespace QindaTK {
class UndoService;

// AGENT-CONTRACT (docs/document-models.md#commands): one recorded, immutable
// history step. UndoService owns the step's lifetime; only the model that
// produced it interprets it. retainedBytes() estimates the memory the step
// keeps alive beyond the live document and is charged against the service's
// byte budget. A step never holds a pointer to a live QObject.
class DocumentCommand {
public:
    virtual ~DocumentCommand() = default;
    [[nodiscard]] virtual quint64 retainedBytes() const = 0;
    // True when applying the step would change nothing (for example a macro
    // whose edits cancel out); such a step is never recorded.
    [[nodiscard]] virtual bool isNoOp() const { return false; }
};
using DocumentCommandPtr = std::shared_ptr<const DocumentCommand>;

// AGENT-CONTRACT (docs/document-models.md#model): the domain half of a
// DocumentSession. A model lives on the GUI thread and owns its revision,
// reset generation and content. It changes content in two phases:
// prepareCommand() validates a step against the current state without any
// mutation or notification; publishCommand() then applies the whole step,
// increments the revision once and notifies, and must not fail after a
// successful prepare on the same thread. UndoService holds at most ONE live
// history per model (historyService()); a model is never recorded twice.
class DocumentModel {
public:
    virtual ~DocumentModel();
    [[nodiscard]] virtual QObject *modelObject() const = 0;
    [[nodiscard]] virtual DocumentId modelId() const = 0;
    // Stable family name such as "property" or "canvas".
    [[nodiscard]] virtual QString modelKind() const = 0;
    // schemaId, document ID, revision and reset generation of the live state.
    [[nodiscard]] virtual SnapshotHeader modelHeader() const = 0;
    // Connects the notification a model emits after every committed change,
    // undo, redo, fixture load or reset.
    virtual QMetaObject::Connection connectModelChanged(QObject *receiver,
                                                        std::function<void()> slot) = 0;
    [[nodiscard]] virtual OperationError prepareCommand(const DocumentCommand &command,
                                                        bool forward) const = 0;
    virtual void publishCommand(const DocumentCommand &command, bool forward) = 0;
    // One step equivalent to `first` followed by `second`, for macros and
    // merge keys; null when this model cannot group the two.
    [[nodiscard]] virtual DocumentCommandPtr composeCommands(const DocumentCommand &first,
                                                             const DocumentCommand &second) const;
    // The service holding this model's single live history, if any.
    [[nodiscard]] UndoService *historyService() const;

protected:
    DocumentModel() = default;
    // Content changed outside recorded history (a fixture load or a direct
    // insert): a saved clean state is no longer reachable by undo/redo.
    void invalidateCleanState();
    // A reset or structural change: recorded steps can no longer apply.
    void invalidateHistory();
    // Called after this model's live history moved to `service` (nullptr when
    // it left one). Never called while the model is being destroyed.
    virtual void historyAttached(UndoService *service) { Q_UNUSED(service) }

private:
    friend class UndoService;
    QPointer<QObject> m_history;
    QString m_historyKey;
    Q_DISABLE_COPY_MOVE(DocumentModel)
};

} // namespace QindaTK
