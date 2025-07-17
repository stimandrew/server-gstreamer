// Файл: gst_server.cpp
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
    QString host = m_host;
    int port = m_port + m_cameraThreads.size();

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

            m_cameraThreads.remove(i);
            m_activeStreams.remove(deviceId);
            emit cameraStateChanged(deviceIndex, false);
            emit activeStreamsChanged();
            break;
        }
    }
}

void GstStreamer::stopAllStreams() {
    QMutexLocker locker(&m_mutex);

    auto threadsCopy = m_cameraThreads;
    m_cameraThreads.clear();

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

    for (int index : activeIndices) {
        emit cameraStateChanged(index, false);
    }
    emit activeStreamsChanged();
}

bool GstStreamer::isCameraActive(int deviceIndex) {
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

int GstStreamer::findCameraIndex(const QString& deviceId) {
    QMutexLocker locker(&m_mutex);
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    for (int i = 0; i < cameras.size(); ++i) {
        if (cameras[i].id() == deviceId) return i;
    }
    return -1;
}

QStringList GstStreamer::availableDevices() const {
    return m_availableDevices;
}

QVariantMap GstStreamer::activeStreams() const {
    QVariantMap result;
    for (auto it = m_activeStreams.constBegin(); it != m_activeStreams.constEnd(); ++it) {
        result.insert(it.key(), QVariant::fromValue(it.value()));
    }
    return result;
}

QString GstStreamer::host() const {
    return m_host;
}

void GstStreamer::setHost(const QString& host) {
    if (m_host != host) {
        m_host = host;
        emit hostChanged();
    }
}

int GstStreamer::port() const {
    return m_port;
}

void GstStreamer::setPort(int port) {
    if (m_port != port) {
        m_port = port;
        emit portChanged();
    }
}

void GstStreamer::refreshAvailableDevices() {
    updateAvailableDevices();
}

void GstStreamer::updateAvailableDevices() {
    QMutexLocker locker(&m_mutex);

    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();

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
            auto& ct = m_cameraThreads[i];

            disconnect(ct.worker, nullptr, this, nullptr);
            disconnect(ct.thread, nullptr, nullptr, nullptr);

            ct.worker->stopStreaming();
            ct.thread->quit();

            if (!ct.thread->wait(500)) {
                ct.thread->terminate();
                ct.thread->wait();
            }

            delete ct.worker;
            delete ct.thread;
            m_cameraThreads.remove(i);
            m_activeStreams.remove(currentId);
        }
    }

    QStringList newDevices;
    for (const QCameraDevice& camera : cameras) {
        newDevices.append(camera.description());
    }

    if (newDevices != m_availableDevices) {
        m_availableDevices = newDevices.isEmpty()
        ? QStringList{"No cameras found"}
        : newDevices;

        QList<int> stoppedIndices;
        const auto oldActive = m_activeStreams.keys();
        for (const QString& id : oldActive) {
            if (!m_activeStreams.contains(id)) {
                int idx = findCameraIndex(id);
                if (idx >= 0) stoppedIndices.append(idx);
            }
        }

        locker.unlock();

        emit availableDevicesChanged();
        emit activeStreamsChanged();

        for (int idx : stoppedIndices) {
            emit cameraStateChanged(idx, false);
        }
    }
}

void GstStreamer::updateActiveStreams() {
    // Не требует реализации - обновляется через сигналы
}
