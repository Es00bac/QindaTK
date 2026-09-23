// SPDX-License-Identifier: LGPL-3.0-or-later
#include "document_resource_links.h"

#include <QSet>
#include <QThread>

namespace QindaTK {
namespace {
OperationError error(ErrorCode code, const QString &message)
{
    return {code, message, {}, {}};
}

bool validHash(const QByteArray &value)
{
    if (value.isEmpty()) return true;
    if (value.size() != 64) return false;
    for (const char c : value) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
              || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}
}

DocumentResourceLinks::DocumentResourceLinks(QObject *parent) : QObject(parent) {}

DocumentResourceLinks::~DocumentResourceLinks()
{
    if (m_resolver) {
        for (const Link &link : m_links)
            m_resolver->removeResource(link.resourceId);
    }
}

QVariantList DocumentResourceLinks::manifest() const
{
    QVariantList result;
    result.reserve(m_links.size());
    for (const Link &link : m_links) {
        result.append(QVariantMap{{QStringLiteral("resourceId"), link.resourceId},
                                  {QStringLiteral("entityId"), link.entityId.toString()},
                                  {QStringLiteral("uriPropertyId"), link.uriPropertyId},
                                  {QStringLiteral("expectedSha256"),
                                   QString::fromLatin1(link.expectedSha256)}});
    }
    return result;
}

void DocumentResourceLinks::setDocument(PropertyDocument *document)
{
    Q_ASSERT(thread() == QThread::currentThread());
    if (m_document == document) return;
    // AGENT-CONTRACT: This adapter owns its resolver registrations for one
    // document binding. Retire them at switch/close so an old document cannot
    // leave URI mappings behind for a later session using the same resolver.
    if (m_resolver) {
        for (const Link &link : m_links)
            m_resolver->removeResource(link.resourceId);
    }
    if (m_document) disconnect(m_document, nullptr, this, nullptr);
    m_document = document;
    if (document) {
        connect(document, &PropertyDocument::propertyChanged, this,
                &DocumentResourceLinks::onPropertyChanged);
        connect(document, &QObject::destroyed, this, [this] {
            m_document = nullptr;
            if (m_resolver) {
                for (const Link &link : m_links)
                    m_resolver->removeResource(link.resourceId);
            }
            emit changed();
        });
    }
    synchronizeResolver();
    emit changed();
}

void DocumentResourceLinks::setUndoService(UndoService *undo)
{
    if (m_undo == undo) return;
    m_undo = undo;
    emit changed();
}

void DocumentResourceLinks::setResolver(ResourceResolver *resolver)
{
    if (m_resolver == resolver) return;
    if (m_resolver) {
        for (const Link &link : m_links)
            m_resolver->removeResource(link.resourceId);
    }
    m_resolver = resolver;
    synchronizeResolver();
    emit changed();
}

void DocumentResourceLinks::setManifest(const QVariantList &rows)
{
    Q_ASSERT(thread() == QThread::currentThread());
    QVector<Link> candidate;
    QSet<QString> ids;
    QSet<QString> entities;
    candidate.reserve(rows.size());
    for (const QVariant &rowValue : rows) {
        const QVariantMap row = rowValue.toMap();
        const QString id = row.value(QStringLiteral("resourceId")).toString().trimmed();
        const EntityId entity = EntityId::parse(row.value(QStringLiteral("entityId")).toString());
        const QString uriProperty = row.value(QStringLiteral("uriPropertyId"),
                                               QStringLiteral("uri")).toString();
        const QByteArray hash = row.value(QStringLiteral("expectedSha256")).toString()
                                    .toLatin1().toLower();
        if (id.isEmpty() || id.size() > 256 || id.contains(QChar::Null)
            || !entity.isValid() || uriProperty.trimmed().isEmpty()
            || !validHash(hash) || ids.contains(id) || entities.contains(entity.toString())) {
            fail(ErrorCode::InvalidArgument,
                 QStringLiteral("Manifest rows need unique resource and entity IDs, a URI property, and a valid SHA-256."));
            return;
        }
        ids.insert(id);
        entities.insert(entity.toString());
        candidate.append({id, entity, uriProperty, hash});
    }
    if (m_document) {
        for (const Link &link : candidate) {
            const auto descriptor = m_document->descriptor(link.uriPropertyId);
            if (!m_document->contains(link.entityId) || !descriptor
                || descriptor->type != QMetaType::fromType<QString>()) {
                fail(ErrorCode::InvalidArgument,
                     QStringLiteral("A manifest resource entity or string URI property does not exist."));
                return;
            }
        }
    }
    // A manifest replacement retires mappings this adapter no longer owns,
    // and re-registers a logical ID if its expected-content baseline changed.
    if (m_resolver) {
        for (const Link &old : m_links) {
            bool retained = false;
            for (const Link &next : candidate) {
                if (next.resourceId == old.resourceId
                    && next.expectedSha256 == old.expectedSha256) {
                    retained = true;
                    break;
                }
            }
            if (!retained) m_resolver->removeResource(old.resourceId);
        }
    }
    m_links = std::move(candidate);
    m_lastError.clear();
    synchronizeResolver();
    emit changed();
}

bool DocumentResourceLinks::synchronizeResolver()
{
    if (!m_document || !m_resolver) return true;
    for (const Link &link : m_links) {
        if (!m_document->contains(link.entityId))
            return fail(ErrorCode::Unavailable,
                        QStringLiteral("A manifest resource entity is no longer present."));
        const QString uri = m_document->committedValue(link.entityId, link.uriPropertyId).toString();
        const QVariantMap current = m_resolver->lookup(link.resourceId);
        if (current.isEmpty()) {
            if (!m_resolver->registerResource(link.resourceId, uri, link.expectedSha256))
                return fail(ErrorCode::Validation, m_resolver->lastError());
        } else {
            const QByteArray expected = current.value(QStringLiteral("expectedSha256")).toString().toLatin1();
            if (expected != link.expectedSha256)
                return fail(ErrorCode::Conflict,
                            QStringLiteral("Resolver and document manifest disagree on expected content hash."));
            if (!m_resolver->restoreMapping(link.resourceId, uri))
                return fail(ErrorCode::Validation, m_resolver->lastError());
        }
    }
    return true;
}

bool DocumentResourceLinks::synchronizeOne(const Link &link)
{
    if (!m_document || !m_resolver || !m_document->contains(link.entityId)) return false;
    const QString uri = m_document->committedValue(link.entityId, link.uriPropertyId).toString();
    if (!m_resolver->restoreMapping(link.resourceId, uri))
        return fail(ErrorCode::Validation, m_resolver->lastError());
    return true;
}

void DocumentResourceLinks::onPropertyChanged(const QString &entityId,
                                               const QString &propertyId)
{
    for (const Link &link : m_links) {
        if (link.entityId.toString() == entityId && link.uriPropertyId == propertyId) {
            synchronizeOne(link);
            emit changed();
            return;
        }
    }
}

const DocumentResourceLinks::Link *DocumentResourceLinks::find(const QString &resourceId) const
{
    for (const Link &link : m_links)
        if (link.resourceId == resourceId) return &link;
    return nullptr;
}

QVariantMap DocumentResourceLinks::resource(const QString &resourceId) const
{
    return m_resolver ? m_resolver->lookup(resourceId) : QVariantMap{};
}

bool DocumentResourceLinks::relink(const QString &resourceId, const QString &newUri,
                                   const QString &expectedRevision)
{
    Q_ASSERT(thread() == QThread::currentThread());
    const Link *link = find(resourceId);
    if (!link || !m_document || !m_undo || !m_resolver)
        return fail(ErrorCode::Unavailable,
                    QStringLiteral("Resource link services or manifest entry are unavailable."));
    bool revisionOk = false;
    const quint64 requestedRevision = expectedRevision.toULongLong(&revisionOk);
    if (!revisionOk || requestedRevision != m_document->revision().value)
        return fail(ErrorCode::StaleRevision, QStringLiteral("The document changed before relink."));
    if (!m_document->contains(link->entityId))
        return fail(ErrorCode::StaleGeneration, QStringLiteral("The resource entity no longer exists."));
    const QString oldUri = m_document->committedValue(link->entityId, link->uriPropertyId).toString();
    if (oldUri == newUri) {
        m_lastError.clear();
        return true;
    }

    EditTransaction edit;
    edit.setDocument(m_document);
    edit.setUndoService(m_undo);
    if (!edit.begin({link->entityId}, link->uriPropertyId,
                    QStringLiteral("Relink resource")))
        return fail(edit.error().code, edit.error().message);
    if (auto error = edit.updateValue(newUri))
        return fail(error.code, error.message);

    // Validate the new external file through the resolver. This temporarily
    // updates its runtime cache; failure to commit restores the document URI.
    if (!m_resolver->relink(resourceId, newUri)) {
        edit.cancel();
        return fail(ErrorCode::Validation, m_resolver->lastError());
    }
    if (m_document->revision().value != requestedRevision) {
        edit.cancel();
        m_resolver->restoreMapping(resourceId, oldUri);
        return fail(ErrorCode::StaleRevision, QStringLiteral("The document changed during relink validation."));
    }
    if (auto error = edit.commitEdit()) {
        m_resolver->restoreMapping(resourceId, oldUri);
        return fail(error.code, error.message);
    }
    m_lastError.clear();
    emit changed();
    emit relinked(resourceId, newUri);
    return true;
}

bool DocumentResourceLinks::fail(ErrorCode code, const QString &message)
{
    m_lastError = message;
    emit changed();
    emit rejected(message, static_cast<int>(code));
    return false;
}

} // namespace QindaTK
