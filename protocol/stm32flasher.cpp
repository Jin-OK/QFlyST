#include "stm32flasher.h"
#include <QThread>
#include <QDebug>
#include <QProgressBar>
#include <QApplication>
#include <QElapsedTimer>

Stm32Flasher::Stm32Flasher(SerialManager *serial, QObject *parent)
    : QObject(parent)
    , m_serial(serial)
    , m_progressBar(nullptr)
    , m_receiveBuffer()
    , m_stopFlag(nullptr)
{
}

Stm32Flasher::~Stm32Flasher()
{
}

void Stm32Flasher::log(const QString &msg)
{
    emit logMessage(msg);
    qDebug() << msg;
}

bool Stm32Flasher::receiveAck()
{
    uint8_t ack;
    if (!receiveByte(&ack)) {
        m_lastError = "等待ACK超时";
        return false;
    }

    // 注意：只读取一个字节，不要清空缓冲区
    // 因为后续数据可能已经和ACK一起返回了

    if (ack == ACK) {
        return true;
    } else if (ack == NACK) {
        m_lastError = "收到NACK";
        return false;
    } else {
        m_lastError = QString("未知响应: 0x%1").arg(ack, 2, 16, QChar('0'));
        return false;
    }
}

bool Stm32Flasher::receiveByte(uint8_t *byte)
{
    // 先从缓冲区取
    if (!m_receiveBuffer.isEmpty()) {
        *byte = static_cast<uint8_t>(m_receiveBuffer.at(0));
        m_receiveBuffer.remove(0, 1);
        return true;
    }

    // 非阻塞方式：使用短超时循环，减少UI卡顿
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < 2000) {  // 总超时2秒
        // 检查停止标志
        if (m_stopFlag && *m_stopFlag == false) {
            m_lastError = "用户取消";
            return false;
        }

        // 处理UI事件，保持响应
        QApplication::processEvents();

        // 检查是否有数据可读
        if (m_serial->bytesAvailable() > 0) {
            QByteArray data = m_serial->readAll();
            m_receiveBuffer.append(data);

            if (!m_receiveBuffer.isEmpty()) {
                *byte = static_cast<uint8_t>(m_receiveBuffer.at(0));
                m_receiveBuffer.remove(0, 1);
                return true;
            }
        }

        // 短暂休眠，避免CPU占用过高
        QThread::msleep(5);
    }

    return false;
}

bool Stm32Flasher::sendCommand(uint8_t cmd)
{
    QByteArray data;
    data.append(static_cast<char>(cmd));
    data.append(static_cast<char>(~cmd));  // 校验码 = 取反

    m_serial->write(data);

    return receiveAck();
}

bool Stm32Flasher::sendAddress(quint32 addr)
{
    QByteArray data;
    data.append(static_cast<char>((addr >> 24) & 0xFF));
    data.append(static_cast<char>((addr >> 16) & 0xFF));
    data.append(static_cast<char>((addr >> 8) & 0xFF));
    data.append(static_cast<char>(addr & 0xFF));

    // 计算校验码
    uint8_t checksum = ((addr >> 24) & 0xFF) ^
                       ((addr >> 16) & 0xFF) ^
                       ((addr >> 8) & 0xFF) ^
                       (addr & 0xFF);
    data.append(static_cast<char>(checksum));

    m_serial->write(data);
    return receiveAck();
}

bool Stm32Flasher::sendData(const QByteArray &data)
{
    m_serial->write(data);
    return receiveAck();
}

uint8_t Stm32Flasher::calcChecksum(const QByteArray &data)
{
    uint8_t sum = 0;
    for (int i = 0; i < data.size(); ++i) {
        sum ^= static_cast<uint8_t>(data[i]);
    }
    return sum;
}

