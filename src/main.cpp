#include <QGuiApplication>
#include <QIcon>
#include <QFont>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "backend.h"
#include "theme.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    QFont appFont(QStringLiteral("monospace"));
    appFont.setPointSize(12);
    app.setFont(appFont);
    app.setApplicationName("omawise");
    app.setApplicationVersion("0.1.2");
    app.setDesktopFileName("omawise");
    app.setWindowIcon(QIcon::fromTheme("omawise"));
    QQuickStyle::setStyle("Material");

    Theme theme;
    Backend backend;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
    return app.exec();
}
