#pragma once

#include <QObject>
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
