#pragma once

#include <QObject>
#include <QMutex>
#include <gst/gst.h>
#include <gst/app/app.h>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <QMediaDevices>
#include <QTimer>
#include <QThread>
#include <QEventLoop>
#include <QDateTime>

class CameraWorker : public QObject {
    Q_OBJECT
public:
    CameraWorker(const QString& deviceId, const QString& host, int port, QObject* parent = nullptr);
    ~CameraWorker();

public slots:
    void startStreaming();
    void stopStreaming();
    void captureFrame(const QString& savePath);

signals:
    void errorOccurred(const QString& message);
    void streamingStateChanged(bool isStreaming);

private slots:
    void handleFrame(const QVideoFrame& frame);
private:
    GstElement* m_pipeline = nullptr;
    GstElement* m_appsrc = nullptr;
    QString m_deviceId;
    QString m_host;
    int m_port;
    QMutex m_mutex;
    QCamera* m_camera = nullptr;
    QMediaCaptureSession m_captureSession;
    QVideoSink m_videoSink;
    bool m_isStreaming = false;
    guint64 m_frameCount = 0;

    void ensureInWorkerThread() {
        if (QThread::currentThread() != this->thread()) {
            qCritical() << "Method called from wrong thread!";
            Q_ASSERT(false);
        }
    }

    static void onBusMessage(GstBus* bus, GstMessage* msg, gpointer data);
    static void onNeedData(GstElement* appsrc, guint size, gpointer data);
    static void onEnoughData(GstElement* appsrc, gpointer data);
};