bool Stm32Flasher::initConnection(bool *stopFlag)
{
    m_stopFlag = stopFlag;  // 保存停止标志

    // 清空缓冲区
    m_serial->clearBuffers();
    m_receiveBuffer.clear();

    // 发送同步字节 0x7F，最多尝试5次
    for (int retry = 0; retry < 5; ++retry) {
        // 检查停止标志 (false表示停止)
        if (m_stopFlag && *m_stopFlag == false) {
            m_lastError = "用户取消";
            return false;
        }

        if (retry > 0) {
            // 使用循环代替长sleep，保持UI响应
            for (int i = 0; i < 20; ++i) {
                if (m_stopFlag && *m_stopFlag == false) {
                    m_lastError = "用户取消";
                    return false;
                }
                QThread::msleep(10);
                QApplication::processEvents();
            }
            m_serial->clearBuffers();
            m_receiveBuffer.clear();
        }

        uint8_t sync = CMD_INIT;
        m_serial->write(QByteArray(reinterpret_cast<const char*>(&sync), 1));

        // 等待响应
        QElapsedTimer timer;
        timer.start();

        while (timer.elapsed() < 500) {  // 每次尝试最多等待500ms
            // 检查停止标志
            if (m_stopFlag && *m_stopFlag == false) {
                m_lastError = "用户取消";
                return false;
            }

            // 处理UI事件
            QApplication::processEvents();

            if (m_serial->bytesAvailable() > 0) {
                QByteArray data = m_serial->readAll();
                for (int j = 0; j < data.size(); ++j) {
                    uint8_t ack = static_cast<uint8_t>(data[j]);
                    if (ack == ACK) {
                        m_serial->clearBuffers();
                        m_receiveBuffer.clear();
                        return true;
                    } else if (ack == NACK) {
                        // 收到NACK，继续下一次重试
                        break;
                    }
                }
            }

            QThread::msleep(5);
        }
    }

    m_lastError = "无法与Bootloader建立连接";
    return false;
}

bool Stm32Flasher::getChipInfo(uint8_t *version, uint16_t *chipId)
{
    // 清空接收缓冲区
    m_receiveBuffer.clear();
    m_serial->clearBuffers();

    // 发送GET命令 (0x00 + 校验 0xFF)
    if (!sendCommand(CMD_GET)) {
        return false;
    }

    // 读取响应长度 (字节数N)
    uint8_t len;
    if (!receiveByte(&len)) {
        m_lastError = "读取版本信息失败";
        return false;
    }

    // 读取Bootloader版本
    if (!receiveByte(version)) {
        m_lastError = "读取版本信息失败";
        return false;
    }

    // 读取命令列表 (N个字节)
    for (int i = 0; i < len; ++i) {
        uint8_t cmd;
        if (!receiveByte(&cmd)) {
            break;
        }
    }

    // 读取结束的ACK (0x79)
    uint8_t endAck;
    receiveByte(&endAck);

    // 发送GET_ID命令获取芯片ID
    if (!sendCommand(CMD_GET_ID)) {
        return false;
    }

    // 读取响应长度
    if (!receiveByte(&len)) {
        m_lastError = "读取芯片ID失败";
        return false;
    }

    // 读取芯片ID (2字节) - 大端序，高字节在前
    uint8_t id1, id2;
    if (!receiveByte(&id1) || !receiveByte(&id2)) {
        m_lastError = "读取芯片ID失败";
        return false;
    }

    // 大端序：id1是高字节，id2是低字节
    *chipId = (static_cast<uint16_t>(id1) << 8) | id2;

    // 读取校验字节
    uint8_t dummy;
    receiveByte(&dummy);

    // 显示芯片信息
    log(QString("芯片内BootLoader版本号：%1.%2").arg(*version / 16).arg(*version % 16));
    log(QString("芯片PID：%1").arg(*chipId, 8, 16, QChar('0')).toUpper());

    // 获取并显示芯片详细信息
    QString flashSize, sramSize;
    logChipInfo(*chipId, flashSize, sramSize);
    log(QString("芯片FLASH容量: %1").arg(flashSize));
    log(QString("芯片SRAM容量: %1").arg(sramSize));

    // 读取芯片唯一序列号
    QString chipUid;
    if (readChipUID(chipUid)) {
        log(QString("芯片唯一序列号: %1").arg(chipUid));
    }

    return true;
}

