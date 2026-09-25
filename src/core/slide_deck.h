// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include "document_model.h"
#include "slide_deck_types.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaTK {
class RichTextDocument;
class UndoService;
class SlideDeckDocumentCodec;

// Persistent slide deck (schema qindatk.slides.v1, docs/slides.md): masters
// with layouts and placeholder defaults, slides with placeholder-bound or
// free objects, rich-text text boxes and notes, transitions and effects.
// AGENT-CONTRACT (docs/document-models.md#commands): every edit is one
// bounded whole-state DocumentCommand recorded by UndoService; a standalone
// deck keeps a private history that a DocumentSession adopts. Object and
// effect IDs are unique in the deck, so their methods need no slide ID.
// The QML snapshots match the SlideSorter, SlideCanvas, SlideMasterEditor
// and AnimationPane inputs (docs/controls.md#slide-authoring-and-presentation).
class SlideDeck : public QObject, public DocumentModel {
    Q_OBJECT
    QML_ELEMENT
    Q_MOC_INCLUDE("undo_service.h")
    Q_MOC_INCLUDE("rich_text_document.h")
    Q_PROPERTY(QString documentId READ documentId CONSTANT)
    Q_PROPERTY(QString revision READ revision NOTIFY changed)
    Q_PROPERTY(QVariantMap slideSize READ slideSize NOTIFY changed)
    Q_PROPERTY(QVariantList slides READ slideSummaries NOTIFY changed)
    Q_PROPERTY(QStringList slideIds READ slideIds NOTIFY changed)
    Q_PROPERTY(QVariantList masters READ masterSummaries NOTIFY changed)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(bool textEditing READ textEditing NOTIFY textEditChanged)
    Q_PROPERTY(QString textEditObjectId READ textEditObjectId NOTIFY textEditChanged)
    Q_PROPERTY(QindaTK::UndoService *undoService READ undoService NOTIFY historyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)
public:
    static constexpr int HistorySteps = 128;
    static constexpr int PreviewWidth = 160;
    explicit SlideDeck(QObject *parent = nullptr);
    ~SlideDeck() override;

    const Slides::DeckState &state() const { return m_state; }
    QString documentId() const { return m_documentId; }
    QString revision() const { return QString::number(m_revision); }
    QVariantMap slideSize() const;
    QVariantList slideSummaries() const;
    QStringList slideIds() const;
    QVariantList masterSummaries() const;
    bool canUndo() const;
    bool canRedo() const;
    bool textEditing() const { return !m_textObjectId.isEmpty(); }
    QString textEditObjectId() const { return m_textObjectId; }
    UndoService *undoService() const;
    QString lastError() const { return m_error; }

    // --- snapshots (QML)
    Q_INVOKABLE QVariantMap slide(const QString &slideId) const;
    Q_INVOKABLE int indexOfSlide(const QString &slideId) const;
    Q_INVOKABLE QVariantList objects(const QString &slideId) const;
    Q_INVOKABLE QVariantList effects(const QString &slideId) const;
    Q_INVOKABLE QVariantList targetChoices(const QString &slideId) const;
    Q_INVOKABLE QVariantList masterChoices() const;
    Q_INVOKABLE QVariantList placeholderOverrides(const QString &masterId) const;
    Q_INVOKABLE QString notes(const QString &slideId) const;
    Q_INVOKABLE QVariantMap schedule(const QString &slideId) const;
    Q_INVOKABLE QVariantMap frame(const QString &slideId, int step, const QString &ticks) const;

    // --- slides
    Q_INVOKABLE QString insertSlide(const QString &afterSlideId = {}, const QString &layoutId = {});
    Q_INVOKABLE QString duplicateSlide(const QString &slideId);
    Q_INVOKABLE bool moveSlide(const QString &slideId, int targetIndex);
    Q_INVOKABLE bool removeSlide(const QString &slideId);
    Q_INVOKABLE bool setSlideHidden(const QString &slideId, bool hidden);
    Q_INVOKABLE bool setSlideSection(const QString &slideId, const QString &section);
    Q_INVOKABLE bool setSlideTitle(const QString &slideId, const QString &title);
    Q_INVOKABLE bool setNotes(const QString &slideId, const QString &text);
    Q_INVOKABLE bool setTransition(const QString &slideId, const QString &kind, double seconds);

    // --- objects
    Q_INVOKABLE QString insertObject(const QString &slideId, const QString &kind,
                                     const QVariantMap &bounds, const QString &text = {});
    Q_INVOKABLE bool removeObject(const QString &objectId);
    Q_INVOKABLE bool moveObject(const QString &objectId, const QVariantMap &bounds);
    Q_INVOKABLE bool setObjectText(const QString &objectId, const QString &text);
    Q_INVOKABLE bool setObjectFill(const QString &objectId, const QString &fill);
    // Rich text box editing through the shared rich-text stack: the returned
    // document (owned by the deck) holds the object's resolved content; bind
    // it to Tk.RichTextEditor. commit stores it as one deck undo step.
    Q_INVOKABLE QindaTK::RichTextDocument *beginTextEdit(const QString &objectId);
    Q_INVOKABLE bool commitTextEdit();
    Q_INVOKABLE void cancelTextEdit();

