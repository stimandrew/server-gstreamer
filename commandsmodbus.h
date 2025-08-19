#pragma once

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDebug>
#include <QMessageBox>

class CommandsModbus
{
public:
    CommandsModbus();
    static void rebootSystem();
};

