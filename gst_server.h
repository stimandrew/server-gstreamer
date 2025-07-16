// gst_server.h
#pragma once

#include <QObject>
#include <QDebug>
#include <QFileInfo>
#include <QCamera>
#include <QCameraDevice>
#include <QMediaDevices>
#include <gst/gst.h>

class GstStreamer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isStreaming READ isStreaming NOTIFY streamingStateChanged)
    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY hostChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY portChanged)
    Q_PROPERTY(int deviceIndex READ deviceIndex WRITE setDeviceIndex NOTIFY deviceIndexChanged)
    Q_PROPERTY(QStringList availableDevices READ availableDevices NOTIFY availableDevicesChanged)

public:
    explicit GstStreamer(QObject *parent = nullptr);
    ~GstStreamer();

    Q_INVOKABLE void startStreaming();
    Q_INVOKABLE void stopStreaming();
    Q_INVOKABLE void refreshAvailableDevices();

    bool isStreaming() const { return m_pipeline != nullptr; }
    QStringList availableDevices() const { return m_availableDevices; }

    QString host() const { return m_host; }
    void setHost(const QString &host) {
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

    int deviceIndex() const { return m_deviceIndex; }
    void setDeviceIndex(int index) {
        if (m_deviceIndex != index) {
            m_deviceIndex = index;
            emit deviceIndexChanged();
        }
    }

signals:
    void errorOccurred(const QString& message);
    void streamingStateChanged();
    void hostChanged();
    void portChanged();
    void deviceIndexChanged();
    void availableDevicesChanged();

private:
    GstElement *m_pipeline = nullptr;
    QString m_host = "192.168.1.2";
    int m_port = 5000;
    int m_deviceIndex = 0;
    QStringList m_availableDevices;

    static void onBusMessage(GstBus *bus, GstMessage *msg, gpointer data);
    void updateAvailableDevices();
};
