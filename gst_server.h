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
    Q_INVOKABLE bool isCameraActive(int deviceIndex) {
        QMutexLocker locker(&m_mutex);
        if (deviceIndex < 0 || deviceIndex >= m_availableDevices.size()) {
            return false;
        }

        const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
        if (deviceIndex >= cameras.size()) {
            return false;
        }

        QString deviceId = cameras.at(deviceIndex).id();
        return m_activeStreams.contains(deviceId);
    }

    Q_INVOKABLE int findCameraIndex(const QString& deviceId) {
        QMutexLocker locker(&m_mutex);
        const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
        for (int i = 0; i < cameras.size(); ++i) {
            if (cameras[i].id() == deviceId) return i;
        }
        return -1;
    }

    QStringList availableDevices() const { return m_availableDevices; }
    QVariantMap activeStreams() const {
        QVariantMap result;
        for (auto it = m_activeStreams.constBegin(); it != m_activeStreams.constEnd(); ++it) {
            result.insert(it.key(), QVariant::fromValue(it.value()));
        }
        return result;
    }

    QString host() const { return m_host; }
    void setHost(const QString& host) {
        if (m_host != host) {
            m_host = host;
            emit hostChanged();
        }
    }

    int port() const { return m_port; }
    void setPort(int port) {
        if (m_port != port) {
            m_port = port;
            emit portChanged();
        }
    }

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
