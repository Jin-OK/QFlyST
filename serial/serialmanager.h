#ifndef SERIALMANAGER_H
#define SERIALMANAGER_H

#include <QObject>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QByteArray>
#include <QStringList>

/**
 * @brief 串口管理类
 * 负责管理串口连接、自动进入Bootloader模式（通过RTS/DTR控制BOOT0和RESET）
 */
class SerialManager : public QObject
{
    Q_OBJECT

public:
    // RTS/DTR 电平模式 - 更灵活的选择
    enum BootMode {
        // 不选择，手动控制
        MODE_MANUAL = 0,

        // 标准模式 (CH340C典型)
        MODE_RTS_HIGH_DTR_LOW = 1,    // RTS=HIGH复位, DTR=LOW进Bootloader

        // 反相模式
        MODE_RTS_LOW_DTR_HIGH = 2,    // RTS=LOW复位, DTR=HIGH进Bootloader

        // 同极性模式
        MODE_RTS_HIGH_DTR_HIGH = 3,   // RTS=HIGH复位, DTR=HIGH进Bootloader
        MODE_RTS_LOW_DTR_LOW = 4,     // RTS=LOW复位, DTR=LOW进Bootloader

        // 单线模式
        MODE_RTS_ONLY = 5,            // 仅RTS控制（需手动复位）
        MODE_DTR_ONLY = 6             // 仅DTR控制（BOOT0外部设定）
    };

    explicit SerialManager(QObject *parent = nullptr);
    ~SerialManager();

    // 扫描可用串口
    QStringList availablePorts() const;

    // 打开串口
    bool open(const QString &portName, qint32 baudRate = 115200);

    // 关闭串口
    void close();

    // 是否已打开
    bool isOpen() const;

    // 发送数据
    qint64 write(const QByteArray &data);

    // 等待接收数据
    bool waitForReadyRead(int msecs = 1000);

    // 读取所有可用数据
    QByteArray readAll();

    // 获取可用字节数
    qint64 bytesAvailable() const;

    // 清空输入/输出缓冲区
    void clearBuffers();

    /**
     * @brief 进入Bootloader模式
     * 根据当前选择的模式执行相应的RTS/DTR组合
     * 如果选择"不选择"，则不执行任何操作
     */
    bool enterBootloader();

    /**
     * @brief 退出Bootloader模式，进入正常运行
     */
    bool enterNormalMode();

    /**
     * @brief 仅复位芯片
     */
    bool resetOnly();

    // 获取/设置Boot模式
    BootMode bootMode() const { return m_bootMode; }
    void setBootMode(BootMode mode) { m_bootMode = mode; }

    // 手动控制RTS和DTR
    void setRts(bool level);
    void setDtr(bool level);

    // 获取当前引脚状态
    bool rts() const { return m_rtsLevel; }
    bool dtr() const { return m_dtrLevel; }

    // 获取当前串口信息
    QSerialPort *port() const { return m_serial; }

signals:
    void dataReceived();
    void errorOccurred(const QString &error);
    void connected();
    void disconnected();
    void rtsChanged(bool level);
    void dtrChanged(bool level);
    void logMessage(const QString &msg);

private:
    QSerialPort *m_serial;
    BootMode m_bootMode;
    bool m_rtsLevel;
    bool m_dtrLevel;

    // 毫秒级延时
    void delayMs(int ms);

    // 根据模式进入Bootloader
    bool enterBootloaderWithMode();
};

#endif // SERIALMANAGER_H
