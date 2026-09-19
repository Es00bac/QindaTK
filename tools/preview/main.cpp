// SPDX-License-Identifier: LGPL-3.0-or-later
//
// qtk-preview: render a QML file with the QindaTK module and either show
// it, grab it to a PNG, or dump the item tree as text/JSON.
//
// AGENT-CONTRACT: this is the verification tool for humans and agents
// alike. `--dump` prints every item with type, objectName, geometry and
// text, so a layout can be checked without a screen; `--grab` writes a
// PNG through the software renderer so it works on a headless machine.
// A QML error exits with status 1 and the error text on stderr.

#include "density.h"
#include "qst_support.h"
#include "theme.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTextStream>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace {

QString cleanTypeName(const QObject *object)
{
    QString name = QString::fromLatin1(object->metaObject()->className());
    const qsizetype qmlType = name.indexOf(QLatin1String("_QMLTYPE_"));
    if (qmlType >= 0) {
        name.truncate(qmlType);
    }
    const qsizetype qmlComponent = name.indexOf(QLatin1String("_QML_"));
    if (qmlComponent >= 0) {
        name.truncate(qmlComponent);
    }
    if (name.startsWith(QLatin1String("QindaTK::"))) {
        name.remove(0, 9);
    } else if (name.startsWith(QLatin1String("QQuick"))) {
        name.remove(0, 6);
    }
    return name;
}

QString textOf(const QObject *object)
{
    const QVariant text = object->property("text");
    if (!text.isValid() || text.typeId() != QMetaType::QString) {
        return {};
    }
    QString value = text.toString().simplified();
    if (value.size() > 48) {
        value = value.left(45) + QStringLiteral("...");
    }
    return value;
}

void dumpText(QTextStream &out, const QQuickItem *item, int depth)
{
    const QString indent(depth * 2, QLatin1Char(' '));
    out << indent << cleanTypeName(item);
    if (!item->objectName().isEmpty()) {
        out << '#' << item->objectName();
    }
    out << "  " << qRound(item->x()) << ',' << qRound(item->y()) << ' ' << qRound(item->width()) << 'x'
        << qRound(item->height());
    if (!item->isVisible()) {
        out << "  hidden";
    }
    const QString text = textOf(item);
    if (!text.isEmpty()) {
        out << "  \"" << text << '"';
    }
    out << '\n';
    for (const QQuickItem *child : item->childItems()) {
        dumpText(out, child, depth + 1);
    }
}

QJsonObject dumpJson(const QQuickItem *item)
{
    QJsonObject node;
    node.insert(QStringLiteral("type"), cleanTypeName(item));
    if (!item->objectName().isEmpty()) {
        node.insert(QStringLiteral("objectName"), item->objectName());
    }
    node.insert(QStringLiteral("x"), item->x());
    node.insert(QStringLiteral("y"), item->y());
    node.insert(QStringLiteral("width"), item->width());
    node.insert(QStringLiteral("height"), item->height());
    node.insert(QStringLiteral("implicitWidth"), item->implicitWidth());
    node.insert(QStringLiteral("implicitHeight"), item->implicitHeight());
    node.insert(QStringLiteral("visible"), item->isVisible());
    const QString text = textOf(item);
    if (!text.isEmpty()) {
        node.insert(QStringLiteral("text"), text);
    }
    QJsonArray children;
    for (const QQuickItem *child : item->childItems()) {
        children.append(dumpJson(child));
    }
    if (!children.isEmpty()) {
        node.insert(QStringLiteral("children"), children);
    }
    return node;
}

// AGENT-NOTE: --grab normally renders through Qt's software adaptation so it
// works on a headless machine. That adaptation draws none of the custom
// scene-graph nodes a GPU-only control builds (Graph, Meter, Sparkline), so
// --gpu keeps the offscreen platform but lets the real RHI backend run: it is
// the only way to verify what those controls actually put on screen.
bool wantsGpu(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        if (QByteArray(argv[i]) == "--gpu") {
            return true;
        }
    }
    return false;
}