void Stm32Flasher::logChipInfo(uint16_t chipId, QString &flashSize, QString &sramSize)
{
    QString chipName;

    switch (chipId) {
    // STM32F0 系列
    case 0x0440:
        chipName = "STM32F051xx";
        flashSize = "64KB";
        sramSize = "8KB";
        break;
    case 0x0444:
        chipName = "STM32F030x4/F031x4";
        flashSize = "16KB";
        sramSize = "4KB";
        break;
    case 0x0445:
        chipName = "STM32F042xx";
        flashSize = "32KB";
        sramSize = "6KB";
        break;
    case 0x0448:
        chipName = "STM32F030x8";
        flashSize = "64KB";
        sramSize = "8KB";
        break;

    // STM32F1 系列
    case 0x0401:
        chipName = "STM32F103xx (Bootloader v1.1)";
        flashSize = "64KB";
        sramSize = "20KB";
        break;
    case 0x0410:
        chipName = "STM32F10xxx Medium-density";
        flashSize = "128KB";
        sramSize = "20KB";
        break;
    case 0x0414:
        chipName = "STM32F10xxx High-density";
        // High-density 的 Flash 大小需要读取寄存器确定
        // 这里先显示范围，实际使用时会通过读取Flash大小寄存器获取准确值
        flashSize = "256KB 或 512KB (需读取Flash大小寄存器)";
        sramSize = "48KB";  // High-density 固定为 48KB
        break;
    case 0x0418:
        chipName = "STM32F10xxx Connectivity";
        flashSize = "256KB";
        sramSize = "64KB";
        break;
    case 0x0420:
        chipName = "STM32F10xxx Medium-density value line";
        flashSize = "128KB";
        sramSize = "8KB";
        break;
    case 0x0428:
        chipName = "STM32F10xxx High-density value line";
        flashSize = "512KB";
        sramSize = "64KB";
        break;
    case 0x0430:
        chipName = "STM32F10xxx XL-density";
        flashSize = "1MB";
        sramSize = "80KB";
        break;
    case 0x0438:
        chipName = "STM32F10xxx Medium-density+ USB";
        flashSize = "128KB";
        sramSize = "20KB";
        break;

    // STM32F2 系列
    case 0x0411:
        chipName = "STM32F2xxx";
        flashSize = "512KB/1MB";
        sramSize = "128KB";
        break;

    // STM32F3 系列
    case 0x0422:
        chipName = "STM32F302xB/C/303xB/C";
        flashSize = "128KB/256KB";
        sramSize = "32KB/40KB";
        break;
    case 0x0432:
        chipName = "STM32F373xx/378xx";
        flashSize = "256KB";
        sramSize = "32KB";
        break;
    case 0x0439:
        chipName = "STM32F302x4/6/303x4/6";
        flashSize = "32KB/64KB";
        sramSize = "10KB/12KB";
        break;
    case 0x043B:
        chipName = "STM32F302x4/303x4";
        flashSize = "64KB";
        sramSize = "16KB";
        break;
    case 0x0446:
        chipName = "STM32F303xD/E/334xx/328xx";
        flashSize = "256KB/512KB";
        sramSize = "64KB";
        break;

    // STM32F4 系列
    case 0x0413:
        chipName = "STM32F40xxx/41xxx";
        flashSize = "512KB/1MB";
        sramSize = "192KB";
        break;
    case 0x0419:
        chipName = "STM32F42xxx/43xxx";
        flashSize = "1MB/2MB";
        sramSize = "256KB";
        break;
    case 0x0421:
        chipName = "STM32F446xx";
        flashSize = "256KB/512KB";
        sramSize = "128KB";
        break;
    case 0x0423:
        chipName = "STM32F401xB/C";
        flashSize = "128KB/256KB";
        sramSize = "64KB/96KB";
        break;
    case 0x0431:
        chipName = "STM32F411xC/E";
        flashSize = "256KB/512KB";
        sramSize = "128KB";
        break;
    case 0x0433:
        chipName = "STM32F401xD/E";
        flashSize = "384KB/512KB";
        sramSize = "96KB";
        break;
    case 0x0434:
        chipName = "STM32F469xx/479xx";
        flashSize = "1MB/2MB";
        sramSize = "320KB";
        break;
    case 0x0458:
        chipName = "STM32F446xx";
        flashSize = "256KB";
        sramSize = "128KB";
        break;

    // STM32F7 系列
    case 0x0449:
        chipName = "STM32F74xxx/75xxx";
        flashSize = "512KB/1MB";
        sramSize = "320KB";
        break;
    case 0x0451:
        chipName = "STM32F76xxx/77xxx";
        flashSize = "1MB/2MB";
        sramSize = "512KB";
        break;

    // STM32L0 系列
    case 0x0412:
        chipName = "STM32L05xxx/06xxx";
        flashSize = "64KB";
        sramSize = "8KB";
        break;
    case 0x0417:
        chipName = "STM32L03xxx/04xxx";
        flashSize = "32KB";
        sramSize = "8KB";
        break;
    case 0x0447:
        chipName = "STM32L07xxx/08xxx";
        flashSize = "128KB/192KB";
        sramSize = "20KB";
        break;

    // STM32L4 系列
    case 0x0461:
        chipName = "STM32L496xx/4A6xx";
        flashSize = "512KB/1MB";
        sramSize = "320KB";
        break;
    case 0x0462:
        chipName = "STM32L45xxx/46xxx";
        flashSize = "256KB/512KB";
        sramSize = "160KB";
        break;
    case 0x0464:
        chipName = "STM32L412xx/422xx";
        flashSize = "128KB";
        sramSize = "40KB";
        break;

    default:
        chipName = QString("未知芯片 (PID: 0x%1)").arg(chipId, 4, 16, QChar('0')).toUpper();
        flashSize = "未知";
        sramSize = "未知";
        break;
    }

    log(QString("芯片型号: %1").arg(chipName));
    // Flash和SRAM容量由调用者输出
}

