// cameraworker.h
#pragma once

#include <QObject>
#include <QMutex>
#include <gst/gst.h>
#include <gst/app/app.h>
#include <QCamera>
#include <QCameraFormat>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <QMediaDevices>
#include <QThread>
#include <QDateTime>
#include <QEventLoop>
#include <QTimer>
#include <QQueue>
#include <QWaitCondition>
#include <QPainter>
#include "yolo11.h"

class CameraWorker : public QObject {
    Q_OBJECT
public:
    CameraWorker(const QString& deviceId, const QString& host, int port, QObject* parent = nullptr);
    ~CameraWorker();
    void init();

public slots:
    void startStreaming();
    void stopStreaming();
    void captureFrame(const QString& savePath);
    void setYoloEnabled(bool enabled);
    void setYoloModelPath(const QString& path);

signals:
    void errorOccurred(const QString& message);
    void streamingStateChanged(bool isStreaming);
    void newFrame(const QImage &frame);
    void newObjects(const QList<QPair<QRect, QString>>& objects);

private slots:
    void handleFrame(const QVideoFrame& frame);
    void processNextFrame();

private:
    GstElement* m_pipeline = nullptr;
    GstElement* m_appsrc = nullptr;
    QString m_deviceId;
    QString m_host;
    int m_port;
    QMutex m_mutex;
    QCamera* m_camera = nullptr;
    QMediaCaptureSession m_captureSession;
    QVideoSink* m_videoSink = nullptr;
    bool m_isStreaming = false;
    guint64 m_frameCount = 0;
    std::chrono::steady_clock::time_point m_lastFrameTime;
    QThread* m_thread = nullptr;
    QTimer* m_frameTimer;
    mutable QMutex m_cameraMutex;
    bool m_yoloEnabled = false;
    bool m_yoloInitialized = false;
    mutable QMutex m_yoloMutex;
    rknn_app_context_t m_rknnAppCtx;
    QMutex queueMutex;

    void cleanupPipeline();
    void cleanupCamera();
    void ensureInWorkerThread();
    bool setupPipeline();
    bool setupCamera();
    QImage processFrame(const QVideoFrame& frame);
    void processFrameWithRGA(const QImage &frame, const QString &sourceDeviceId);
    void requestFrame();

    struct FrameData {
        QImage frame;
        QString deviceId;
    };
    QQueue<FrameData> frameQueue;

    QMutex m_yoloProcessingMutex;
    QWaitCondition m_yoloProcessingCondition;
    bool m_yoloProcessing = false;

    static void onBusMessage(GstBus* bus, GstMessage* msg, gpointer data);
    static void onNeedData(GstElement* appsrc, guint size, gpointer data);
    static void onEnoughData(GstElement* appsrc, gpointer data);

    QImage drawDetectionResults(const QImage& frame, const QList<QPair<QRect, QString>>& objects);
    void pushFrameToPipeline(const QImage& frame);
};
