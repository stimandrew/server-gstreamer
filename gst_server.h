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
#include "modbusserver.h"

class GstStreamer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY hostChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY portChanged)
    Q_PROPERTY(QStringList availableDevices READ availableDevices NOTIFY availableDevicesChanged)
    Q_PROPERTY(QVariantMap activeStreams READ activeStreams NOTIFY activeStreamsChanged)
    Q_PROPERTY(bool yoloEnabled READ yoloEnabled WRITE setYoloEnabled NOTIFY yoloEnabledChanged)
    Q_PROPERTY(QString yoloModelPath READ yoloModelPath WRITE setYoloModelPath NOTIFY yoloModelPathChanged)
    Q_PROPERTY(QVariantList objects READ objects NOTIFY objectsChanged)

    Q_PROPERTY(QString modbusHost READ modbusHost WRITE setModbusHost NOTIFY modbusHostChanged)
    Q_PROPERTY(int modbusPort READ modbusPort WRITE setModbusPort NOTIFY modbusPortChanged)
    Q_PROPERTY(bool modbusRunning READ modbusRunning NOTIFY modbusStatusChanged)
    Q_PROPERTY(QString modbusStatus READ modbusStatus NOTIFY modbusStatusChanged)

public:
    explicit GstStreamer(QObject* parent = nullptr);
    ~GstStreamer();
    Q_INVOKABLE void startStreaming(int deviceIndex);
    Q_INVOKABLE void stopStreaming(int deviceIndex);
    Q_INVOKABLE void stopAllStreams();
    Q_INVOKABLE bool isCameraActive(int deviceIndex);
    Q_INVOKABLE int findCameraIndex(const QString& deviceId);
    Q_INVOKABLE void captureCameraImage(int deviceIndex, const QString& savePath = "");
    Q_INVOKABLE void setYoloEnabled(bool enabled);
    Q_INVOKABLE void setYoloModelPath(const QString& path);
    Q_INVOKABLE void toggleModbusServer();

    bool modbusRunning() const;
    QString modbusStatus() const;
    QString modbusHost() const;
    void setModbusHost(const QString &host);
    int modbusPort() const;
    void setModbusPort(int port);

    QStringList availableDevices() const;
    QVariantMap activeStreams() const;
    QString host() const;
    void setHost(const QString& host);
    int port() const;
    void setPort(int port);
    bool yoloEnabled() const;
    QString yoloModelPath() const;
    QVariantList objects() const;

signals:
    void errorOccurred(const QString& message);
    void hostChanged();
    void portChanged();
    void availableDevicesChanged();
    void activeStreamsChanged();
    void cameraStateChanged(int deviceIndex, bool isActive);
    void yoloEnabledChanged(bool enabled);
    void yoloModelPathChanged(const QString& path);
    void objectsChanged(const QVariantList& objects);
    void modbusStatusChanged();
    void modbusHostChanged();
    void modbusPortChanged();

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
    CameraWorker* m_camera = nullptr;

    bool m_yoloEnabled = false;
    QString m_yoloModelPath;
    QList<QPair<QRect, QString>> m_objects;
    ModbusServer* m_modbusServer;
    void updateAvailableDevices();
    void updateActiveStreams();
    void resetCamera();

    void handleModbusData(QModbusDataUnit::RegisterType table, int address, int size);
    void handleModbusStateChanged(int state);
    void handleModbusError(QModbusDevice::Error error);

    QString m_modbusHost = "192.168.1.1";
    int m_modbusPort = 50200;
};
