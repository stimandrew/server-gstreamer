#pragma once

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <QMessageBox>
#include "modbusserver.h"

class GstStreamer;

class ModbusDeviceServer : public ModbusServer
{
    Q_OBJECT
public:
    explicit ModbusDeviceServer(QObject *parent = nullptr);
    void rebootSystem();

    // Установка ссылки на GstStreamer для управления камерами
    void setStreamer(GstStreamer* streamer);

private:
    GstStreamer* m_streamer = nullptr; // Ссылка на объект управления камерами

private slots:
    void handleCoilWritten(int address, bool value);
};