bool Stm32Flasher::eraseChip()
{
    log("开始擦除芯片...");
    emit statusChanged("正在擦除...");
    emit progressChanged(0);

    bool success;

    // 尝试标准擦除
    if (sendCommand(CMD_ERASE)) {
        // 标准擦除：发送0xFF表示全部擦除
        QByteArray data;
        data.append(static_cast<char>(0xFF));
        data.append(static_cast<char>(0x00));  // 校验码 = ~0xFF = 0x00
        success = sendData(data);
    } else {
        // 标准擦除失败，尝试扩展擦除
        if (sendCommand(CMD_EXT_ERASE)) {
            // 扩展擦除：发送0xFFFF + 校验
            QByteArray data;
            data.append(static_cast<char>(0xFF));
            data.append(static_cast<char>(0xFF));
            data.append(static_cast<char>(0x00));  // 校验码
            success = sendData(data);
        } else {
            return false;
        }
    }

    if (success) {
        emit progressChanged(100);
        log("擦除完成");
        emit statusChanged("擦除完成");
    } else {
        log("擦除失败");
    }

    return success;
}

bool Stm32Flasher::writeFirmware(const QByteArray &data, quint32 startAddress)
{
    log("开始写入固件...");
    emit statusChanged("正在写入...");

    const int pageSize = 256;  // STM32每次最多写入256字节
    int totalBytes = data.size();
    int writtenBytes = 0;

    while (writtenBytes < totalBytes) {
        // 计算本次写入的字节数
        int bytesToWrite = qMin(pageSize, totalBytes - writtenBytes);

        // 发送写命令
        if (!sendCommand(CMD_WRITE)) {
            return false;
        }

        // 发送起始地址
        quint32 addr = startAddress + writtenBytes;
        if (!sendAddress(addr)) {
            return false;
        }

        // 构造数据包：N-1 + 数据 + 校验码
        QByteArray packet;
        packet.append(static_cast<char>(bytesToWrite - 1));

        for (int i = 0; i < bytesToWrite; ++i) {
            packet.append(data[writtenBytes + i]);
        }

        // 计算校验码
        uint8_t checksum = static_cast<uint8_t>(bytesToWrite - 1);
        for (int i = 0; i < bytesToWrite; ++i) {
            checksum ^= static_cast<uint8_t>(data[writtenBytes + i]);
        }
        packet.append(static_cast<char>(checksum));

        // 发送数据
        if (!sendData(packet)) {
            log(QString("写入失败，地址: 0x%1").arg(addr, 8, 16, QChar('0')));
            return false;
        }

        writtenBytes += bytesToWrite;

        // 更新进度
        int percent = (writtenBytes * 100) / totalBytes;
        emit progressChanged(percent);
        if (m_progressBar) {
            m_progressBar->setValue(percent);
        }
    }

    log(QString("写入完成，共 %1 字节").arg(writtenBytes));
    emit statusChanged("写入完成");
    return true;
}

