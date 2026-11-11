#include "serialmanager.h"
#include <QThread>
#include <QSerialPortInfo>

SerialManager::SerialManager(QObject *parent)
    : QObject(parent)
    , m_serial(new QSerialPort(this))
    , m_bootMode(MODE_RTS_HIGH_DTR_LOW)
    , m_rtsLevel(false)
    , m_dtrLevel(true)
{
    // 连接错误信号
    connect(m_serial, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError error) {
        if (error != QSerialPort::NoError) {
            emit errorOccurred(m_serial->errorString());
        }
    });
}

SerialManager::~SerialManager()
{
    close();
}

QStringList SerialManager::availablePorts() const
{
    QStringList ports;
    const auto infos = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &info : infos) {
        ports.append(info.portName());
    }
    return ports;
}

bool SerialManager::open(const QString &portName, qint32 baudRate)
{
    if (m_serial->isOpen()) {
        close();
    }

    m_serial->setPortName(portName);
    m_serial->setBaudRate(baudRate);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::EvenParity);  // STM32 bootloader 使用偶校验
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        emit errorOccurred(m_serial->errorString());
        return false;
    }

    // 默认RTS=LOW, DTR=HIGH (正常运行状态)
    setRts(false);
    setDtr(true);

    emit connected();
    return true;
}

void SerialManager::close()
{
    if (m_serial->isOpen()) {
        // 恢复到正常运行状态
        setRts(false);
        setDtr(true);
        m_serial->close();
        emit disconnected();
    }
}

bool SerialManager::isOpen() const
{
    return m_serial->isOpen();
}

qint64 SerialManager::write(const QByteArray &data)
{
    if (!m_serial->isOpen()) {
        return -1;
    }
    return m_serial->write(data);
}

bool SerialManager::waitForReadyRead(int msecs)
{
    return m_serial->waitForReadyRead(msecs);
}

QByteArray SerialManager::readAll()
{
    return m_serial->readAll();
}

qint64 SerialManager::bytesAvailable() const
{
    if (!m_serial->isOpen()) {
        return 0;
    }
    return m_serial->bytesAvailable();
}

void SerialManager::clearBuffers()
{
    m_serial->clear();
}

bool SerialManager::enterBootloader()
{
    if (!m_serial->isOpen()) {
        return false;
    }

    // 如果是手动模式，不执行任何操作
    if (m_bootMode == MODE_MANUAL) {
        return true;  // 返回true，让用户手动控制
    }

    return enterBootloaderWithMode();
}

bool SerialManager::enterBootloaderWithMode()
{
    switch (m_bootMode) {
    case MODE_RTS_HIGH_DTR_LOW:
        // FlyMCU 模式: RTS=HIGH复位, DTR=LOW进ISP
        emit logMessage("RTS置高,复位");
        emit logMessage("DTR置低,选择进入BootLoader");
        setRts(true);
        setDtr(false);
        emit logMessage("...延时100毫秒");
        delayMs(100);

        emit logMessage("RTS变低,释放复位");
        emit logMessage("DTR维持低");
        setRts(false);
        emit logMessage("...延时200毫秒,等待芯片启动");
        delayMs(200);
        break;

    case MODE_RTS_LOW_DTR_HIGH:
        // 反相模式
        emit logMessage("RTS置低,复位");
        emit logMessage("DTR置高,选择进入BootLoader");
        setRts(false);
        setDtr(true);
        emit logMessage("...延时100毫秒");
        delayMs(100);
        emit logMessage("RTS变高,释放复位");
        emit logMessage("...延时200毫秒");
        delayMs(200);
        break;

    case MODE_RTS_HIGH_DTR_HIGH:
        emit logMessage("RTS/DTR置高,复位");
        setRts(true);
        setDtr(true);
        delayMs(100);
        emit logMessage("释放复位");
        setRts(false);
        delayMs(200);
        break;

    case MODE_RTS_LOW_DTR_LOW:
        emit logMessage("RTS/DTR置低,复位");
        setRts(false);
        setDtr(false);
        delayMs(100);
        emit logMessage("释放复位");
        setRts(true);
        delayMs(200);
        break;

    case MODE_RTS_ONLY:
        emit logMessage("RTS控制复位");
        setRts(true);
        delayMs(100);
        setRts(false);
        delayMs(200);
        break;

    case MODE_DTR_ONLY:
        emit logMessage("DTR控制复位");
        setDtr(false);
        delayMs(100);
        setDtr(true);
        delayMs(200);
        break;

    case MODE_MANUAL:
    default:
        emit logMessage("手动模式,跳过自动控制");
        break;
    }

    // 清空缓冲区
    clearBuffers();
    delayMs(20);
    clearBuffers();

    return true;
}

bool SerialManager::enterNormalMode()
{
    if (!m_serial->isOpen()) {
        return false;
    }

    // 拉低RTS → BOOT0 = LOW (选择正常运行模式)
    setRts(false);
    delayMs(10);

    // 产生复位脉冲
    setDtr(false);
    delayMs(10);
    setDtr(true);
    delayMs(10);
    setDtr(false);
    delayMs(50);

    return true;
}

bool SerialManager::resetOnly()
{
    if (!m_serial->isOpen()) {
        return false;
    }

    // 仅产生复位脉冲，不改变BOOT0
    setDtr(false);
    delayMs(10);
    setDtr(true);
    delayMs(10);
    setDtr(false);
    delayMs(50);

    return true;
}

void SerialManager::setRts(bool level)
{
    m_rtsLevel = level;
    m_serial->setRequestToSend(level);
    emit rtsChanged(level);
}

void SerialManager::setDtr(bool level)
{
    m_dtrLevel = level;
    m_serial->setDataTerminalReady(level);
    emit dtrChanged(level);
}

void SerialManager::delayMs(int ms)
{
    QThread::msleep(ms);
}
