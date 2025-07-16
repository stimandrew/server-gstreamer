// gst_server.cpp
#include "gst_server.h"


GstStreamer::GstStreamer(QObject* parent) : QObject(parent)
{
    updateAvailableDevices();
}

GstStreamer::~GstStreamer()
{
    stopAllStreams();
}

void GstStreamer::startStreaming(int deviceIndex) {
    QMutexLocker locker(&m_mutex);
    if (deviceIndex < 0 || deviceIndex >= m_availableDevices.size()) {
        emit errorOccurred("Invalid camera index");
        return;
    }

    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    if (deviceIndex >= cameras.size()) {
        emit errorOccurred("Camera not available");
        return;
    }

    QString deviceId = cameras.at(deviceIndex).id();
    QString host = m_host; // Create local copies
    int port = m_port + m_cameraThreads.size();

    // Check if already streaming
    for (const auto& ct : m_cameraThreads) {
        if (ct.deviceId == deviceId) {
            emit errorOccurred("Camera already streaming");
            return;
        }
    }

    QThread* thread = new QThread();
    CameraWorker* worker = new CameraWorker(deviceId, host, port);

    worker->moveToThread(thread);

    connect(thread, &QThread::started, worker, &CameraWorker::startStreaming);
    connect(worker, &CameraWorker::streamingStateChanged, this, [this, deviceId, host, port](bool isStreaming) {
        QMutexLocker locker(&m_mutex);
        if (isStreaming) {
            const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
            QString cameraName;
            for (const auto& camera : cameras) {
                if (camera.id() == deviceId) {
                    cameraName = camera.description();
                    break;
                }
            }

            QVariantMap streamInfo;
            streamInfo["name"] = cameraName;
            streamInfo["address"] = QString("%1:%2").arg(host).arg(port);
            streamInfo["deviceId"] = deviceId;
            m_activeStreams[deviceId] = streamInfo;
            emit activeStreamsChanged();
        } else {
            m_activeStreams.remove(deviceId);
            emit activeStreamsChanged();
        }
    });
    connect(worker, &CameraWorker::errorOccurred, this, &GstStreamer::errorOccurred);
    connect(thread, &QThread::finished, worker, &CameraWorker::deleteLater);

    m_cameraThreads.append({thread, worker, deviceId});
    thread->start();
}

void GstStreamer::stopStreaming(int deviceIndex) {
    QMutexLocker locker(&m_mutex);
    if (deviceIndex < 0 || deviceIndex >= m_availableDevices.size()) {
        emit errorOccurred("Invalid camera index");
        return;
    }

    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    if (deviceIndex >= cameras.size()) {
        emit errorOccurred("Camera not available");
        return;
    }

    QString deviceId = cameras.at(deviceIndex).id();

    for (int i = 0; i < m_cameraThreads.size(); ++i) {
        if (m_cameraThreads[i].deviceId == deviceId) {
            m_cameraThreads[i].worker->stopStreaming();
            m_cameraThreads[i].thread->quit();
            m_cameraThreads[i].thread->wait();

            delete m_cameraThreads[i].worker;
            delete m_cameraThreads[i].thread;

            m_cameraThreads.remove(i);
            break;
        }
    }

    m_activeStreams.remove(deviceId);
    emit activeStreamsChanged();
}

void GstStreamer::stopAllStreams()
{
    QMutexLocker locker(&m_mutex);

    // Создаем временную копию для безопасного удаления
    auto threadsCopy = m_cameraThreads;
    m_cameraThreads.clear();

    // Останавливаем все потоки
    for (auto& ct : threadsCopy) {
        // Отключаем сигналы перед удалением
        disconnect(ct.worker, nullptr, this, nullptr);
        disconnect(ct.thread, nullptr, nullptr, nullptr);

        ct.worker->stopStreaming();
        ct.thread->quit();

        if (!ct.thread->wait(1000)) {
            qWarning() << "Thread didn't finish in time, terminating";
            ct.thread->terminate();
            ct.thread->wait();
        }

        delete ct.worker;
        delete ct.thread;
    }

    m_activeStreams.clear();
    emit activeStreamsChanged();
}

void GstStreamer::refreshAvailableDevices()
{
    updateAvailableDevices();
}

void GstStreamer::updateAvailableDevices()
{
    QMutexLocker locker(&m_mutex);
    m_availableDevices.clear();
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();

    for (const QCameraDevice& camera : cameras) {
        m_availableDevices.append(camera.description());
    }

    if (m_availableDevices.isEmpty()) {
        m_availableDevices.append("No cameras found");
    }

    emit availableDevicesChanged();
}

void GstStreamer::updateActiveStreams()
{
    // No implementation needed - updated via signals
}
