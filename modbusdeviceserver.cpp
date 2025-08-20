#include "modbusdeviceserver.h"

ModbusDeviceServer::ModbusDeviceServer(QObject *parent) : ModbusServer(parent)
{

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
