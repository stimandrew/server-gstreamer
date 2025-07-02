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

    // +++ ДОБАВЛЯЕМ НОВУЮ ОПЦИЮ ДЛЯ ВЫБОРА УСТРОЙСТВА +++
    QCommandLineOption deviceOption(
        QStringList() << "d" << "device",
        "Video device index (0, 1, etc.)",
        "index",
        "0" // Значение по умолчанию
        );
    parser.addOption(deviceOption);

    // Парсинг аргументов
    parser.process(a);

    // Создание и запуск стримера
    GstStreamer streamer;
    streamer.startStreaming(
        parser.value(hostOption),
        parser.value(portOption).toInt(),
        // +++ ПЕРЕДАЕМ ИНДЕКС УСТРОЙСТВА +++
        parser.value(deviceOption).toInt()
        );

    // Обработка сигнала завершения (Ctrl+C)
    QObject::connect(&a, &QCoreApplication::aboutToQuit, [&streamer]() {
        streamer.stopStreaming();
    });

    qDebug() << "Server started. Press Ctrl+C to stop...";

    return a.exec();
}
