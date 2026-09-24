// SPDX-License-Identifier: LGPL-3.0-or-later
#include "pdf_asset_backend.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QImage>
#include <QImageWriter>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTransform>
#include <algorithm>

namespace QindaTK {
namespace {
constexpr qsizetype MaxOutputBytes = 32 * 1024 * 1024;
constexpr qint64 MaxProcessMilliseconds = 30000;

OperationError failure(ErrorCode code, const QString &message)
{ return {code, message, QStringLiteral("uri"), QStringLiteral("pdf")}; }

QByteArray runPoppler(const QString &tool, const QStringList &arguments,
                      const CancellationToken &cancel, OperationError *error)
{
    const QString executable = QStandardPaths::findExecutable(tool);
    if (executable.isEmpty()) {
        *error = failure(ErrorCode::Unsupported, QStringLiteral("%1 is not installed").arg(tool));
        return {};
    }
    QProcess process;
    process.setProgram(executable);
    process.setArguments(arguments);
    process.start(QIODevice::ReadOnly);
    if (!process.waitForStarted(2000)) {
        *error = failure(ErrorCode::BackendFailure, process.errorString());
        return {};
    }
    QElapsedTimer elapsed;
    elapsed.start();
    QByteArray output;
    QByteArray diagnostics;
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(50);
        output += process.readAllStandardOutput();
        diagnostics += process.readAllStandardError();
        if (output.size() > MaxOutputBytes || elapsed.elapsed() > MaxProcessMilliseconds
            || cancel.isCancelled()) {
            process.kill();
            process.waitForFinished(1000);
            *error = failure(cancel.isCancelled() ? ErrorCode::Cancelled
                : output.size() > MaxOutputBytes ? ErrorCode::Validation
                : ErrorCode::BackendFailure,
                cancel.isCancelled() ? QStringLiteral("PDF request cancelled")
                : output.size() > MaxOutputBytes ? QStringLiteral("PDF page exceeds output limit")
                : QStringLiteral("PDF renderer timed out"));
            return {};
        }
    }
    output += process.readAllStandardOutput();
    diagnostics += process.readAllStandardError();
    if (output.size() > MaxOutputBytes || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        *error = failure(ErrorCode::BackendFailure,
            QStringLiteral("%1 failed: %2").arg(tool, QString::fromUtf8(diagnostics.left(2048)).trimmed()));
        return {};
    }
    return output;
}

AssetResult pdfMetadata(const QString &path, const CancellationToken &cancel)
{
    AssetResult result;
    const QByteArray bytes = runPoppler(QStringLiteral("pdfinfo"),
        {QStringLiteral("-f"), QStringLiteral("1"), QStringLiteral("-l"), QStringLiteral("1"), path},
        cancel, &result.error);
    if (result.error) return result;
    const QString info = QString::fromUtf8(bytes);
    const QRegularExpression pages(QStringLiteral("^Pages:\\s*(\\d+)"), QRegularExpression::MultilineOption);
    const auto match = pages.match(info);
    if (!match.hasMatch() || match.captured(1).toInt() < 1) {
        result.error = failure(ErrorCode::Validation, QStringLiteral("PDF page count is unavailable"));
        return result;
    }
    result.metadata = {{QStringLiteral("kind"), QStringLiteral("metadata")},
        {QStringLiteral("mediaType"), QStringLiteral("pdf")},
        {QStringLiteral("pageCount"), match.captured(1).toInt()}};
    return result;
}
} // namespace

AssetResult fetchPdfAsset(const QString &path, const AssetRequest &request,
                          const CancellationToken &cancel, const JobProgress &progress)
{
    if (request.kind == QStringLiteral("metadata")) return pdfMetadata(path, cancel);
    AssetResult result;
    if (request.pageIndex > 100000) {
        result.error = failure(ErrorCode::InvalidArgument, QStringLiteral("PDF page index is out of range"));
        return result;
    }
    if (progress) progress(0.0);
    const int pageNumber = request.pageIndex + 1;
    const int dimension = std::clamp(std::max(request.size.width(), request.size.height()), 1, 4096);
    QByteArray bytes = runPoppler(QStringLiteral("pdftoppm"),
        {QStringLiteral("-f"), QString::number(pageNumber), QStringLiteral("-l"),
         QString::number(pageNumber), QStringLiteral("-singlefile"), QStringLiteral("-png"),
         QStringLiteral("-scale-to"), QString::number(dimension), path}, cancel, &result.error);
    if (result.error) return result;
    QImage image;
    if (!image.loadFromData(bytes, "PNG") || image.width() < 1 || image.height() < 1) {
        result.error = failure(ErrorCode::Validation, QStringLiteral("PDF renderer returned no page image"));
        return result;
    }
    if (request.rotation != 0) {
        image = image.transformed(QTransform().rotate(request.rotation));
        QBuffer buffer(&bytes);
        bytes.clear();
        if (!buffer.open(QIODevice::WriteOnly) || !QImageWriter(&buffer, "png").write(image)) {
            result.error = failure(ErrorCode::BackendFailure, QStringLiteral("PDF page rotation failed"));
            return result;
        }
    }
    if (cancel.isCancelled()) {
        result.error = failure(ErrorCode::Cancelled, QStringLiteral("PDF request cancelled"));
        return result;
    }
    result.bytes = std::move(bytes);
    result.mimeType = QStringLiteral("image/png");
    result.metadata = {{QStringLiteral("kind"), QStringLiteral("pdf-page")},
        {QStringLiteral("pageIndex"), request.pageIndex},
        {QStringLiteral("rotation"), request.rotation},
        {QStringLiteral("width"), image.width()}, {QStringLiteral("height"), image.height()}};
    if (progress) progress(1.0);
    return result;
}

} // namespace QindaTK
