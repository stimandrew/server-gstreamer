#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include "gst_server.h"

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "wayland");
    qputenv("GST_DEBUG", "4");
    qputenv("GST_DEBUG_NO_COLOR", "1");

    gst_init(&argc, &argv);
    QGuiApplication app(argc, argv);

    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    qmlRegisterType<GstStreamer>("gst_server", 1, 0, "GstStreamer");

    QQmlApplicationEngine engine;

    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

    if (engine.rootObjects().isEmpty()) {
        return -1;
    }



    return app.exec();
}
