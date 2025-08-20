#pragma once

#include <QObject>
#include <QModbusServer>
#include <QModbusDataUnit>
#include <QModbusTcpServer>
#include <QUrl>
#include "commandsmodbus.h"

class ModbusServer : public QObject
{
    Q_OBJECT

public:
    explicit ModbusServer(QObject *parent = nullptr);
    ~ModbusServer();

    bool connectDevice(const QString &host, int port, int serverAddress);
    void disconnectDevice();
    bool isConnected() const;
    QString serverHost() const;
    int serverPort() const;
    int serverAddress() const;

    // Методы работы с регистрами
    void setCoil(int address, bool value);
    void setDiscreteInput(int address, bool value);
    void setInputRegister(int address, quint16 value);
    void setHoldingRegister(int address, quint16 value);

    bool getCoil(int address, bool &value) const;
    bool getDiscreteInput(int address, bool &value) const;
    bool getInputRegister(int address, quint16 &value) const;
    bool getHoldingRegister(int address, quint16 &value) const;

    // Управление сервером
    void setListenOnly(bool listenOnly);
    void setDeviceBusy(bool busy);
    QModbusDevice::State state() const;
    void rebootSystem();

signals:
    void dataWritten(QModbusDataUnit::RegisterType table, int address, int size);
    void stateChanged(int state);
    void errorOccurred(QModbusDevice::Error error);

private slots:
    void handleDataWritten(QModbusDataUnit::RegisterType table, int address, int size);

private:
    QModbusTcpServer *m_modbusDevice = nullptr;
    QMap<int, quint16> m_holdingRegisters;
};