    // --- masters and inheritance
    Q_INVOKABLE QString createMaster(const QString &title);
    Q_INVOKABLE bool setMasterBackground(const QString &masterId, const QString &background,
                                         const QString &foreground = {});
    Q_INVOKABLE bool setPlaceholderDefault(const QString &masterId, const QString &role,
                                           const QString &text);
    // An empty layoutId moves the master placeholder itself.
    Q_INVOKABLE bool setPlaceholderBounds(const QString &masterId, const QString &layoutId,
                                          const QString &role, const QVariantMap &bounds);
    // Keeps each slide's deliberate overrides; an empty layoutId keeps a
    // layout ID the master also has, else takes its layout with the same
    // title, else its first layout.
    Q_INVOKABLE bool applyMaster(const QString &masterId, const QStringList &slideIds,
                                 const QString &layoutId = {});
    // Clears the text/bounds/fill overrides of the slide's `role` object.
    Q_INVOKABLE bool resetOverride(const QString &slideId, const QString &role);

    // --- effects (seconds in the patch are rounded to milliseconds; the
    // exact alternatives `delayTicks`/`durationTicks` are decimal strings)
    Q_INVOKABLE QString addEffect(const QString &targetId, const QString &kind = QStringLiteral("fade-in"));
    Q_INVOKABLE bool editEffect(const QString &effectId, const QVariantMap &patch);
    Q_INVOKABLE bool moveEffect(const QString &effectId, int targetIndex);
    Q_INVOKABLE bool removeEffect(const QString &effectId);

    // --- history, persistence, output
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE QString toJson() const;
    Q_INVOKABLE bool fromJson(const QString &json);
    Q_INVOKABLE bool saveToFile(const QString &path);
    Q_INVOKABLE bool loadFromFile(const QString &path);
    // One PDF page per shown slide (all builds complete); with notes, each
    // slide page is followed by its notes page.
    Q_INVOKABLE bool exportPdf(const QString &path, bool includeNotes = false);
    Q_INVOKABLE QImage renderFrame(const QString &slideId, int step, const QString &ticks,
                                   int width, int height) const;
    Q_INVOKABLE QString previewDataUrl(const QString &slideId, int width) const;

    // DocumentModel
    QObject *modelObject() const override { return const_cast<SlideDeck *>(this); }
    DocumentId modelId() const override { return DocumentId::parse(m_documentId); }
    QString modelKind() const override { return QStringLiteral("slides"); }
    SnapshotHeader modelHeader() const override;
    QMetaObject::Connection connectModelChanged(QObject *receiver, std::function<void()> slot) override;
    OperationError prepareCommand(const DocumentCommand &command, bool forward) const override;
    void publishCommand(const DocumentCommand &command, bool forward) override;
    DocumentCommandPtr composeCommands(const DocumentCommand &first,
                                       const DocumentCommand &second) const override;

    static QString schemaId() { return QStringLiteral("qindatk.slides.v1"); }
    // Pure JSON codec shared by the DocumentCodec, toJson and the tests.
    static QByteArray encodeState(const Slides::DeckState &state, const SnapshotHeader &header,
                                  const QJsonObject &extensions);
    static bool decodeState(const QByteArray &bytes, Slides::DeckState *state,
                            SnapshotHeader *header, QJsonObject *extensions, QString *problem);

signals:
    void changed();
    void historyChanged();
    void textEditChanged();
    void errorChanged();

private:
    friend class SlideDeckDocumentCodec;
    struct DeckCommand;
    void historyAttached(UndoService *service) override;
    UndoService *history() const;
    bool commitState(const Slides::DeckState &after, const QString &label);
    bool fail(const QString &message);
    Slides::Slide *slideById(Slides::DeckState &state, const QString &slideId) const;
    // Slide index and object/effect index of an ID in `state`, or -1.
    std::pair<int, int> locateObject(const Slides::DeckState &state, const QString &objectId) const;
    std::pair<int, int> locateEffect(const Slides::DeckState &state, const QString &effectId) const;
    void adoptLoaded(Slides::DeckState state, const SnapshotHeader &header);
    // Sorter thumbnail (PNG data URL, PreviewWidth wide) rendered with the
    // shared painter; reused while the slide, its master and the slide size
    // are unchanged. prunePreviews drops entries of deleted slides, so the
    // cache never outgrows the deck (at most MaximumSlides entries).
    QString cachedPreview(const Slides::Slide &slide) const;
    void prunePreviews() const;
    struct PreviewEntry {
        Slides::Slide slide;
        Slides::Master master;
        QSizeF size;
        QString url;
    };
    mutable QHash<QString, PreviewEntry> m_previews;

    Slides::DeckState m_state;
    QString m_documentId;
    quint64 m_revision = 1;
    quint64 m_resetGeneration = 0;
    UndoService *m_localHistory = nullptr;
    QMetaObject::Connection m_historyConnection;
    QPointer<RichTextDocument> m_textDocument;
    QString m_textObjectId;
    QString m_error;
};

} // namespace QindaTK
