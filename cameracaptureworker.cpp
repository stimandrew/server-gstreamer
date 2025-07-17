#include "cameracaptureworker.h"
#include <QDateTime>
#include <QTimer>
#include <QDebug>
#include <QMediaDevices>

CameraCaptureWorker::CameraCaptureWorker(const QString& deviceId, QObject* parent)
    : QObject(parent), m_deviceId(deviceId)
{
    // Настраиваем видео sink для получения кадров
    connect(&m_videoSink, &QVideoSink::videoFrameChanged,
            this, &CameraCaptureWorker::handleFrame);
    m_captureSession.setVideoSink(&m_videoSink);
}

CameraCaptureWorker::~CameraCaptureWorker()
{
    stopCapture();
    if (m_camera) {
        m_camera->stop();
        delete m_camera;
    }
}

void CameraCaptureWorker::startCapture(int intervalMs, const QString& savePath)
{
    QMutexLocker locker(&m_mutex);

    if (m_isCapturing) {
        emit errorOccurred("Capture is already running");
        return;
    }

    // Инициализация камеры
    if (!m_camera) {
        const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
        for (const QCameraDevice& camera : cameras) {
            if (camera.id() == m_deviceId) {
                m_camera = new QCamera(camera);
                m_captureSession.setCamera(m_camera);
                break;
            }
        }

        if (!m_camera) {
            emit errorOccurred("Camera not found");
            return;
        }
    }

    m_savePath = savePath;
    m_captureInterval = intervalMs;
    m_isCapturing = true;

    // Создаем таймер для периодического захвата
    if (!m_captureTimer) {
        m_captureTimer = new QTimer(this);
        connect(m_captureTimer, &QTimer::timeout, this, [this]() {
            if (m_camera && m_camera->isActive()) {
                // Захватываем следующий кадр (обработка будет в handleFrame)
                m_videoSink.setVideoFrame(QVideoFrame());
            }
        });
    }

    m_camera->start();
    m_captureTimer->start(m_captureInterval);

    emit captureStarted();
}

void CameraCaptureWorker::stopCapture()
{
    QMutexLocker locker(&m_mutex);

    if (!m_isCapturing) return;

    if (m_captureTimer) {
        m_captureTimer->stop();
    }

    if (m_camera && m_camera->isActive()) {
        m_camera->stop();
    }

    m_isCapturing = false;
    emit captureStopped();
}

void CameraCaptureWorker::captureSingleImage(const QString& savePath)
{
    QMutexLocker locker(&m_mutex);

    if (!m_camera) {
        // Инициализация камеры для единичного захвата
        const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
        for (const QCameraDevice& camera : cameras) {
            if (camera.id() == m_deviceId) {
                m_camera = new QCamera(camera);
                m_captureSession.setCamera(m_camera);
                break;
            }
        }

        if (!m_camera) {
            emit errorOccurred("Camera not found");
            return;
        }

        connect(m_camera, &QCamera::activeChanged, this, [this, savePath](bool active) {
            if (active) {
                // После активации камеры делаем снимок
                m_videoSink.setVideoFrame(QVideoFrame());
            }
        });

        m_camera->start();
    } else if (m_camera->isActive()) {
        // Камера уже активна - делаем снимок
        m_videoSink.setVideoFrame(QVideoFrame());
    } else {
        m_camera->start();
    }

    if (!savePath.isEmpty()) {
        m_savePath = savePath;
    }
}

void CameraCaptureWorker::handleFrame(const QVideoFrame& frame)
{
    QMutexLocker locker(&m_mutex);

    if (!frame.isValid()) {
        return;
    }

    QImage image = frame.toImage();
    if (image.isNull()) {
        emit errorOccurred("Failed to convert video frame to image");
        return;
    }

    if (m_isCapturing) {
        // Для периодического захвата
        saveImage(image);
    } else {
        // Для единичного захвата
        saveImage(image, "single_capture");
        m_camera->stop();
    }
}

void CameraCaptureWorker::saveImage(const QImage& image, const QString& baseName)
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmsszzz");
    QString fileName = baseName.isEmpty()
                           ? QString("%1/capture_%2.jpg").arg(m_savePath).arg(timestamp)
                           : QString("%1/%2_%3.jpg").arg(m_savePath).arg(baseName).arg(timestamp);

    if (!image.save(fileName, "JPEG", 90)) {
        emit errorOccurred(QString("Failed to save image to %1").arg(fileName));
        return;
    }

    emit imageCaptured(fileName);
}
