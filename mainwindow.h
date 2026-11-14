#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProgressBar>
#include <QLabel>
#include <QTimer>
#include "serial/serialmanager.h"
#include "protocol/stm32flasher.h"
#include "utils/hexparser.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    // 串口操作
    void onRefreshPorts();

    // 烧录操作
    void onSelectFileClicked();
    void onStartProgramClicked();
    void onStopProgramClicked();
    void onReadInfoClicked();
    void onEraseChipClicked();
    void onReadFlashClicked();

    // 状态更新
    void onSerialConnected();
    void onSerialDisconnected();
    void onSerialError(const QString &error);
    void onProgressChanged(int percent);
    void onStatusChanged(const QString &status);
    void onLogMessage(const QString &msg);

    // 超时处理
    void onProgramTimeout();

private:
    void setupUI();
    void setupConnections();
    void updateUI(bool connected);
    void log(const QString &msg);
    void setStatus(const QString &msg);
    void loadFirmware();
    void startProgramming();
    void stopProgramming();
    void setProgramButtonToStop();
    void setProgramButtonToStart();
    void loadSettings();
    void saveSettings();
    bool saveAsHex(const QByteArray &data, quint32 startAddress, const QString &filePath);

    Ui::MainWindow *ui;

    // 核心组件
    SerialManager *m_serialManager;
    Stm32Flasher *m_flasher;
    QProgressBar *m_progressBar;  // 状态栏进度条

    // 状态
    bool m_isConnected;
    QString m_firmwarePath;
    QByteArray m_firmwareData;
    quint32 m_firmwareStartAddr;

    // 选项
    bool m_verifyEnabled;
    bool m_runAfterEnabled;
    bool m_continuousMode;

    // 烧录控制
    bool m_isProgramming;
    QTimer *m_programTimer;  // 超时定时器
};

#endif // MAINWINDOW_H
