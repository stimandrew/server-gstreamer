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
    connect(worker, &CameraWorker::streamingStateChanged, this, [this, deviceIndex, deviceId, host, port](bool isStreaming) {
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
            emit cameraStateChanged(deviceIndex, true);
        } else {
            m_activeStreams.remove(deviceId);
            emit activeStreamsChanged();
            emit cameraStateChanged(deviceIndex, false);
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
            auto& ct = m_cameraThreads[i];

            // Отключаем все сигналы, чтобы избежать возможных колбэков
            disconnect(ct.worker, nullptr, this, nullptr);
            disconnect(ct.thread, nullptr, nullptr, nullptr);

            // Останавливаем пайплайн и поток
            ct.worker->stopStreaming();
            ct.thread->quit();

            // Даем потоку время на завершение
            if (!ct.thread->wait(1000)) {
                qWarning() << "Thread didn't finish in time, terminating";
                ct.thread->terminate();
                ct.thread->wait();
            }

            // Удаляем объекты
            delete ct.worker;
            delete ct.thread;

            // Удаляем запись из списка
            m_cameraThreads.remove(i);

            // Обновляем состояние
            m_activeStreams.remove(deviceId);
            emit cameraStateChanged(deviceIndex, false);
            emit activeStreamsChanged();
            break;
        }
    }
}

void GstStreamer::stopAllStreams() {
    QMutexLocker locker(&m_mutex);

    // Создаем временную копию для безопасного удаления
    auto threadsCopy = m_cameraThreads;
    m_cameraThreads.clear();

    // Собираем индексы всех активных камер
    QList<int> activeIndices;
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    for (const auto& ct : threadsCopy) {
        for (int i = 0; i < cameras.size(); ++i) {
            if (cameras[i].id() == ct.deviceId) {
                activeIndices.append(i);
                break;
            }
        }
    }

    // Останавливаем все потоки
    for (auto& ct : threadsCopy) {
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

    // Уведомляем UI об остановке каждой камеры
    for (int index : activeIndices) {
        emit cameraStateChanged(index, false);
    }
    emit activeStreamsChanged();
}

void GstStreamer::refreshAvailableDevices() {
    updateAvailableDevices();
}

void GstStreamer::updateAvailableDevices() {
    QMutexLocker locker(&m_mutex);

    // Get current cameras
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();

    // Check for removed cameras and stop their threads
    for (int i = m_cameraThreads.size() - 1; i >= 0; --i) {
        bool found = false;
        QString currentId = m_cameraThreads[i].deviceId;

        for (const QCameraDevice& camera : cameras) {
            if (camera.id() == currentId) {
                found = true;
                break;
            }
        }

        if (!found) {
            // Camera was removed - stop the stream
            auto& ct = m_cameraThreads[i];

            // Disconnect signals to prevent callbacks during cleanup
            disconnect(ct.worker, nullptr, this, nullptr);
            disconnect(ct.thread, nullptr, nullptr, nullptr);

            // Stop the stream
            ct.worker->stopStreaming();
            ct.thread->quit();

            if (!ct.thread->wait(500)) {
                ct.thread->terminate();
                ct.thread->wait();
            }

            // Clean up
            delete ct.worker;
            delete ct.thread;
            m_cameraThreads.remove(i);

            // Remove from active streams
            m_activeStreams.remove(currentId);
        }
    }

    // Update available devices list
    QStringList newDevices;
    for (const QCameraDevice& camera : cameras) {
        newDevices.append(camera.description());
    }

    if (newDevices != m_availableDevices) {
        m_availableDevices = newDevices.isEmpty()
        ? QStringList{"No cameras found"}
        : newDevices;

        // Find camera indices that were stopped
        QList<int> stoppedIndices;
        const auto oldActive = m_activeStreams.keys();
        for (const QString& id : oldActive) {
            if (!m_activeStreams.contains(id)) {
                int idx = findCameraIndex(id);
                if (idx >= 0) stoppedIndices.append(idx);
            }
        }

        // Emit signals after all updates are done
        locker.unlock();

        emit availableDevicesChanged();
        emit activeStreamsChanged();

        for (int idx : stoppedIndices) {
            emit cameraStateChanged(idx, false);
        }
    }
}

void GstStreamer::updateActiveStreams()
{
    // No implementation needed - updated via signals
}
