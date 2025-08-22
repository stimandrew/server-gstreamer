#include "modbusdeviceserver.h"
#include "gst_server.h"

ModbusDeviceServer::ModbusDeviceServer(QObject *parent) : ModbusServer(parent)
{
    QTimer *updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &ModbusDeviceServer::updateCameraStates);
    updateTimer->start(10000);
    // Подключаем обработчик изменений Coils
    connect(this, &ModbusServer::dataWritten, this,
            [this](QModbusDataUnit::RegisterType table, int address, int size) {
                if (table == QModbusDataUnit::Coils) {
                    for (int i = address; i < address + size; ++i) {
                        bool value;
                        if (getCoil(i, value)) {
                            handleCoilWritten(i, value);
                        }
                    }
                }
            });
}

void ModbusDeviceServer::rebootSystem() {
    // Создаем интерфейс для работы с systemd-logind
    QDBusInterface loginInterface(
        "org.freedesktop.login1",                     // DBus-сервис
        "/org/freedesktop/login1",                     // Путь к объекту
        "org.freedesktop.login1.Manager",              // Интерфейс
        QDBusConnection::systemBus()                   // Системная шина DBus
        );

    // Проверяем, доступен ли интерфейс
    if (!loginInterface.isValid()) {
        QMessageBox::critical(nullptr, "DBus Error",
                              "Cannot connect to systemd-logind:\n" +
                                  QDBusConnection::systemBus().lastError().message());
        return;
    }

    // Вызываем метод Reboot (false = без подтверждения)
    QDBusReply<void> reply = loginInterface.call("Reboot", false);

    // Обработка ошибок
    if (!reply.isValid()) {
        QMessageBox::critical(nullptr, "Reboot Failed",
                              "DBus call failed:\n" + reply.error().message());
    }
}

void ModbusDeviceServer::setStreamer(GstStreamer* streamer)
{
    m_streamer = streamer;
}

void ModbusDeviceServer::handleCoilWritten(int address, bool value)
{
    qDebug() << "Coil" << address << "set to:" << value;

    if (!m_streamer) {
        qWarning() << "GstStreamer not set! Cannot control cameras.";
        return;
    }

    // Управление камерами по адресам Coils:
    // Coil 0: Камера 1
    // Coil 1: Камера 2
    // Coil 2: Камера 3
    // и т.д.

    int cameraIndex = address;

    if (cameraIndex >= 0 && cameraIndex < 10) { // Поддерживаем до 10 камер
        if (value) {
            // Включение камеры
            qDebug() << "Starting camera" << (cameraIndex + 1);
            QMetaObject::invokeMethod(m_streamer, "startStreaming",
                                      Qt::QueuedConnection, Q_ARG(int, cameraIndex));
        } else {
            // Выключение камеры
            qDebug() << "Stopping camera" << (cameraIndex + 1);
            QMetaObject::invokeMethod(m_streamer, "stopStreaming",
                                      Qt::QueuedConnection, Q_ARG(int, cameraIndex));
        }
    }
}

void ModbusDeviceServer::updateCameraStates()
{
    if (!m_streamer) return;

    // Обновляем состояние катушек в соответствии с состоянием камер
    for (int i = 0; i < 10; i++) { // Поддерживаем до 10 камер
        bool isCameraActive = m_streamer->isCameraActive(i);
        setCoil(i, isCameraActive);

        qDebug() << "Camera" << i << "state:" << isCameraActive << "(coil" << i << "set to" << isCameraActive << ")";
    }
}
