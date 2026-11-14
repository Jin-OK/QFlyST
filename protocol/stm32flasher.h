#ifndef STM32FLASHER_H
#define STM32FLASHER_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QProgressBar>
#include "../serial/serialmanager.h"

/**
 * @brief STM32串口烧录协议类
 * 实现STM32内置Bootloader的串口通信协议
 */
class Stm32Flasher : public QObject
{
    Q_OBJECT

public:
    // 烧录命令字节
    enum Command {
        CMD_INIT      = 0x7F,  // 初始化/同步命令
        CMD_GET       = 0x00,  // 获取版本和命令
        CMD_GET_VER   = 0x01,  // 获取版本
        CMD_GET_ID    = 0x02,  // 获取芯片ID
        CMD_READ      = 0x11,  // 读内存
        CMD_GO        = 0x21,  // 执行代码
        CMD_WRITE     = 0x31,  // 写内存
        CMD_ERASE     = 0x43,  // 擦除
        CMD_EXT_ERASE = 0x44   // 扩展擦除
    };

    // 响应状态
    enum Ack {
        ACK  = 0x79,  // 确认
        NACK = 0x1F   // 否认
    };

    explicit Stm32Flasher(SerialManager *serial, QObject *parent = nullptr);
    ~Stm32Flasher();

    // 初始化连接（发送0x7F同步）
    bool initConnection(bool *stopFlag = nullptr);

    // 获取芯片信息
    bool getChipInfo(uint8_t *version, uint16_t *chipId);

    // 擦除芯片
    bool eraseChip();

    // 写入固件
    bool writeFirmware(const QByteArray &data, quint32 startAddress = 0x08000000);

    // 写入固件（带进度范围）
    bool writeFirmwareWithProgress(const QByteArray &data, quint32 startAddress, int progressMin, int progressMax, QObject *progressReceiver);

    // 读取固件（验证用）
    bool readFirmware(QByteArray &data, quint32 startAddress, quint32 length);

    // 读取固件（带进度范围）
    bool readFirmwareWithProgress(QByteArray &data, quint32 startAddress, quint32 length, int progressMin, int progressMax, QObject *progressReceiver);

    // 读取芯片唯一序列号
    bool readChipUID(QString &uid);

    // 执行代码（跳转到应用程序）
    bool execute(quint32 address = 0x08000000);

    // 获取错误信息
    QString lastError() const { return m_lastError; }

    // 设置进度条
    void setProgressbar(QProgressBar *bar) { m_progressBar = bar; }

    // 设置停止标志
    void setStopFlag(bool *flag) { m_stopFlag = flag; }
    bool *stopFlag() const { return m_stopFlag; }

signals:
    void progressChanged(int percent);
    void statusChanged(const QString &status);
    void logMessage(const QString &msg);

private:
    SerialManager *m_serial;
    QProgressBar *m_progressBar;
    QString m_lastError;
    QByteArray m_receiveBuffer;  // 接收缓冲区
    bool *m_stopFlag;  // 停止标志指针

    // 发送命令并等待确认
    bool sendCommand(uint8_t cmd);

    // 发送地址
    bool sendAddress(quint32 addr);

    // 发送数据
    bool sendData(const QByteArray &data);

    // 接收确认
    bool receiveAck();

    // 接收字节
    bool receiveByte(uint8_t *byte);

    // 计算校验码
    uint8_t calcChecksum(const QByteArray &data);

    // 日志输出
    void log(const QString &msg);

    // 显示芯片详细信息
    void logChipInfo(uint16_t chipId, QString &flashSize, QString &sramSize);
};

#endif // STM32FLASHER_H
