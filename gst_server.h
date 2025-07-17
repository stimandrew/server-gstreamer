// Файл: gst_server.h
#pragma once

#include <QObject>
#include <QDebug>
#include <QFileInfo>
#include <QCamera>
#include <QCameraDevice>
#include <QMediaDevices>
#include <QThread>
#include <QVector>
#include <QMutex>
#include <gst/gst.h>
#include <QMap>
#include "cameraworker.h"
#include "cameracaptureworker.h"

// Класс для управления видеопотоками с камер через GStreamer
class GstStreamer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY hostChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY portChanged)
    Q_PROPERTY(QStringList availableDevices READ availableDevices NOTIFY availableDevicesChanged)
    Q_PROPERTY(QVariantMap activeStreams READ activeStreams NOTIFY activeStreamsChanged)

public:
    // Конструктор класса
    explicit GstStreamer(QObject* parent = nullptr);

    // Деструктор класса
    ~GstStreamer();

    // Запускает поток с камеры по указанному индексу
    Q_INVOKABLE void startStreaming(int deviceIndex);

    // Останавливает поток с камеры по указанному индексу
    Q_INVOKABLE void stopStreaming(int deviceIndex);

    // Останавливает все активные потоки
    Q_INVOKABLE void stopAllStreams();

    // Проверяет, активна ли камера по указанному индексу
    Q_INVOKABLE bool isCameraActive(int deviceIndex);

    // Находит индекс камеры по её идентификатору
    Q_INVOKABLE int findCameraIndex(const QString& deviceId);

    Q_INVOKABLE void captureCameraImage(int deviceIndex, const QString& savePath = "");

    // Возвращает список доступных устройств
    QStringList availableDevices() const;

    // Возвращает информацию о активных потоках
    QVariantMap activeStreams() const;

    // Возвращает текущий хост для потоков
    QString host() const;

    // Устанавливает хост для потоков
    void setHost(const QString& host);

    // Возвращает текущий порт для потоков
    int port() const;

    // Устанавливает порт для потоков
    void setPort(int port);

signals:
    // Сигнал об ошибке
    void errorOccurred(const QString& message);

    // Сигнал об изменении хоста
    void hostChanged();

    // Сигнал об изменении порта
    void portChanged();

    // Сигнал об изменении списка доступных устройств
    void availableDevicesChanged();

    // Сигнал об изменении списка активных потоков
    void activeStreamsChanged();

    // Сигнал об изменении состояния камеры
    void cameraStateChanged(int deviceIndex, bool isActive);

public slots:
    // Обновляет список доступных устройств
    void refreshAvailableDevices();

private:
    // Структура для хранения информации о потоке камеры
    struct CameraThread {
        QThread* thread;
        CameraWorker* worker;
        QString deviceId;
    };

    QString m_host = "192.168.1.2"; // Хост по умолчанию
    int m_port = 5000;              // Порт по умолчанию
    QStringList m_availableDevices; // Список доступных устройств
    QMap<QString, QVariantMap> m_activeStreams; // Активные потоки
    QVector<CameraThread> m_cameraThreads; // Потоки камер
    QMutex m_mutex; // Мьютекс для синхронизации

    // Обновляет список доступных устройств
    void updateAvailableDevices();

    // Обновляет список активных потоков
    void updateActiveStreams();
};
