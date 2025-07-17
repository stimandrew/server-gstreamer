#pragma once

#include <QObject>
#include <QImage>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QVideoSink>
#include <QThread>
#include <QMutex>
#include <QDir>

class CameraCaptureWorker : public QObject
{
    Q_OBJECT
public:
    explicit CameraCaptureWorker(const QString& deviceId, QObject* parent = nullptr);
    ~CameraCaptureWorker();

public slots:
    void startCapture(int intervalMs = 1000, const QString& savePath = QDir::currentPath());
    void stopCapture();
    void captureSingleImage(const QString& savePath = QDir::currentPath());

signals:
    void captureStarted();
    void captureStopped();
    void imageCaptured(const QString& filePath);
    void errorOccurred(const QString& message);

private slots:
    void handleFrame(const QVideoFrame& frame);

private:
    QCamera* m_camera = nullptr;
    QMediaCaptureSession m_captureSession;
    QVideoSink m_videoSink;
    QString m_deviceId;
    QMutex m_mutex;
    bool m_isCapturing = false;
    int m_captureInterval = 1000;
    QString m_savePath;
    QTimer* m_captureTimer = nullptr;

    void saveImage(const QImage& image, const QString& baseName = "");
};
