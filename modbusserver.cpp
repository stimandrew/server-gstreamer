#include "modbusserver.h"

ModbusServer::ModbusServer(QObject *parent) : QObject(parent)
{
}

ModbusServer::~ModbusServer()
{
    if (m_modbusDevice) {
        m_modbusDevice->disconnectDevice();
        delete m_modbusDevice;
    }
}

bool ModbusServer::connectDevice(const QString &host, int port, int serverAddress)
{
    // Очистка предыдущего подключения
    if (m_modbusDevice) {
        m_modbusDevice->disconnectDevice();
        delete m_modbusDevice;
        m_modbusDevice = nullptr;
    }

    // Создаем TCP сервер
    m_modbusDevice = new QModbusTcpServer(this);

    // Устанавливаем параметры подключения
    m_modbusDevice->setConnectionParameter(QModbusDevice::NetworkPortParameter, port);
    m_modbusDevice->setConnectionParameter(QModbusDevice::NetworkAddressParameter, host);

    if (!m_modbusDevice)
        return false;

    connect(m_modbusDevice, &QModbusServer::dataWritten,
            this, &ModbusServer::handleDataWritten);

    // Настройка карты регистров
    QModbusDataUnitMap reg;
    reg.insert(QModbusDataUnit::Coils, { QModbusDataUnit::Coils, 0, 10 });
    reg.insert(QModbusDataUnit::DiscreteInputs, { QModbusDataUnit::DiscreteInputs, 0, 10 });
    reg.insert(QModbusDataUnit::InputRegisters, { QModbusDataUnit::InputRegisters, 0, 10 });
    reg.insert(QModbusDataUnit::HoldingRegisters, { QModbusDataUnit::HoldingRegisters, 0, 101 });
    m_modbusDevice->setMap(reg);

    // Подключаем сигналы
    connect(m_modbusDevice, &QModbusServer::dataWritten, this, &ModbusServer::dataWritten);
    connect(m_modbusDevice, &QModbusServer::stateChanged, this, &ModbusServer::stateChanged);
    connect(m_modbusDevice, &QModbusServer::errorOccurred, this, &ModbusServer::errorOccurred);

    // Устанавливаем адрес сервера и подключаемся
    m_modbusDevice->setServerAddress(serverAddress);
    return m_modbusDevice->connectDevice();
}

void ModbusServer::disconnectDevice()
{
    if (m_modbusDevice)
        m_modbusDevice->disconnectDevice();
}

bool ModbusServer::isConnected() const
{
    return m_modbusDevice && (m_modbusDevice->state() == QModbusDevice::ConnectedState);
}

void ModbusServer::setCoil(int address, bool value)
{
    if (m_modbusDevice)
        m_modbusDevice->setData(QModbusDataUnit::Coils, quint16(address), value);
}

void ModbusServer::setDiscreteInput(int address, bool value)
{
    if (m_modbusDevice)
        m_modbusDevice->setData(QModbusDataUnit::DiscreteInputs, quint16(address), value);
}

void ModbusServer::setInputRegister(int address, quint16 value)
{
    if (m_modbusDevice)
        m_modbusDevice->setData(QModbusDataUnit::InputRegisters, quint16(address), value);
}

void ModbusServer::setHoldingRegister(int address, quint16 value)
{
    if (m_modbusDevice) {
        m_modbusDevice->setData(QModbusDataUnit::HoldingRegisters, quint16(address), value);
        m_holdingRegisters[address] = value;
    }
}

bool ModbusServer::getCoil(int address, bool &value) const
{
    if (!m_modbusDevice)
        return false;
    quint16 val;
    bool success = m_modbusDevice->data(QModbusDataUnit::Coils, quint16(address), &val);
    value = val;
    return success;
}

bool ModbusServer::getDiscreteInput(int address, bool &value) const
{
    if (!m_modbusDevice)
        return false;
    quint16 val;
    bool success = m_modbusDevice->data(QModbusDataUnit::DiscreteInputs, quint16(address), &val);
    value = val;
    return success;
}

bool ModbusServer::getInputRegister(int address, quint16 &value) const
{
    if (!m_modbusDevice)
        return false;
    return m_modbusDevice->data(QModbusDataUnit::InputRegisters, quint16(address), &value);
}

bool ModbusServer::getHoldingRegister(int address, quint16 &value) const
{
    if (m_holdingRegisters.contains(address)) {
        value = m_holdingRegisters[address];
        return true;
    }
    return false;
}

void ModbusServer::setListenOnly(bool listenOnly)
{
    if (m_modbusDevice)
        m_modbusDevice->setValue(QModbusServer::ListenOnlyMode, listenOnly);
}

void ModbusServer::setDeviceBusy(bool busy)
{
    if (m_modbusDevice)
        m_modbusDevice->setValue(QModbusServer::DeviceBusy, busy ? 0xffff : 0x0000);
}

QString ModbusServer::serverHost() const
{
    if (m_modbusDevice) {
        return m_modbusDevice->connectionParameter(QModbusDevice::NetworkAddressParameter).toString();
    }
    return QString();
}

int ModbusServer::serverPort() const
{
    if (m_modbusDevice) {
        return m_modbusDevice->connectionParameter(QModbusDevice::NetworkPortParameter).toInt();
    }
    return 0;
}

int ModbusServer::serverAddress() const
{
    if (m_modbusDevice) {
        return m_modbusDevice->serverAddress();
    }
    return 0;
}

QModbusDevice::State ModbusServer::state() const
{
    if (m_modbusDevice) {
        return m_modbusDevice->state();
    }
    return QModbusDevice::UnconnectedState;
}

void ModbusServer::rebootSystem()
{
    CommandsModbus::rebootSystem();
}

void ModbusServer::handleDataWritten(QModbusDataUnit::RegisterType table, int address, int size)
{
    qDebug() << "ModbusServer::handleDataWritten - Table:" << table
             << "Address:" << address << "Size:" << size;

    if (!m_modbusDevice) {
        qDebug() << "Modbus device is null!";
        return;
    }

    if (table == QModbusDataUnit::HoldingRegisters) {
        for (int i = address; i < address + size; ++i) {
            quint16 value;
            if (m_modbusDevice->data(QModbusDataUnit::HoldingRegisters, i, &value)) {
                m_holdingRegisters[i] = value;
                qDebug() << "Holding register" << i << "set to:" << value;

                // Немедленно эмитируем сигнал для каждого измененного регистра
                emit dataWritten(table, i, 1);
            } else {
                qDebug() << "Failed to get data for holding register" << i;
            }
        }
    }
}