bool wantsHeadless(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        const QByteArray arg(argv[i]);
        if (arg == "--grab" || arg == "--dump" || arg == "--dump-json" || arg == "--offscreen"
            || arg == "--check" || arg == "--dump-theme" || arg == "--list-icons") {
            return true;
        }
        if (arg == "--stay" || arg == "--show") {
            return false;
        }
    }
    return false;
}

} // namespace

int main(int argc, char **argv)
{
    if (wantsHeadless(argc, argv)) {
        if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
        }
        if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND")) {
            // AGENT-GUARD: the offscreen platform advertises no OpenGL, so
            // QtQuick picks the software adaptation on its own -- leaving the
            // variable unset is NOT enough to get the GPU path. --gpu has to
            // name the RHI adaptation explicitly.
            qputenv("QT_QUICK_BACKEND", wantsGpu(argc, argv) ? "rhi" : "software");
        }
    }
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("qtk-preview"));
    QGuiApplication::setOrganizationName(QStringLiteral("QindaTK"));
    QGuiApplication::setApplicationVersion(QStringLiteral(QINDATK_VERSION));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Render a QML file with QindaTK: show it, grab a PNG, or dump the item tree."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file.qml"), QStringLiteral("QML file to load"));
    parser.addOption({QStringLiteral("size"), QStringLiteral("Window size WxH (default: the root's implicit size, or 1280x800)"), QStringLiteral("WxH")});
    parser.addOption({QStringLiteral("grab"), QStringLiteral("Write a PNG of the rendered window"), QStringLiteral("out.png")});
    parser.addOption({QStringLiteral("dump"), QStringLiteral("Print the item tree (type#objectName x,y WxH \"text\")")});
    parser.addOption({QStringLiteral("dump-json"), QStringLiteral("Print the item tree as JSON")});
    parser.addOption({QStringLiteral("dump-theme"), QStringLiteral("Print the resolved theme as JSON and exit")});
    parser.addOption({QStringLiteral("list-icons"), QStringLiteral("Print the built-in icon names and exit")});
    parser.addOption({QStringLiteral("check"), QStringLiteral("Load only; exit 0 when the file instantiates without errors")});
    parser.addOption({QStringLiteral("theme"), QStringLiteral("Theme preset or .json theme file"), QStringLiteral("id|file")});
    parser.addOption({QStringLiteral("density"), QStringLiteral("compact | comfortable | touch"), QStringLiteral("mode")});
    parser.addOption({QStringLiteral("qst"), QStringLiteral("Publish a QindaQt desktop theme (id or .json) through QindaQt.Tokens and the QindaTK.QindaQt bridge"), QStringLiteral("theme")});
    parser.addOption({QStringLiteral("list-qst"), QStringLiteral("Print the installed QindaQt desktop theme ids and exit")});
    parser.addOption({QStringLiteral("wait"), QStringLiteral("Milliseconds to settle before grab/dump (default 120)"), QStringLiteral("ms"), QStringLiteral("120")});
    parser.addOption({QStringLiteral("stay"), QStringLiteral("Keep the window open after grab/dump")});
    parser.addOption({QStringLiteral("show"), QStringLiteral("Show on screen (default when no headless option is given)")});
    parser.addOption({QStringLiteral("offscreen"), QStringLiteral("Force the offscreen platform")});
    parser.addOption({QStringLiteral("gpu"), QStringLiteral("Render through the real GPU backend instead of the software adaptation (needed to grab scene-graph controls such as Graph)")});
    parser.addOption({{QStringLiteral("I"), QStringLiteral("import")}, QStringLiteral("Extra QML import path"), QStringLiteral("dir")});
    parser.process(app);

    QindaTK::Theme *theme = QindaTK::Theme::instance();
    if (parser.isSet(QStringLiteral("theme"))) {
        const QString value = parser.value(QStringLiteral("theme"));
        const bool ok = value.endsWith(QLatin1String(".json")) ? theme->loadFile(value) : theme->applyPreset(value);
        if (!ok) {
            fprintf(stderr, "qtk-preview: unknown theme \"%s\" (presets: %s)\n", qPrintable(value),
                    qPrintable(theme->presets().join(QLatin1String(", "))));
            return 2;
        }
    }
    if (parser.isSet(QStringLiteral("density"))) {
        QindaTK::Density::instance()->setModeName(parser.value(QStringLiteral("density")));
    }
    QTextStream out(stdout);
    if (parser.isSet(QStringLiteral("dump-theme"))) {
        out << QJsonDocument(QJsonObject::fromVariantMap(theme->toMap())).toJson(QJsonDocument::Indented);
        return 0;
    }
    if (parser.isSet(QStringLiteral("list-qst"))) {
        if (!QindaTK::PreviewQst::available()) {
            fprintf(stderr, "qtk-preview: built without the QindaQt desktop libraries\n");
            return 2;
        }
        for (const QString &id : QindaTK::PreviewQst::themeIds()) {
            out << id << '\n';
        }
        return 0;
    }
    if (parser.isSet(QStringLiteral("list-icons"))) {
        QQmlEngine probe;
        QObject *icons = probe.singletonInstance<QObject *>(QStringLiteral("QindaTK"), QStringLiteral("Icons"));
        QStringList names;
        if (icons != nullptr) {
            QMetaObject::invokeMethod(icons, "names", Q_RETURN_ARG(QStringList, names));
        }
        for (const QString &name : names) {
            out << name << '\n';
        }
        return 0;
    }

    const QStringList files = parser.positionalArguments();
    if (files.isEmpty()) {
        fprintf(stderr, "qtk-preview: a QML file is required\n");
        return 2;
    }
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral(QINDATK_BUILD_QML_DIR));
    for (const QString &dir : parser.values(QStringLiteral("import"))) {
        engine.addImportPath(dir);
    }
    const QFileInfo info(files.first());
    engine.addImportPath(info.absolutePath());
    std::unique_ptr<QObject> bridge;
    if (parser.isSet(QStringLiteral("qst"))) {
        QString error;
        const double textScale = QindaTK::Density::instance()->mode() == QindaTK::Density::Compact ? 1.0 : 1.15;
        if (!QindaTK::PreviewQst::publish(engine, parser.value(QStringLiteral("qst")), textScale, &error)) {
            fprintf(stderr, "qtk-preview: --qst failed: %s\n", qPrintable(error));
            return 2;
        }
        // The same bridge an application instantiates; it adopts the tokens
        // just published and follows later republishes.
        QQmlComponent bridgeComponent(&engine);
        bridgeComponent.setData("import QtQuick\nimport QindaTK.QindaQt\nQindaQtTheme {}\n",
                                QUrl(QStringLiteral("inline:qtk-preview-bridge.qml")));
        bridge.reset(bridgeComponent.create());
        if (!bridge) {
            fprintf(stderr, "qtk-preview: cannot load QindaTK.QindaQt: %s\n", qPrintable(bridgeComponent.errorString()));
            return 2;
        }
        out << "qst: published " << parser.value(QStringLiteral("qst")) << ", QindaTK theme is now \""
            << theme->name() << "\" (" << theme->preset() << ")\n";
    }
    QQmlComponent component(&engine, QUrl::fromLocalFile(info.absoluteFilePath()));
    if (component.isError()) {
        for (const QQmlError &error : component.errors()) {
            fprintf(stderr, "%s\n", qPrintable(error.toString()));
        }
        return 1;
    }
    QObject *root = component.create();
    if (root == nullptr || component.isError()) {
        for (const QQmlError &error : component.errors()) {
            fprintf(stderr, "%s\n", qPrintable(error.toString()));
        }
        return 1;
    }
    QSize size(1280, 800);
    if (parser.isSet(QStringLiteral("size"))) {
        const QStringList parts = parser.value(QStringLiteral("size")).split(QLatin1Char('x'));
        if (parts.size() == 2) {
            size = QSize(parts[0].toInt(), parts[1].toInt());
        }
    }
    QQuickWindow *window = qobject_cast<QQuickWindow *>(root);
    QQuickItem *rootItem = nullptr;
    if (window != nullptr) {
        rootItem = window->contentItem();
        if (parser.isSet(QStringLiteral("size"))) {
            window->resize(size);
        }
    } else {
        rootItem = qobject_cast<QQuickItem *>(root);
        if (rootItem == nullptr) {
            fprintf(stderr, "qtk-preview: the root object is neither a Window nor an Item\n");
            return 1;
        }
        window = new QQuickWindow();
        window->setColor(theme->color()->bg());
        window->setTitle(QStringLiteral("qtk-preview: ") + info.fileName());
        if (!parser.isSet(QStringLiteral("size"))) {
            // The root's own size wins: its implicit size, else an explicit
            // width/height it declares, else the 1280x800 default.
            if (rootItem->implicitWidth() > 0 && rootItem->implicitHeight() > 0) {
                size = QSize(qRound(rootItem->implicitWidth()), qRound(rootItem->implicitHeight()));
            } else if (rootItem->width() > 0 && rootItem->height() > 0) {
                size = QSize(qRound(rootItem->width()), qRound(rootItem->height()));
            }
        }
        window->resize(size);
        rootItem->setParentItem(window->contentItem());
        rootItem->setSize(QSizeF(size));
        QObject::connect(window, &QQuickWindow::widthChanged, rootItem, [rootItem, window]() { rootItem->setWidth(window->width()); });
        QObject::connect(window, &QQuickWindow::heightChanged, rootItem, [rootItem, window]() { rootItem->setHeight(window->height()); });
    }
    window->show();

    const bool headless = parser.isSet(QStringLiteral("grab")) || parser.isSet(QStringLiteral("dump"))
                          || parser.isSet(QStringLiteral("dump-json")) || parser.isSet(QStringLiteral("check"));
    if (!headless) {
        return app.exec();
    }
    int status = 0;
    QTimer::singleShot(parser.value(QStringLiteral("wait")).toInt(), &app, [&]() {
        if (parser.isSet(QStringLiteral("grab"))) {
            const QImage image = window->grabWindow();
            const QString path = parser.value(QStringLiteral("grab"));
            if (image.isNull() || !image.save(path)) {
                fprintf(stderr, "qtk-preview: could not write %s\n", qPrintable(path));
                status = 1;
            } else {
                out << "wrote " << path << " (" << image.width() << 'x' << image.height() << ")\n";
            }
        }
        if (parser.isSet(QStringLiteral("dump"))) {
            // A Window root dumps its content; an Item root is itself line 1.
            if (rootItem == window->contentItem()) {
                for (const QQuickItem *item : rootItem->childItems()) {
                    dumpText(out, item, 0);
                }
            } else {
                dumpText(out, rootItem, 0);
                // Popups (menus, dialogs, tooltips) live in the window's
                // Overlay beside the root item; list them after it.
                for (const QQuickItem *sibling : window->contentItem()->childItems()) {
                    if (sibling != rootItem) {
                        out << "overlay:\n";
                        dumpText(out, sibling, 1);
                    }
                }
            }
        }
        if (parser.isSet(QStringLiteral("dump-json"))) {
            QJsonObject document = dumpJson(rootItem);
            if (rootItem != window->contentItem()) {
                QJsonArray overlay;
                for (const QQuickItem *sibling : window->contentItem()->childItems()) {
                    if (sibling != rootItem) {
                        overlay.append(dumpJson(sibling));
                    }
                }
                if (!overlay.isEmpty()) {
                    document.insert(QStringLiteral("overlay"), overlay);
                }
            }
            out << QJsonDocument(document).toJson(QJsonDocument::Indented);
        }
        out.flush();
        if (!parser.isSet(QStringLiteral("stay"))) {
            app.exit(status);
        }
    });
    const int code = app.exec();
    return code != 0 ? code : status;
}
