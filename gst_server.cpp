// Файл: gst_server.cpp
#include "gst_server.h"

// Конструктор: инициализирует объект и обновляет список устройств
GstStreamer::GstStreamer(QObject* parent) : QObject(parent)
{
    updateAvailableDevices();
}

// Деструктор: останавливает все активные потоки
GstStreamer::~GstStreamer()
{
    stopAllStreams();
}

// Запускает поток с камеры по указанному индексу
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
    QString host = m_host; // Локальная копия хоста
    int port = m_port + m_cameraThreads.size();

    // Проверка, не запущен ли уже поток для этой камеры
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

// Останавливает поток с камеры по указанному индексу
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

            // Stop worker in its own thread
            QMetaObject::invokeMethod(ct.worker, "stopStreaming", Qt::BlockingQueuedConnection);

            // Clean up connections
            disconnect(ct.worker, nullptr, this, nullptr);
            disconnect(ct.thread, nullptr, ct.worker, nullptr);

            // Quit thread and wait for completion
            ct.thread->quit();
            if (!ct.thread->wait(2000)) {  // Increased timeout
                qWarning() << "Forcing thread termination";
                ct.thread->terminate();
                ct.thread->wait();
            }

            // Clean up objects
            ct.worker->deleteLater();
            ct.thread->deleteLater();

            // Remove from containers
            m_cameraThreads.remove(i);
            m_activeStreams.remove(deviceId);

            emit cameraStateChanged(deviceIndex, false);
            emit activeStreamsChanged();

            return;
        }
    }
}

// Останавливает все активные потоки
void GstStreamer::stopAllStreams() {
    QMutexLocker locker(&m_mutex);

    // Collect indices of active cameras before stopping
    QList<int> activeIndices;
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    for (const auto& ct : m_cameraThreads) {
        for (int i = 0; i < cameras.size(); ++i) {
            if (cameras[i].id() == ct.deviceId) {
                activeIndices.append(i);
                break;
            }
        }
    }

    // Stop all workers in their own threads first
    for (auto& ct : m_cameraThreads) {
        QMetaObject::invokeMethod(ct.worker, "stopStreaming", Qt::BlockingQueuedConnection);
    }

    // Then clean up all threads
    for (auto& ct : m_cameraThreads) {
        disconnect(ct.worker, nullptr, this, nullptr);
        disconnect(ct.thread, nullptr, nullptr, nullptr);

        ct.thread->quit();
        if (!ct.thread->wait(100)) {  // Increased timeout
            qWarning() << "Forcing thread termination";
            ct.thread->terminate();
            ct.thread->wait();
        }

        ct.worker->deleteLater();
        ct.thread->deleteLater();
    }

    m_cameraThreads.clear();
    m_activeStreams.clear();

    // Notify UI about stopped streams
    for (int index : activeIndices) {
        emit cameraStateChanged(index, false);
    }
    emit activeStreamsChanged();
}

// Проверяет, активна ли камера по указанному индексу
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

// Находит индекс камеры по её идентификатору
int GstStreamer::findCameraIndex(const QString& deviceId) {
    QMutexLocker locker(&m_mutex);
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    for (int i = 0; i < cameras.size(); ++i) {
        if (cameras[i].id() == deviceId) return i;
    }
    return -1;
}

// Возвращает список доступных устройств
QStringList GstStreamer::availableDevices() const {
    return m_availableDevices;
}

// Возвращает информацию о активных потоках
QVariantMap GstStreamer::activeStreams() const {
    QVariantMap result;
    for (auto it = m_activeStreams.constBegin(); it != m_activeStreams.constEnd(); ++it) {
        result.insert(it.key(), QVariant::fromValue(it.value()));
    }
    return result;
}

// Возвращает текущий хост
QString GstStreamer::host() const {
    return m_host;
}

// Устанавливает хост
void GstStreamer::setHost(const QString& host) {
    if (m_host != host) {
        m_host = host;
        emit hostChanged();
    }
}

// Возвращает текущий порт
int GstStreamer::port() const {
    return m_port;
}

// Устанавливает порт
void GstStreamer::setPort(int port) {
    if (m_port != port) {
        m_port = port;
        emit portChanged();
    }
}

// Обновляет список доступных устройств (публичный слот)
void GstStreamer::refreshAvailableDevices() {
    updateAvailableDevices();
}

// Обновляет список доступных устройств
void GstStreamer::updateAvailableDevices() {
    QMutexLocker locker(&m_mutex);

    // Получаем текущие камеры
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();

    // Проверяем удаленные камеры
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
            // Камера удалена - останавливаем поток
            auto& ct = m_cameraThreads[i];

            // Отключаем все сигналы от worker и thread
            disconnect(ct.worker, nullptr, this, nullptr);
            disconnect(ct.thread, nullptr, nullptr, nullptr);

            // Останавливаем worker в его собственном потоке
            QMetaObject::invokeMethod(ct.worker, "stopStreaming", Qt::BlockingQueuedConnection);

            // Завершаем поток
            ct.thread->quit();
            if (!ct.thread->wait(500)) {
                ct.thread->terminate();
                ct.thread->wait();
            }

            // Удаляем объекты
            ct.worker->deleteLater();
            ct.thread->deleteLater();

            // Удаляем из контейнеров
            m_cameraThreads.remove(i);
            m_activeStreams.remove(currentId);

            // Находим индекс камеры для уведомления UI
            int idx = -1;
            for (int j = 0; j < m_availableDevices.size(); ++j) {
                if (m_availableDevices[j].contains(currentId)) {
                    idx = j;
                    break;
                }
            }

            if (idx >= 0) {
                locker.unlock();
                emit cameraStateChanged(idx, false);
                locker.relock();
            }
        }
    }

    // Обновляем список устройств
    QStringList newDevices;
    for (const QCameraDevice& camera : cameras) {
        newDevices.append(camera.description());
    }

    if (newDevices != m_availableDevices) {
        m_availableDevices = newDevices.isEmpty()
        ? QStringList{"No cameras found"}
        : newDevices;

        locker.unlock();
        emit availableDevicesChanged();
        emit activeStreamsChanged();
    }
}

// Обновляет список активных потоков
void GstStreamer::updateActiveStreams()
{
    // Не требует реализации - обновляется через сигналы
}


void GstStreamer::captureCameraImage(int deviceIndex, const QString& savePath)
{
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

    // Создаем worker для захвата изображения
    CameraCaptureWorker* worker = new CameraCaptureWorker(deviceId);
    QThread* thread = new QThread();

    worker->moveToThread(thread);

    connect(thread, &QThread::started, worker, [worker, savePath]() {
        worker->captureSingleImage(savePath);
    });
    connect(worker, &CameraCaptureWorker::imageCaptured, this, [this, thread, worker](const QString& filePath) {
        emit errorOccurred(QString("Image captured: %1").arg(filePath));
        worker->deleteLater();
        thread->quit();
    });
    connect(worker, &CameraCaptureWorker::errorOccurred, this, &GstStreamer::errorOccurred);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);

    thread->start();
}
