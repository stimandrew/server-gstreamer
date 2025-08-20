#pragma once

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <QMessageBox>
#include "modbusserver.h"

class ModbusDeviceServer : public ModbusServer
{
    Q_OBJECT
public:
    explicit ModbusDeviceServer(QObject *parent = nullptr);
    void rebootSystem();
};

