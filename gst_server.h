// gst_server.h
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

class CameraWorker : public QObject {
    Q_OBJECT
public:
    CameraWorker(const QString& deviceId, const QString& host, int port, QObject* parent = nullptr);
    ~CameraWorker();

public slots:
    void startStreaming();
    void stopStreaming();

signals:
    void errorOccurred(const QString& message);
    void streamingStateChanged(bool isStreaming);

private:
    GstElement* m_pipeline = nullptr;
    QString m_deviceId;
    QString m_host;
    int m_port;
    QMutex m_mutex;

    static void onBusMessage(GstBus* bus, GstMessage* msg, gpointer data);
};

class GstStreamer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY hostChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY portChanged)
    Q_PROPERTY(QStringList availableDevices READ availableDevices NOTIFY availableDevicesChanged)
    Q_PROPERTY(QStringList activeStreams READ activeStreams NOTIFY activeStreamsChanged)

public:
    explicit GstStreamer(QObject* parent = nullptr);
    ~GstStreamer();

    Q_INVOKABLE void startStreaming(int deviceIndex);
    Q_INVOKABLE void stopStreaming(int deviceIndex);
    Q_INVOKABLE void stopAllStreams();
    Q_INVOKABLE void refreshAvailableDevices();

    QStringList availableDevices() const { return m_availableDevices; }
    QStringList activeStreams() const { return m_activeStreams; }

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

private:
    struct CameraThread {
        QThread* thread;
        CameraWorker* worker;
        QString deviceId;
    };

    QString m_host = "192.168.1.2";
    int m_port = 5000;
    QStringList m_availableDevices;
    QStringList m_activeStreams;
    QVector<CameraThread> m_cameraThreads;
    QMutex m_mutex;

    void updateAvailableDevices();
    void updateActiveStreams();
};
