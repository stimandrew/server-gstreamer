// Файл: gst_server.cpp
#include "gst_server.h"

// Конструктор: инициализирует объект и обновляет список устройств
GstStreamer::GstStreamer(QObject* parent) : QObject(parent)
{
    qDebug() << "Creating GstStreamer...";
    deviceServer = new ModbusDeviceServer(this);

    // Устанавливаем ссылку на себя в Modbus сервере
    deviceServer->setStreamer(this);

    if (deviceServer) {
        qDebug() << "Modbus server created successfully";
        connect(deviceServer, &ModbusServer::dataWritten,
                this, &GstStreamer::handleModbusData);
        connect(deviceServer, &ModbusServer::stateChanged,
                this, &GstStreamer::handleModbusStateChanged);
        connect(deviceServer, &ModbusServer::errorOccurred,
                this, &GstStreamer::handleModbusError);
        qDebug() << "Modbus server signals connected";
    } else {
        qWarning() << "Failed to create Modbus server!";
    }

    updateAvailableDevices();
    qDebug() << "GstStreamer initialized";
    QTimer::singleShot(5000, this, &GstStreamer::autoConnectModbus);
}

// Деструктор: останавливает все активные потоки
GstStreamer::~GstStreamer()
{
    stopAllStreams();
}

bool GstStreamer::yoloEnabled() const { return m_yoloEnabled; }
QString GstStreamer::yoloModelPath() const { return m_yoloModelPath; }
QVariantList GstStreamer::objects() const {
    QVariantList list;
    for (const auto &obj : m_objects) {
        QVariantMap map;
        map["rect"] = QVariant::fromValue(obj.first);
        map["label"] = obj.second;
        list.append(map);
    }
    return list;
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

    // Проверка на уже запущенный поток
    for (const auto& ct : m_cameraThreads) {
        if (ct.deviceId == deviceId) {
            emit errorOccurred("Camera already streaming");
            return;
        }
    }

    // Уникальный порт для каждого потока
    int port = m_port + m_cameraThreads.size();

    m_camera = new CameraWorker(deviceId, m_host, port);

    connect(m_camera, &CameraWorker::newObjects, this, [this, deviceId](const QList<QPair<QRect, QString>>& objects) {
        QMutexLocker locker(&m_mutex);
        m_objects = objects;
        emit objectsChanged(this->objects());
    });

    // Подключаем сигналы
    connect(m_camera, &CameraWorker::streamingStateChanged, this,
            [this, deviceIndex, deviceId](bool isStreaming) {
                QMutexLocker locker(&m_mutex);
                if (isStreaming) {
                    QVariantMap streamInfo;
                    streamInfo["name"] = m_availableDevices.at(deviceIndex);
                    streamInfo["address"] = QString("%1:%2").arg(m_host).arg(m_port + deviceIndex);
                    streamInfo["deviceId"] = deviceId;
                    m_activeStreams[deviceId] = streamInfo;
                } else {
                    m_activeStreams.remove(deviceId);
                }
                emit activeStreamsChanged();
                emit cameraStateChanged(deviceIndex, isStreaming);
            });

    connect(m_camera, &CameraWorker::errorOccurred, this, &GstStreamer::errorOccurred);
    connect(m_camera, &CameraWorker::destroyed, this, [this, deviceId]() {
        QMutexLocker locker(&m_mutex);
        m_activeStreams.remove(deviceId);
        emit activeStreamsChanged();
    });

    QMetaObject::invokeMethod(m_camera, "startStreaming", Qt::QueuedConnection);

    m_cameraThreads.append({nullptr, m_camera, deviceId});
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

            if (ct.worker) {
                QMetaObject::invokeMethod(ct.worker, "setYoloEnabled", Qt::BlockingQueuedConnection, Q_ARG(bool, false));
            }

            // Останавливаем worker (поток уничтожится в деструкторе worker)
            QMetaObject::invokeMethod(ct.worker, "stopStreaming", Qt::BlockingQueuedConnection);
            ct.worker->deleteLater();
            ct.worker = nullptr;

            // Удаляем из контейнеров
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

    // Stop all workers and let them clean up their own threads
    for (auto& ct : m_cameraThreads) {
        // Disconnect all signals from worker to avoid any callbacks during cleanup
        disconnect(ct.worker, nullptr, this, nullptr);

        if (ct.worker) {
            QMetaObject::invokeMethod(ct.worker, "setYoloEnabled", Qt::BlockingQueuedConnection, Q_ARG(bool, false));
        }

        // Stop the worker - thread will be cleaned up in worker's destructor
        QMetaObject::invokeMethod(ct.worker, "stopStreaming", Qt::BlockingQueuedConnection);
        ct.worker->deleteLater();
        ct.worker = nullptr;
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

    // Получаем текущий список камер
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();

    // Проверяем удаленные камеры
    for (int i = m_cameraThreads.size() - 1; i >= 0; --i) {
        bool found = false;
        QString currentId = m_cameraThreads[i].deviceId;

        // Ищем камеру в текущем списке
        for (const QCameraDevice& camera : cameras) {
            if (camera.id() == currentId) {
                found = true;
                break;
            }
        }

        if (!found) {
            // Камера удалена - останавливаем поток
            auto& ct = m_cameraThreads[i];

            // 1. Останавливаем worker
            if (ct.worker) {
                // Отключаем все сигналы
                disconnect(ct.worker, nullptr, this, nullptr);

                // Если worker в своем потоке - останавливаем правильно
                if (ct.worker->thread() && ct.worker->thread()->isRunning()) {
                    QMetaObject::invokeMethod(ct.worker, "stopStreaming", Qt::BlockingQueuedConnection);
                    ct.worker->deleteLater();
                    ct.worker = nullptr;
                } else {
                    ct.worker->deleteLater();
                    ct.worker = nullptr;
                }
            }

            // 2. Останавливаем поток
            if (ct.thread) {
                if (ct.thread->isRunning()) {
                    ct.thread->quit();
                    if (!ct.thread->wait(500)) {
                        qWarning() << "Thread termination timeout, forcing...";
                        ct.thread->terminate();
                        ct.thread->wait();
                    }
                }
                ct.thread->deleteLater();
            }

            // 3. Удаляем из контейнеров
            m_activeStreams.remove(currentId);
            m_cameraThreads.remove(i);

            // 4. Находим индекс камеры для уведомления UI
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
        locker.relock();
    }
}

// Обновляет список активных потоков
void GstStreamer::updateActiveStreams()
{
    // Не требует реализации - обновляется через сигналы
}

void GstStreamer::resetCamera()
{
    if (m_camera) {
        m_camera->stopStreaming();
        disconnect(m_camera, nullptr, this, nullptr);
        m_camera->deleteLater();
        m_camera = nullptr;
        m_objects.clear();
        emit objectsChanged(objects());
    }
}


void GstStreamer::captureCameraImage(int deviceIndex, const QString& savePath) {
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

    // Проверяем, есть ли уже worker для этой камеры
    CameraWorker* streamingWorker = nullptr;
    for (const auto& ct : m_cameraThreads) {
        if (ct.deviceId == deviceId) {
            streamingWorker = ct.worker;
            break;
        }
    }

    // Если камера уже используется для стриминга, используем её видео sink
    if (streamingWorker) {
        QMetaObject::invokeMethod(streamingWorker, "captureFrame", Qt::QueuedConnection,
                                  Q_ARG(QString, savePath.isEmpty() ? QDir::currentPath() : savePath));
    } else {
        // Создаем временный worker для захвата
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
}

void GstStreamer::setYoloEnabled(bool enabled) {
    qDebug() << "void GstStreamer::setYoloEnabled(bool enabled)";
    qDebug() << "m_yoloEnabled = " << m_yoloEnabled;
    qDebug() << "enabled = " << enabled;
    m_yoloEnabled = enabled;
    qDebug() << "m_yoloEnabled = " << m_yoloEnabled;
    if (m_camera) {
        m_camera->setYoloEnabled(enabled);
    }
    emit yoloEnabledChanged(enabled);
}

void GstStreamer::setYoloModelPath(const QString& path) {
    qDebug() << "void GstStreamer::setYoloModelPath(const QString& path)";
    qDebug() << "m_yoloModelPath = " << m_yoloModelPath;
    qDebug() << "path = " << path;
    if (path != "") {
        m_yoloModelPath = path;
        qDebug() << "m_yoloModelPath = " << m_yoloModelPath;
        if (m_camera) {
            m_camera->setYoloModelPath(path);
        }
        emit yoloModelPathChanged(path);
        emit objectsChanged(objects());
    }
}

void GstStreamer::handleModbusData(QModbusDataUnit::RegisterType table, int address, int size)
{
    qDebug() << "GstStreamer::handleModbusData - Table:" << table
             << "Address:" << address << "Size:" << size;

    if (table == QModbusDataUnit::HoldingRegisters) {
        // Получаем ЗАПИСАННОЕ значение
        quint16 value;
        if (deviceServer->getHoldingRegister(address, value)) {
            qDebug() << "Holding register" << address << "value:" << value;

            // Команда перезагрузки системы
            if (address == 100) {
                qDebug() << "Checking reboot command on register 100, value:" << value;
                if (value == 1) { // Команда "перезагрузить"
                    qDebug() << "!!! REBOOT COMMAND RECEIVED !!!";
                    deviceServer->rebootSystem();
                    // Сбрасываем регистр после выполнения команды
                    deviceServer->setHoldingRegister(address, 0);
                    qDebug() << "Holding register 100 reset to 0";
                }
            }
        } else {
            qDebug() << "Failed to get holding register" << address;
        }
    }
}

bool GstStreamer::modbusRunning() const
{
    return deviceServer && (deviceServer->state() == QModbusDevice::ConnectedState);
}

QString GstStreamer::modbusStatus() const
{
    if (!deviceServer) return "Not initialized";

    switch (deviceServer->state()) {
    case QModbusDevice::UnconnectedState: return "Stopped";
    case QModbusDevice::ConnectingState: return "Connecting";
    case QModbusDevice::ConnectedState: return "Running";
    case QModbusDevice::ClosingState: return "Closing";
    default: return "Unknown";
    }
}

QString GstStreamer::modbusHost() const
{
    return m_modbusHost;
}

void GstStreamer::setModbusHost(const QString &host)
{
    if (m_modbusHost != host) {
        m_modbusHost = host;
        emit modbusHostChanged();

        if (modbusRunning()) {
            deviceServer->disconnectDevice();
            deviceServer->connectDevice(m_modbusHost, m_modbusPort, 1);
        }
    }
}

int GstStreamer::modbusPort() const
{
    return m_modbusPort;
}

void GstStreamer::setModbusPort(int port)
{
    if (m_modbusPort != port) {
        m_modbusPort = port;
        emit modbusPortChanged();

        if (modbusRunning()) {
            deviceServer->disconnectDevice();
            deviceServer->connectDevice(m_modbusHost, m_modbusPort, 1);
        }
    }
}


void GstStreamer::toggleModbusServer()
{
    if (!deviceServer) {
        qWarning() << "Modbus server not initialized!";
        return;
    }

    if (modbusRunning()) {
        qDebug() << "Stopping Modbus server...";
        deviceServer->disconnectDevice();
        qDebug() << "Modbus server stopped";
    } else {
        qDebug() << "Starting Modbus server on" << m_modbusHost << ":" << m_modbusPort;
        if (deviceServer->connectDevice(m_modbusHost, m_modbusPort, 1)) {
            qDebug() << "Modbus server started successfully";
        } else {
            qWarning() << "Failed to start Modbus server!";
            QString error = "Failed to start Modbus server on " + m_modbusHost + ":" + QString::number(m_modbusPort);
            emit errorOccurred(error);
        }
    }
    emit modbusStatusChanged();
}

void GstStreamer::handleModbusStateChanged(int state)
{
    qDebug() << "Modbus state changed to:" << state;
    emit modbusStatusChanged();
}

void GstStreamer::handleModbusError(QModbusDevice::Error error)
{
    qDebug() << "Modbus error occurred:" << error;
    if (error != QModbusDevice::NoError) {
        QString errorMsg = QString("Modbus error: %1").arg(error);
        qWarning() << errorMsg;
        emit errorOccurred(errorMsg);
    }
    emit modbusStatusChanged();
}

void GstStreamer::startStreamingFromModbus(int deviceIndex)
{
    QMutexLocker locker(&m_modbusMutex);
    startStreaming(deviceIndex);
}

void GstStreamer::stopStreamingFromModbus(int deviceIndex)
{
    QMutexLocker locker(&m_modbusMutex);
    stopStreaming(deviceIndex);
}

void GstStreamer::autoConnectModbus()
{
    if (!deviceServer) {
        qWarning() << "Modbus server not initialized for auto-connect!";
        return;
    }

    if (!modbusRunning()) {
        qDebug() << "Auto-connecting Modbus server on" << m_modbusHost << ":" << m_modbusPort;
        if (deviceServer->connectDevice(m_modbusHost, m_modbusPort, 1)) {
            qDebug() << "Modbus server auto-connected successfully";
        } else {
            qWarning() << "Failed to auto-connect Modbus server!";
            QString error = "Failed to auto-connect Modbus server on " +
                            m_modbusHost + ":" + QString::number(m_modbusPort);
            emit errorOccurred(error);

            // Опционально: попробовать снова через некоторое время
            // QTimer::singleShot(10000, this, &GstStreamer::autoConnectModbus);
        }
        emit modbusStatusChanged();
    } else {
        qDebug() << "Modbus server already running, skipping auto-connect";
    }
}
