// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-088 qualification consumer. Built only from an installed QindaTK prefix,
// it imports QindaTK from that prefix (found relative to this executable),
// then asks each optional engine whether it is present and prints one JSON
// report. An omitted engine must answer "unavailable" with a reason instead
// of failing; tools/qualify compares the report with the build's switches.
//
// Exit status: 0 report complete; 2 the QML scene did not load;
// 3 an engine was unavailable without a reason.
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QQmlApplicationEngine>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>

#include <qindatk/audio_engine.h>
#include <qindatk/file_dialog_adapter.h>
#include <qindatk/media_decoder.h>
#include <qindatk/pdf_annotation_document.h>
#include <qindatk/print_adapter.h>
#include <qindatk/volume_raycast.h>
#include <qindatk/volume_render_node.h>
#include <qindatk/voxel_volume.h>

#if defined(QUALIFY_HAVE_WIDGETS)
#include <QApplication>
#include <qindatk/native_file_dialog_backend.h>
#if defined(QINDATK_HAVE_NATIVE_PRINT)
#include <qindatk/native_print_backend.h>
#endif
using QualifyApplication = QApplication;
#else
#include <QGuiApplication>
using QualifyApplication = QGuiApplication;
#endif

namespace {

QJsonObject entry(bool available, const QString &reason, const QJsonValue &detail = {})
{
    QJsonObject object{{QStringLiteral("available"), available},
                       {QStringLiteral("reason"), available ? QString() : reason}};
    if (!detail.isUndefined() && !detail.isNull())
        object.insert(QStringLiteral("detail"), detail);
    return object;
}

QJsonObject mediaCapability(const QString &scratch)
{
    using QindaTK::MediaDecoder;
    if (MediaDecoder::available())
        return entry(true, {}, MediaDecoder::backendVersion());
    MediaDecoder decoder;
    const QindaTK::OperationError error = decoder.open(scratch + QStringLiteral("/missing.mp4"));
    return entry(false, error.message);
}

QJsonObject audioCapability()
{
    QindaTK::AudioEngine engine;
    for (const QVariant &backend : engine.backends()) {
        const QVariantMap map = backend.toMap();
        if (map.value(QStringLiteral("id")) == QStringLiteral("alsa"))
            return entry(map.value(QStringLiteral("available")).toBool(),
                         map.value(QStringLiteral("reason")).toString());
    }
    return entry(false, {});
}

QJsonObject pdfCapability(const QString &scratch)
{
    const QString path = scratch + QStringLiteral("/page.pdf");
    {
        QPdfWriter writer(path);
        writer.setPageSize(QPageSize(QPageSize::A6));
        QPainter painter(&writer);
        painter.drawText(QPointF(20, 40), QStringLiteral("QindaTK qualification"));
    }
    QindaTK::PdfAnnotationDocument document;
    const bool opened = document.open(path);
    const QStringList kinds =
        document.capabilities().value(QStringLiteral("annotationKinds")).toStringList();
    const QString reason = document.errorString().isEmpty()
        ? QStringLiteral("The document accepts no annotations") : document.errorString();
    return entry(opened && !kinds.isEmpty(), reason, QJsonValue::fromVariant(kinds));
}

QJsonObject volumeShaderCapability()
{
    QindaTK::VoxelVolume volume;
    volume.configure({{QStringLiteral("sizeX"), 4}, {QStringLiteral("sizeY"), 4},
                      {QStringLiteral("sizeZ"), 4}});
    // Every other probe input is set as a capable GPU would report it, so the
    // toolkit's sentence names the build-time shader boundary only.
    QindaTK::VolumeGpuProbe probe;
    probe.rhi = true;
    probe.shaders = QindaTK::VolumeRenderNode::shadersAvailable();
    probe.threeDimensionalTextures = true;
    probe.formatSupported = true;
    probe.maximumTextureSide = 2048;
    const QString reason =
        QindaTK::VolumeRaycaster::gpuUnavailableReason(&volume, probe, qint64(1) << 30, false);
    return entry(reason.isEmpty(), reason);
}

QJsonObject sceneCapability(QObject *root)
{
    // The renderer QML files are compiled into libqindatk's resources only
    // when Qt Quick 3D was found (src/CMakeLists.txt); SceneView3D itself
    // always instantiates and describes its fallback.
    const bool built = QFile::exists(QStringLiteral(":/qt/qml/QindaTK/SceneView3DRenderer.qml"));
    return entry(built,
                 QStringLiteral("QindaTK was built without Qt Quick 3D; SceneView3D shows its fallback"),
                 root->property("sceneDescription").toString());
}

QJsonObject dialogCapability()
{
    QindaTK::FileDialogAdapter adapter;
#if defined(QUALIFY_HAVE_WIDGETS)
    adapter.setBackend(std::make_shared<QindaTK::NativeFileDialogBackend>());
#endif
    if (adapter.available())
        return entry(true, {}, QJsonObject::fromVariantMap(adapter.capabilities()));
    // No dialog can open here: an unavailable adapter answers synchronously.
    adapter.request(QStringLiteral("open"));
    return entry(false, adapter.lastError());
}

QJsonObject printCapability()
{
#if defined(QINDATK_HAVE_NATIVE_PRINT)
    QindaTK::PrintAdapter::registerDefaultBackend(std::make_shared<QindaTK::NativePrintBackend>());
#endif
    QindaTK::PrintAdapter adapter;
    QJsonArray destinations;
    bool native = false;
    for (const QVariant &value : adapter.destinations()) {
        const QVariantMap destination = value.toMap();
        const QString id = destination.value(QStringLiteral("printerId")).toString();
        destinations.append(id);
        native = native || id == QStringLiteral("print-to-file")
            || id.startsWith(QStringLiteral("printer:"));
    }
    return entry(native,
                 QStringLiteral("QindaTK was installed without the native print backend (Qt PrintSupport); PDF output remains"),
                 destinations);
}

} // namespace