bool Stm32Flasher::writeFirmwareWithProgress(const QByteArray &data, quint32 startAddress, int progressMin, int progressMax, QObject *progressReceiver)
{
    Q_UNUSED(progressReceiver);  // 不使用这个参数，直接通过信号更新
    log("开始写入固件...");
    emit statusChanged("正在写入...");

    const int pageSize = 256;
    int totalBytes = data.size();
    int writtenBytes = 0;

    while (writtenBytes < totalBytes) {
        // 检查停止标志
        if (m_stopFlag && *m_stopFlag == false) {
            m_lastError = "用户取消";
            return false;
        }

        int bytesToWrite = qMin(pageSize, totalBytes - writtenBytes);

        if (!sendCommand(CMD_WRITE)) {
            return false;
        }

        quint32 addr = startAddress + writtenBytes;
        if (!sendAddress(addr)) {
            return false;
        }

        QByteArray packet;
        packet.append(static_cast<char>(bytesToWrite - 1));
        for (int i = 0; i < bytesToWrite; ++i) {
            packet.append(data[writtenBytes + i]);
        }
        uint8_t checksum = static_cast<uint8_t>(bytesToWrite - 1);
        for (int i = 0; i < bytesToWrite; ++i) {
            checksum ^= static_cast<uint8_t>(data[writtenBytes + i]);
        }
        packet.append(static_cast<char>(checksum));

        if (!sendData(packet)) {
            log(QString("写入失败，地址: 0x%1").arg(addr, 8, 16, QChar('0')));
            return false;
        }

        writtenBytes += bytesToWrite;

        // 计算进度并发送信号
        int percent = progressMin + (writtenBytes * (progressMax - progressMin)) / totalBytes;
        emit progressChanged(percent);

        // 使用短延时，保持UI响应
        QThread::msleep(1);
        QApplication::processEvents();
    }

    log(QString("写入完成，共 %1 字节").arg(writtenBytes));
    emit statusChanged("写入完成");
    return true;
}

bool Stm32Flasher::readFirmware(QByteArray &data, quint32 startAddress, quint32 length)
{
    log("开始读取固件...");

    const int pageSize = 256;
    data.clear();

    quint32 readBytes = 0;
    while (readBytes < length) {
        int bytesToRead = qMin(pageSize, static_cast<int>(length - readBytes));

        // 发送读命令
        log(QString("发送读命令，地址: 0x%1，字节数: %2").arg(startAddress + readBytes, 8, 16, QChar('0')).arg(bytesToRead));
        if (!sendCommand(CMD_READ)) {
            log("读命令失败: " + m_lastError);
            return false;
        }

        // 发送起始地址
        quint32 addr = startAddress + readBytes;
        if (!sendAddress(addr)) {
            log("发送地址失败: " + m_lastError);
            return false;
        }

        // 发送读取字节数 N-1
        QByteArray cmd;
        cmd.append(static_cast<char>(bytesToRead - 1));
        cmd.append(static_cast<char>(calcChecksum(cmd)));
        m_serial->write(cmd);

        if (!receiveAck()) {
            log("读取ACK失败: " + m_lastError);
            return false;
        }

        // 一次性读取所有数据
        if (!m_serial->waitForReadyRead(2000)) {
            log("读取数据超时");
            return false;
        }

        QByteArray chunk = m_serial->readAll();
        log(QString("读取到 %1 字节").arg(chunk.size()));
        if (chunk.size() < bytesToRead) {
            log(QString("读取数据不足: 期望%1字节，实际%2字节").arg(bytesToRead).arg(chunk.size()));
            return false;
        }

        data.append(chunk.left(bytesToRead));
        readBytes += bytesToRead;

        int percent = (readBytes * 100) / length;
        emit progressChanged(percent);
    }

    log(QString("读取完成，共 %1 字节").arg(readBytes));
    return true;
}

