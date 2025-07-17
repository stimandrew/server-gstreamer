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

// Класс GstStreamer: управляет видеопотоками с камер через GStreamer
class GstStreamer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY hostChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY portChanged)
    Q_PROPERTY(QStringList availableDevices READ availableDevices NOTIFY availableDevicesChanged)
    Q_PROPERTY(QVariantMap activeStreams READ activeStreams NOTIFY activeStreamsChanged)

public:
    explicit GstStreamer(QObject* parent = nullptr);
    ~GstStreamer();

    Q_INVOKABLE void startStreaming(int deviceIndex);
    Q_INVOKABLE void stopStreaming(int deviceIndex);
    Q_INVOKABLE void stopAllStreams();
    Q_INVOKABLE bool isCameraActive(int deviceIndex);
    Q_INVOKABLE int findCameraIndex(const QString& deviceId);

    QStringList availableDevices() const;
    QVariantMap activeStreams() const;

    QString host() const;
    void setHost(const QString& host);

    int port() const;
    void setPort(int port);

signals:
    void errorOccurred(const QString& message);
    void hostChanged();
    void portChanged();
    void availableDevicesChanged();
    void activeStreamsChanged();
    void cameraStateChanged(int deviceIndex, bool isActive);

public slots:
    void refreshAvailableDevices();

private:
    struct CameraThread {
        QThread* thread;
        CameraWorker* worker;
        QString deviceId;
    };

    QString m_host = "192.168.1.2";
    int m_port = 5000;
    QStringList m_availableDevices;
    QMap<QString, QVariantMap> m_activeStreams;
    QVector<CameraThread> m_cameraThreads;
    QMutex m_mutex;

    void updateAvailableDevices();
    void updateActiveStreams();
};
