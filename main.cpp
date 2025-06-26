#include <QCoreApplication>
#include <QCommandLineParser>
#include "gst_server.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);


    // Настройка парсера командной строки
    QCommandLineParser parser;
    parser.setApplicationDescription("GStreamer UDP Video Server");
    parser.addHelpOption();

    // Добавление параметров командной строки
    QCommandLineOption hostOption(
        QStringList() << "a" << "address",
        "Target host address",
        "host",
        "192.168.1.2" // Значение по умолчанию
        );
    parser.addOption(hostOption);

    QCommandLineOption portOption(
        QStringList() << "p" << "port",
        "UDP port",
        "port",
        "5000" // Значение по умолчанию
        );
    parser.addOption(portOption);

    // Парсинг аргументов
    parser.process(a);

    // Создание и запуск стримера
    GstStreamer streamer;
    streamer.startStreaming(
        parser.value(hostOption),
        parser.value(portOption).toInt()
        );

    // Обработка сигнала завершения (Ctrl+C)
    QObject::connect(&a, &QCoreApplication::aboutToQuit, [&streamer]() {
        streamer.stopStreaming();
    });

    qDebug() << "Server started. Press Ctrl+C to stop...";

    // Set up code that uses the Qt event loop here.
    // Call a.quit() or a.exit() to quit the application.
    // A not very useful example would be including
    // #include <QTimer>
    // near the top of the file and calling
    // QTimer::singleShot(5000, &a, &QCoreApplication::quit);
    // which quits the application after 5 seconds.

    // If you do not need a running Qt event loop, remove the call
    // to a.exec() or use the Non-Qt Plain C++ Application template.

    return a.exec();
}