bool Stm32Flasher::readFirmwareWithProgress(QByteArray &data, quint32 startAddress, quint32 length, int progressMin, int progressMax, QObject *progressReceiver)
{
    Q_UNUSED(progressReceiver);  // 不使用这个参数，直接通过信号更新
    log("开始读取固件...");

    const int pageSize = 256;
    data.clear();

    quint32 readBytes = 0;

    while (readBytes < length) {
        // 检查停止标志
        if (m_stopFlag && *m_stopFlag == false) {
            m_lastError = "用户取消";
            return false;
        }

        int bytesToRead = qMin(pageSize, static_cast<int>(length - readBytes));

        if (!sendCommand(CMD_READ)) {
            log("发送读命令失败");
            return false;
        }

        quint32 addr = startAddress + readBytes;
        if (!sendAddress(addr)) {
            log("发送地址失败");
            return false;
        }

        // 短暂延时，确保芯片准备好接收数据
        QThread::msleep(5);
        QApplication::processEvents();

        // 发送读取字节数 N-1 + 校验码(取反)
        uint8_t nMinus1 = static_cast<uint8_t>(bytesToRead - 1);
        uint8_t checksum = ~nMinus1;  // 校验码 = 取反
        QByteArray cmd;
        cmd.append(static_cast<char>(nMinus1));
        cmd.append(static_cast<char>(checksum));

        m_serial->write(cmd);

        if (!receiveAck()) {
            log("请求读取数据失败");
            return false;
        }

        // 读取数据字节 - 使用 receiveByte() 确保从缓冲区读取
        QByteArray chunk;
        for (int i = 0; i < bytesToRead; ++i) {
            // 检查停止标志
            if (m_stopFlag && *m_stopFlag == false) {
                m_lastError = "用户取消";
                return false;
            }

            uint8_t byte;
            if (!receiveByte(&byte)) {
                log(QString("读取数据超时 (%1/%2)").arg(i).arg(bytesToRead));
                return false;
            }
            chunk.append(static_cast<char>(byte));
        }

        data.append(chunk);
        readBytes += bytesToRead;

        // 计算进度并发送信号
        int percent = progressMin + (readBytes * (progressMax - progressMin)) / length;
        emit progressChanged(percent);

        // 使用短延时，保持UI响应
        QThread::msleep(1);
        QApplication::processEvents();
    }

    log(QString("读取完成，共 %1 字节").arg(readBytes));
    return true;
}

bool Stm32Flasher::readChipUID(QString &uid)
{
    // STM32F1系列唯一ID地址: 0x1FFFF7E8 (12字节)
    // 注意：不同系列地址可能不同，这里以F1为例
    const quint32 UID_ADDR = 0x1FFFF7E8;
    const int UID_LEN = 12;

    log("读取芯片唯一序列号...");

    // 发送读命令
    if (!sendCommand(CMD_READ)) {
        log("发送读命令失败");
        return false;
    }

    // 发送起始地址
    if (!sendAddress(UID_ADDR)) {
        log("发送地址失败");
        return false;
    }

    // 短暂延时
    QThread::msleep(5);
    QApplication::processEvents();

    // 发送读取字节数 N-1 + 校验码(取反)
    uint8_t nMinus1 = static_cast<uint8_t>(UID_LEN - 1);
    uint8_t checksum = ~nMinus1;
    QByteArray cmd;
    cmd.append(static_cast<char>(nMinus1));
    cmd.append(static_cast<char>(checksum));

    m_serial->write(cmd);

    if (!receiveAck()) {
        log("请求读取数据失败");
        return false;
    }

    // 读取12字节UID
    QByteArray uidData;
    for (int i = 0; i < UID_LEN; ++i) {
        uint8_t byte;
        if (!receiveByte(&byte)) {
            log(QString("读取数据超时 (%1/%2)").arg(i).arg(UID_LEN));
            return false;
        }
        uidData.append(static_cast<char>(byte));
    }

    // 格式化为十六进制字符串（连续格式）
    uid.clear();
    for (int i = 0; i < UID_LEN; ++i) {
        uid += QString("%1").arg(static_cast<uint8_t>(uidData[i]), 2, 16, QChar('0')).toUpper();
    }

    // 按4字节分组显示（3组，每组8个十六进制字符）
    // 注意：STM32的UID是小端序，需要反转每组的字节顺序
    QString grouped;
    for (int g = 0; g < 3; ++g) {
        QString group;
        for (int i = 3; i >= 0; --i) {
            group += QString("%1").arg(static_cast<uint8_t>(uidData[g * 4 + i]), 2, 16, QChar('0')).toUpper();
        }
        if (!grouped.isEmpty()) grouped += " ";
        grouped += group;
    }

    log(QString("96位芯片唯一序列号："));
    log(QString("[%1]").arg(uid));
    log(QString("[%1]").arg(grouped));

    return true;
}

bool Stm32Flasher::execute(quint32 address)
{
    log(QString("执行代码，地址: 0x%1").arg(address, 8, 16, QChar('0')));
    emit statusChanged("正在启动...");

    // 发送执行命令
    if (!sendCommand(CMD_GO)) {
        return false;
    }

    // 发送起始地址
    if (!sendAddress(address)) {
        return false;
    }

    log("代码执行中...");
    emit statusChanged("运行中");
    return true;
}
