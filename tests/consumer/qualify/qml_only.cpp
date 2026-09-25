// SPDX-License-Identifier: LGPL-3.0-or-later
// WP-088 QML-only consumer: links Qt Quick but not libqindatk. Loading the
// installed QindaTK plugin therefore depends on the plugin's own RUNPATH
// (src/CMakeLists.txt, D-202), which is what the relocation check proves.
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>

#include <cstdio>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const QString importRoot = QDir::cleanPath(
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral(QUALIFY_BIN_TO_QML)));
    QQmlApplicationEngine engine;
    engine.addImportPath(importRoot);
    engine.load(QUrl(QStringLiteral("qrc:/qualify/Main.qml")));
    if (engine.rootObjects().size() != 1)
        return 2;
    const QString text = engine.rootObjects().constFirst()->property("buttonText").toString();
    std::printf("qml-only consumer: importRoot=%s buttonText=%s\n",
                qPrintable(importRoot), qPrintable(text));
    return text == QStringLiteral("Qualify") ? 0 : 2;
}