int main(int argc, char **argv)
{
    QualifyApplication app(argc, argv);
    const QString importRoot = QDir::cleanPath(
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral(QUALIFY_BIN_TO_QML)));

    QJsonObject report{{QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
                       {QStringLiteral("platform"), QGuiApplication::platformName()},
                       {QStringLiteral("importRoot"), importRoot}};
    QQmlApplicationEngine engine;
    engine.addImportPath(importRoot);
    engine.load(QUrl(QStringLiteral("qrc:/qualify/Main.qml")));
    QTextStream out(stdout);
    if (engine.rootObjects().size() != 1) {
        report.insert(QStringLiteral("qmlLoaded"), false);
        out << QJsonDocument(report).toJson(QJsonDocument::Indented);
        return 2;
    }
    QObject *root = engine.rootObjects().constFirst();
    report.insert(QStringLiteral("qmlLoaded"), root->property("buttonText").toString() == QStringLiteral("Qualify"));

    QTemporaryDir scratch;
    const QJsonObject capabilities{
        {QStringLiteral("media.ffmpeg"), mediaCapability(scratch.path())},
        {QStringLiteral("audio.alsa"), audioCapability()},
        {QStringLiteral("pdf.poppler"), pdfCapability(scratch.path())},
        {QStringLiteral("volume.gpuShaders"), volumeShaderCapability()},
        {QStringLiteral("scene3d.quick3d"), sceneCapability(root)},
        {QStringLiteral("dialogs.native"), dialogCapability()},
        {QStringLiteral("print.native"), printCapability()},
    };
    report.insert(QStringLiteral("capabilities"), capabilities);
    out << QJsonDocument(report).toJson(QJsonDocument::Indented);
    out.flush();

    if (!report.value(QStringLiteral("qmlLoaded")).toBool())
        return 2;
    for (auto it = capabilities.begin(); it != capabilities.end(); ++it) {
        const QJsonObject capability = it.value().toObject();
        if (!capability.value(QStringLiteral("available")).toBool()
            && capability.value(QStringLiteral("reason")).toString().isEmpty()) {
            return 3;
        }
    }
    return 0;
}
