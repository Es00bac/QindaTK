// SPDX-License-Identifier: LGPL-3.0-or-later
#include "demo_host.h"
#include "../../src/core/caption_track_model.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QUrl>

namespace {
QString digest(const QByteArray &bytes)
{ return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()); }

bool sourcesReady(const QVariantMap &snapshot)
{
    const QVariantList frames = snapshot.value(QStringLiteral("settings")).toMap()
        .value(QStringLiteral("frames")).toList();
    if (frames.isEmpty()) return false;
    for (const QVariant &value : frames) {
        const QVariantMap frame = value.toMap();
        QFile file(frame.value(QStringLiteral("path")).toString());
        if (!file.open(QIODevice::ReadOnly)
            || digest(file.readAll()) != frame.value(QStringLiteral("sha256")).toString()) return false;
    }
    return true;
}
}

DemoHost::DemoHost(const QString &root, QObject *parent)
    : QObject(parent), m_root(root), m_outputPath(QDir(root).filePath(QStringLiteral("caption-output")))
{
    connect(&m_queue, &QindaTK::RenderQueueModel::changed, this, [this]() {
        if (m_queue.rowCount() > 0) {
            const QString current = state();
            if (!current.isEmpty() && (m_history.isEmpty() || m_history.constLast() != current))
                m_history.append(current);
        }
        emit changed();
    });
    if (!createSources() || !m_queue.open(QDir(root).filePath(QStringLiteral("queue.json")))) {
        if (m_error.isEmpty()) m_error = m_queue.lastError();
        emit changed();
        return;
    }
    m_ready = true;
    if (m_queue.rowCount() > 0 && (m_history.isEmpty() || m_history.constLast() != state()))
        m_history.append(state());
}

bool DemoHost::createSources()
{
    const QString sourceDir = QDir(m_root).filePath(QStringLiteral("sources"));
    if (!QDir().mkpath(sourceDir)) { m_error = QStringLiteral("Cannot create source directory"); return false; }
    QVariantList frames;
    QByteArray identity;
    const QStringList times{QStringLiteral("250"), QStringLiteral("750"), QStringLiteral("1250")};
    for (int i = 0; i < times.size(); ++i) {
        QImage image(640, 360, QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(22 + i * 8, 55 + i * 5, 74));
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(43, 132, 126));
        painter.drawEllipse(QRectF(55 + i * 40, 100, 235, 210));
        painter.setBrush(QColor(186, 125, 83));
        painter.drawRoundedRect(QRectF(390 - i * 20, 100, 185, 175), 18, 18);
        painter.end();
        const QString path = QDir(sourceDir).filePath(QStringLiteral("source-%1.png").arg(i));
        if (!image.save(path, "PNG")) { m_error = QStringLiteral("Cannot save source PNG"); return false; }
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) { m_error = file.errorString(); return false; }
        const QString hash = digest(file.readAll());
        identity += hash.toLatin1();
        frames.append(QVariantMap{{QStringLiteral("path"), path},
            {QStringLiteral("sha256"), hash}, {QStringLiteral("timeMs"), times.at(i)}});
    }
    CaptionTrackModel track;
    if (!track.addCue(QStringLiteral("100"), QStringLiteral("1100"),
                      QStringLiteral("A reproducible caption sequence"))) {
        m_error = track.error(); return false;
    }
    const QString trackJson = track.toJson();
    identity += trackJson.toUtf8();
    m_snapshot = {{QStringLiteral("id"), QStringLiteral("demo-caption")},
        {QStringLiteral("label"), QStringLiteral("Captioned opening shot")},
        {QStringLiteral("providerId"), QStringLiteral("qindatk.caption-png-sequence")},
        {QStringLiteral("providerVersion"), QStringLiteral("1")},
        {QStringLiteral("format"), QStringLiteral("png-sequence")},
        {QStringLiteral("capabilityId"), QStringLiteral("caption-burn-in")},
        {QStringLiteral("sourceId"), QStringLiteral("opening-shot")},
        {QStringLiteral("sourceRevision"), digest(identity)},
        {QStringLiteral("sourceRange"), QStringLiteral("250–1250 ms")},
        {QStringLiteral("outputPath"), m_outputPath},
        {QStringLiteral("settings"), QVariantMap{{QStringLiteral("captionTrackJson"), trackJson},
            {QStringLiteral("frames"), frames}}}};
    return true;
}

QVariantList DemoHost::jobs() const
{
    if (m_queue.rowCount() > 0) return m_queue.jobs();
    return {QVariantMap{{QStringLiteral("jobId"), QStringLiteral("demo-caption")},
        {QStringLiteral("label"), m_snapshot.value(QStringLiteral("label"))},
        {QStringLiteral("state"), m_draftState}, {QStringLiteral("progress"), 0},
        {QStringLiteral("sourceSnapshot"), m_snapshot.value(QStringLiteral("sourceId"))},
        {QStringLiteral("sourceVersion"), m_snapshot.value(QStringLiteral("sourceRevision"))},
        {QStringLiteral("sourceRange"), m_snapshot.value(QStringLiteral("sourceRange"))},
        {QStringLiteral("outputFormat"), m_snapshot.value(QStringLiteral("format"))},
        {QStringLiteral("outputPath"), m_outputPath}, {QStringLiteral("error"), m_error}}};
}

QUrl DemoHost::previewUrl() const
{
    return state() == QLatin1String("succeeded")
        ? QUrl::fromLocalFile(QDir(m_outputPath).filePath(QStringLiteral("frame-000000.png"))) : QUrl();
}

QString DemoHost::state() const
{
    if (m_queue.rowCount() == 0) return m_draftState;
    return m_queue.job(QStringLiteral("demo-caption")).value(QStringLiteral("state")).toString();
}

bool DemoHost::validate(const QString &id)
{
    if (!m_ready || id != QLatin1String("demo-caption") || m_queue.rowCount() != 0) return false;
    if (QFileInfo::exists(m_outputPath)) {
        m_error = QStringLiteral("Output target is already occupied"); emit changed(); return false;
    }
    if (!sourcesReady(m_snapshot)) {
        m_error = QStringLiteral("Source frame changed before validation"); emit changed(); return false;
    }
    m_draftState = QStringLiteral("validated"); m_error.clear(); emit changed(); return true;
}

bool DemoHost::submit(const QString &id)
{
    if (!m_ready || id != QLatin1String("demo-caption") || m_draftState != QLatin1String("validated")) return false;
    if (m_queue.enqueueJob(m_snapshot) != id) {
        m_error = m_queue.lastError(); emit changed(); return false;
    }
    return true;
}

bool DemoHost::cancel(const QString &id) { return m_queue.cancelJob(id); }
bool DemoHost::retry(const QString &id)
{
    if (!sourcesReady(m_queue.job(id).value(QStringLiteral("snapshot")).toMap())) {
        m_error = QStringLiteral("Source frame changed before retry"); emit changed(); return false;
    }
    return m_queue.retryJob(id);
}

bool DemoHost::openOutput(const QString &id, const QVariantMap &manifest)
{
    if (id != QLatin1String("demo-caption") || !manifest.value(QStringLiteral("validated")).toBool()
        || state() != QLatin1String("succeeded") || !m_queue.verifyOutput(id)) {
        m_error = m_queue.lastError().isEmpty() ? QStringLiteral("Output is not verified") : m_queue.lastError();
        emit changed(); return false;
    }
    return QDesktopServices::openUrl(QUrl::fromLocalFile(m_outputPath));
}
