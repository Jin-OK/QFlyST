#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QThread>
#include <QSettings>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QInputDialog>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_serialManager(new SerialManager(this))
    , m_flasher(nullptr)
    , m_progressBar(nullptr)
    , m_isConnected(false)
    , m_firmwareStartAddr(0x08000000)
    , m_verifyEnabled(true)
    , m_runAfterEnabled(true)
    , m_continuousMode(false)
    , m_isProgramming(false)
    , m_stopFlag(false)
    , m_programTimer(new QTimer(this))
{
    ui->setupUi(this);

    setupUI();
    setupConnections();

    // 初始化串口管理器信号
    connect(m_serialManager, &SerialManager::connected, this, &MainWindow::onSerialConnected);
    connect(m_serialManager, &SerialManager::disconnected, this, &MainWindow::onSerialDisconnected);
    connect(m_serialManager, &SerialManager::errorOccurred, this, &MainWindow::onSerialError);
    connect(m_serialManager, &SerialManager::logMessage, this, &MainWindow::log);

    // 超时定时器
    connect(m_programTimer, &QTimer::timeout, this, &MainWindow::onProgramTimeout);

    // 扫描串口
    onRefreshPorts();

    // 加载上次的设置
    loadSettings();
}

MainWindow::~MainWindow()
{
    // 保存设置
    saveSettings();

    delete ui;
}

void MainWindow::setupUI()
{
    setWindowTitle("QFlyST V1.0 - STM32在线编程工具");
    resize(780, 590);

    // 状态栏添加进度条 (可拉伸)
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);  // 可拉伸
    ui->statusbar->addWidget(m_progressBar, 10);  // stretch = 10，更大的拉伸因子

    // 状态栏添加状态标签 (在最右边，固定宽度)
    QLabel *statusLabel = new QLabel(this);
    statusLabel->setText("就绪");
    statusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    statusLabel->setMinimumWidth(100);  // 设置最小宽度
    ui->statusbar->addWidget(statusLabel);

    // 设置开始烧录按钮为浅蓝色
    ui->btnStartProgram->setStyleSheet(
        "QPushButton {"
        "   background-color: #3498db;"
        "   color: white;"
        "   border: none;"
        "   padding: 5px 15px;"
        "   border-radius: 3px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #2980b9;"
        "}"
        "QPushButton:pressed {"
        "   background-color: #21618c;"
        "}"
        "QPushButton:disabled {"
        "   background-color: #bdc3c7;"
        "   color: #7f8c8d;"
        "}"
    );

    // 设置清除芯片按钮为浅粉红色
    ui->btnEraseChip->setStyleSheet(
        "QPushButton {"
        "   background-color: #FFD1DC;"  // 更浅的粉红色
        "   color: #333;"
        "   border: none;"
        "   padding: 5px 15px;"
        "   border-radius: 3px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #FFB6C1;"  // 浅粉红色
        "}"
        "QPushButton:pressed {"
        "   background-color: #FF9AAE;"
        "}"
        "QPushButton:disabled {"
        "   background-color: #bdc3c7;"
        "   color: #7f8c8d;"
        "}"
    );

    // 初始化时禁用烧录相关按钮
    updateUI(false);

    // 初始搜索串口
    onRefreshPorts();
}

void MainWindow::setupConnections()
{
    // 串口操作
    connect(ui->btnRefreshPorts, &QPushButton::clicked, this, &MainWindow::onRefreshPorts);

    // 文件选择
    connect(ui->btnSelectFile, &QPushButton::clicked, this, &MainWindow::onSelectFileClicked);

    // 文件路径手动输入 - 当编辑框失去焦点或按下回车时加载
    connect(ui->editFirmwarePath, &QLineEdit::editingFinished, this, [this]() {
        QString path = ui->editFirmwarePath->text();
        if (!path.isEmpty() && path != m_firmwarePath) {
            loadFirmware();
        }
    });

    // 固件起始地址输入 - 当编辑框失去焦点或按下回车时更新地址
    connect(ui->editStartAddress, &QLineEdit::editingFinished, this, [this]() {
        QString addr = ui->editStartAddress->text().trimmed();
        // 去除 0x 前缀再解析
        if (addr.startsWith("0x", Qt::CaseInsensitive)) {
            addr = addr.mid(2);
        }
        bool ok;
        quint32 addrValue = addr.toUInt(&ok, 16);
        if (ok) {
            m_firmwareStartAddr = addrValue;
            log(QString("固件起始地址设置为: 0x%1").arg(m_firmwareStartAddr, 8, 16, QChar('0')));
            // 更新显示格式
            ui->editStartAddress->setText(QString("0x%1").arg(m_firmwareStartAddr, 8, 16, QChar('0')));
        } else {
            log("无效的固件起始地址格式");
            ui->editStartAddress->setText(QString("0x%1").arg(m_firmwareStartAddr, 8, 16, QChar('0')));
        }
    });

    // 手动指定地址复选框 - 切换时重新加载固件以更新地址栏状态
    connect(ui->chkManualAddress, &QCheckBox::toggled, this, [this]() {
        if (!m_firmwarePath.isEmpty()) {
            loadFirmware();
        }
    });

    // 烧录操作按钮
    connect(ui->btnStartProgram, &QPushButton::clicked, this, &MainWindow::onStartProgramClicked);
    connect(ui->btnReadInfo, &QPushButton::clicked, this, &MainWindow::onReadInfoClicked);
    connect(ui->btnEraseChip, &QPushButton::clicked, this, &MainWindow::onEraseChipClicked);
    connect(ui->btnReadFlash, &QPushButton::clicked, this, &MainWindow::onReadFlashClicked);

    // 编程选项
    connect(ui->chkVerify, &QCheckBox::toggled, this, [this](bool checked){ m_verifyEnabled = checked; });
    connect(ui->chkRunAfter, &QCheckBox::toggled, this, [this](bool checked){ m_runAfterEnabled = checked; });

    // RTS/DTR配置
    connect(ui->cbBootMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (m_serialManager) {
            m_serialManager->setBootMode(static_cast<SerialManager::BootMode>(index));
        }
    });
}

void MainWindow::onRefreshPorts()
{
    ui->cbSerialPort->clear();
    QStringList ports = m_serialManager->availablePorts();
    ui->cbSerialPort->addItems(ports);
    if (ports.isEmpty()) {
        log("未找到可用串口");
    } else {
        log(QString("找到 %1 个串口").arg(ports.size()));
    }
}

void MainWindow::onSelectFileClicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        this,
        "选择固件文件",
        QString(),
        "固件文件 (*.hex *.bin);;Intel HEX (*.hex);;二进制文件 (*.bin);;所有文件 (*.*)"
    );

    if (filePath.isEmpty()) {
        return;
    }

    ui->editFirmwarePath->setText(filePath);
    loadFirmware();
}

void MainWindow::loadFirmware()
{
    QString filePath = ui->editFirmwarePath->text();
    if (filePath.isEmpty()) {
        return;
    }

    // 检查文件是否存在（启动时可能文件已被删除）
    QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        log(QString("固件文件不存在: %1").arg(filePath));
        // 清空路径，避免下次启动时再次尝试
        m_firmwarePath.clear();
        return;
    }

    log(QString("加载固件: %1").arg(filePath));

    HexParser::ParseResult result = HexParser::loadFirmware(filePath);
    if (!result.success) {
        log("加载固件失败: " + result.error);
        return;
    }

    m_firmwarePath = filePath;
    m_firmwareData = result.data;

    // 判断是否为 HEX 文件
    bool isHexFile = filePath.endsWith(".hex", Qt::CaseInsensitive);

    if (isHexFile && !ui->chkManualAddress->isChecked()) {
        // HEX 文件：使用文件中的地址，地址栏只读
        m_firmwareStartAddr = result.startAddress;
        ui->editStartAddress->setText(QString("0x%1").arg(m_firmwareStartAddr, 8, 16, QChar('0')));
        ui->editStartAddress->setReadOnly(true);
        ui->editStartAddress->setStyleSheet("QLineEdit { background-color: #f0f0f0; color: #666; }");
        log(QString("HEX文件地址: 0x%1").arg(m_firmwareStartAddr, 8, 16, QChar('0')));
    } else {
        // BIN 文件 或 手动指定地址：使用UI中的地址
        QString addrText = ui->editStartAddress->text().trimmed();
        // 去除 0x 前缀再解析
        if (addrText.startsWith("0x", Qt::CaseInsensitive)) {
            addrText = addrText.mid(2);
        }
        bool ok;
        quint32 addrValue = addrText.toUInt(&ok, 16);
        if (ok) {
            m_firmwareStartAddr = addrValue;
        } else {
            // BIN 文件默认使用 STM32 Flash 起始地址
            m_firmwareStartAddr = 0x08000000;
            ui->editStartAddress->setText("0x08000000");
        }
        ui->editStartAddress->setReadOnly(false);
        ui->editStartAddress->setStyleSheet("");
    }

    log(QString("固件大小: %1 字节 | 起始地址: 0x%2")
        .arg(m_firmwareData.size())
        .arg(m_firmwareStartAddr, 8, 16, QChar('0')));

    setStatus("固件已就绪");
}

void MainWindow::onStartProgramClicked()
{
    // 如果正在烧录，点击的是停止按钮
    if (m_isProgramming) {
        onStopProgramClicked();
        return;
    }

    // 开始烧录流程
    startProgramming();
}

void MainWindow::startProgramming()
{
    // 清除日志
    ui->logView->clear();

    // 获取串口
    QString portName = ui->cbSerialPort->currentText();
    if (portName.isEmpty()) {
        log("请先搜索并选择串口");
        return;
    }

    // 连接串口
    if (!m_isConnected) {
        qint32 baudRate = ui->cbBaudRate->currentText().toInt();
        if (baudRate <= 0) {
            baudRate = 115200;
        }

        log(QString("连接 %1 @ %2...").arg(portName).arg(baudRate));

        if (!m_serialManager->open(portName, baudRate)) {
            log("打开串口失败");
            return;
        }

        // 创建烧录器
        if (m_flasher) {
            delete m_flasher;
        }
        m_flasher = new Stm32Flasher(m_serialManager, this);

        connect(m_flasher, &Stm32Flasher::progressChanged, this, &MainWindow::onProgressChanged);
        connect(m_flasher, &Stm32Flasher::statusChanged, this, &MainWindow::onStatusChanged);
        connect(m_flasher, &Stm32Flasher::logMessage, this, &MainWindow::onLogMessage);
    }

    if (m_firmwareData.isEmpty()) {
        loadFirmware();
        if (m_firmwareData.isEmpty()) {
            log("请先选择固件文件");
            return;
        }
    }

    // 设置烧录状态
    m_isProgramming = true;  // 表示正在烧录
    m_stopFlag = false;  // 清除停止标志
    setProgramButtonToStop();

    log("========== 开始编程 ==========");
    setStatus("正在编程...");
    onProgressChanged(0);

    // 设置停止标志
    m_flasher->setStopFlag(&m_stopFlag);

    // 启动60秒超时定时器（原来是30秒）
    m_programTimer->start(60000);

    // 1. 进入 Bootloader 模式
    SerialManager::BootMode mode = static_cast<SerialManager::BootMode>(ui->cbBootMode->currentIndex());
    log(QString("进入 Bootloader 模式... (模式: %1)").arg(ui->cbBootMode->currentText()));

    if (!m_serialManager->enterBootloader()) {
        log("进入 Bootloader 失败");
        stopProgramming();
        return;
    }

    // 如果是手动模式，提示用户并等待
    if (mode == SerialManager::MODE_MANUAL) {
        log("手动模式：请确保芯片已进入Bootloader");
        log("提示: 按住RESET，选择'不使用'模式，然后点击开始编程，再松开RESET");
        setStatus("等待芯片进入Bootloader...");
    } else if (mode == SerialManager::MODE_RTS_ONLY) {
        log("请手动按下复位键...");
        setStatus("等待复位...");
    }

    // 等待过程中检查停止标志，使用非阻塞方式
    for (int i = 0; i < 10 && m_isProgramming; ++i) {
        QThread::msleep(20);
        QApplication::processEvents();
    }

    if (!m_isProgramming) {
        return;  // 用户取消
    }

    // 2. 初始化连接 (发送 0x7F)
    if (!m_flasher->initConnection()) {
        log("Bootloader 初始化失败: " + m_flasher->lastError());
        log("请检查硬件连接和 BOOT0 电平");
        stopProgramming();
        return;
    }

    // 3. 获取芯片信息
    uint8_t version;
    uint16_t chipId;
    if (!m_flasher->getChipInfo(&version, &chipId)) {
        log("获取芯片信息失败");
        stopProgramming();
        return;
    }

    log(QString("芯片ID: 0x%1 | Bootloader版本: %2.%3")
        .arg(chipId, 4, 16, QChar('0'))
        .arg(version / 16)
        .arg(version % 16));

    // 4. 擦除芯片 (0-20%)
    if (!m_flasher->eraseChip()) {
        log("擦除失败");
        stopProgramming();
        return;
    }
    onProgressChanged(20);

    // 5. 写入固件 (20-70%)
    if (!m_flasher->writeFirmwareWithProgress(m_firmwareData, m_firmwareStartAddr, 20, 70, this)) {
        log("写入失败");
        stopProgramming();
        return;
    }
    onProgressChanged(70);

    // 6. 校验（如果启用）(70-100%)
    bool verifyPassed = false;
    if (m_verifyEnabled) {
        log("开始校验...");
        QByteArray readData;
        if (!m_flasher->readFirmwareWithProgress(readData, m_firmwareStartAddr, m_firmwareData.size(), 70, 100, this)) {
            log("校验失败：读取数据错误");
        } else if (readData != m_firmwareData) {
            log("校验失败！数据不匹配");
        } else {
            log("校验成功");
            verifyPassed = true;
        }
    } else {
        onProgressChanged(100);
    }

    // 7. 执行代码（如果启用）
    if (m_runAfterEnabled) {
        if (!m_flasher->execute(m_firmwareStartAddr)) {
            log("执行代码失败");
        }
    }

    onProgressChanged(100);
    log("========== 编程完成 ==========");
    setStatus("编程完成");

    // 停止烧录流程
    stopProgramming();

    // 连续烧录模式
    if (m_continuousMode) {
        log("连续烧录模式：等待下一次编程...");
    }
}

void MainWindow::onStopProgramClicked()
{
    if (!m_isProgramming) {
        return;
    }

    log("========== 用户取消编程 ==========");
    stopProgramming();
}

void MainWindow::stopProgramming()
{
    m_isProgramming = false;
    m_stopFlag = true;  // 设置停止标志
    m_programTimer->stop();
    setProgramButtonToStart();

    // 关闭串口
    if (m_serialManager->isOpen()) {
        m_serialManager->close();
    }
}

void MainWindow::onProgramTimeout()
{
    log("========== 编程超时 (60秒) ==========");
    stopProgramming();
}

void MainWindow::setProgramButtonToStop()
{
    ui->btnStartProgram->setText("停止烧录");
    ui->btnStartProgram->setStyleSheet(
        "QPushButton {"
        "   background-color: #e74c3c;"
        "   color: white;"
        "   border: none;"
        "   padding: 5px 15px;"
        "   border-radius: 3px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #c0392b;"
        "}"
        "QPushButton:pressed {"
        "   background-color: #a93226;"
        "}"
    );
    ui->btnStartProgram->setEnabled(true);
}

void MainWindow::setProgramButtonToStart()
{
    ui->btnStartProgram->setText("开始烧录");
    ui->btnStartProgram->setStyleSheet(
        "QPushButton {"
        "   background-color: #3498db;"
        "   color: white;"
        "   border: none;"
        "   padding: 5px 15px;"
        "   border-radius: 3px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #2980b9;"
        "}"
        "QPushButton:pressed {"
        "   background-color: #21618c;"
        "}"
        "QPushButton:disabled {"
        "   background-color: #bdc3c7;"
        "   color: #7f8c8d;"
        "}"
    );
    ui->btnStartProgram->setEnabled(true);
}

void MainWindow::onReadInfoClicked()
{
    // 清除日志
    ui->logView->clear();

    // 获取串口
    QString portName = ui->cbSerialPort->currentText();
    if (portName.isEmpty()) {
        log("请先搜索并选择串口");
        return;
    }

    // 连接串口
    if (!m_isConnected) {
        qint32 baudRate = ui->cbBaudRate->currentText().toInt();
        if (baudRate <= 0) baudRate = 115200;

        if (!m_serialManager->open(portName, baudRate)) {
            log("打开串口失败");
            return;
        }
        if (m_flasher) delete m_flasher;
        m_flasher = new Stm32Flasher(m_serialManager, this);
        connect(m_flasher, &Stm32Flasher::logMessage, this, &MainWindow::onLogMessage);
    }

    log("读取器件信息...");

    // 设置停止标志
    m_stopFlag = false;
    m_flasher->setStopFlag(&m_stopFlag);

    // 进入 Bootloader 模式
    if (!m_serialManager->enterBootloader()) {
        log("进入 Bootloader 失败");
        m_serialManager->close();
        return;
    }

    // 记录开始时间
    QElapsedTimer timer;
    timer.start();

    // 使用非阻塞方式等待
    for (int i = 0; i < 10; ++i) {
        QThread::msleep(10);
        QApplication::processEvents();
    }

    // 初始化连接
    if (!m_flasher->initConnection()) {
        log("Bootloader 初始化失败");
        m_serialManager->close();
        return;
    }

    // 获取芯片信息
    uint8_t version;
    uint16_t chipId;
    if (!m_flasher->getChipInfo(&version, &chipId)) {
        log("获取芯片信息失败");
        m_serialManager->close();
        return;
    }

    // 显示连接耗时
    qint64 elapsed = timer.elapsed();
    log(QString("在串口%1连接成功@%2bps，耗时%3毫秒")
        .arg(portName)
        .arg(ui->cbBaudRate->currentText())
        .arg(elapsed));

    setStatus("读取完成");

    // 关闭串口
    m_serialManager->close();
}

void MainWindow::onEraseChipClicked()
{
    // 清除日志
    ui->logView->clear();

    // 获取串口
    QString portName = ui->cbSerialPort->currentText();
    if (portName.isEmpty()) {
        log("请先搜索并选择串口");
        return;
    }

    // 连接串口
    if (!m_isConnected) {
        qint32 baudRate = ui->cbBaudRate->currentText().toInt();
        if (baudRate <= 0) baudRate = 115200;

        if (!m_serialManager->open(portName, baudRate)) {
            log("打开串口失败");
            return;
        }
        if (m_flasher) delete m_flasher;
        m_flasher = new Stm32Flasher(m_serialManager, this);
        connect(m_flasher, &Stm32Flasher::logMessage, this, &MainWindow::onLogMessage);
    }

    log("擦除芯片...");

    // 设置停止标志
    m_stopFlag = false;
    m_flasher->setStopFlag(&m_stopFlag);

    // 进入 Bootloader 模式
    if (!m_serialManager->enterBootloader()) {
        log("进入 Bootloader 失败");
        m_serialManager->close();  // 关闭串口
        return;
    }

    // 使用非阻塞方式等待
    for (int i = 0; i < 10; ++i) {
        QThread::msleep(10);
        QApplication::processEvents();
    }

    // 初始化连接
    if (!m_flasher->initConnection()) {
        log("Bootloader 初始化失败");
        m_serialManager->close();  // 关闭串口
        return;
    }

    // 擦除
    if (!m_flasher->eraseChip()) {
        log("擦除失败");
        m_serialManager->close();  // 关闭串口
        return;
    }

    log("擦除完成");
    setStatus("擦除完成");

    // 关闭串口
    m_serialManager->close();
}

void MainWindow::onReadFlashClicked()
{
    // 获取串口
    QString portName = ui->cbSerialPort->currentText();
    if (portName.isEmpty()) {
        log("请先搜索并选择串口");
        return;
    }

    // 获取读取起始地址
    QString addrText = ui->editStartAddress->text().trimmed();
    if (addrText.startsWith("0x", Qt::CaseInsensitive)) {
        addrText = addrText.mid(2);
    }
    bool ok;
    quint32 startAddr = addrText.toUInt(&ok, 16);
    if (!ok) {
        log("无效的起始地址格式");
        return;
    }

    // 弹出对话框询问读取长度
    QStringList items;
    items << "0x1000 (4KB)" << "0x4000 (16KB)" << "0x8000 (32KB)" << "0x10000 (64KB)"
          << "0x20000 (128KB)" << "0x40000 (256KB)" << "自定义";

    QString item = QInputDialog::getItem(this, "读取FLASH",
        "选择读取长度:", items, 5, false, &ok);
    if (!ok || item.isEmpty()) {
        return;
    }

    quint32 readLength;
    if (item == "自定义") {
        QString lengthText = QInputDialog::getText(this, "读取FLASH",
            "请输入读取长度 (十六进制):", QLineEdit::Normal, "0x1000", &ok);
        if (!ok || lengthText.isEmpty()) {
            return;
        }
        if (lengthText.startsWith("0x", Qt::CaseInsensitive)) {
            lengthText = lengthText.mid(2);
        }
        readLength = lengthText.toUInt(&ok, 16);
    } else {
        // 从选项解析长度
        QString hexStr = item.split(" ")[0];
        readLength = hexStr.toUInt(&ok, 16);
    }

    if (!ok || readLength == 0) {
        log("无效的读取长度");
        return;
    }

    // 选择保存文件路径
    QString savePath = QFileDialog::getSaveFileName(
        this,
        "保存Flash内容",
        QString(),
        "BIN文件 (*.bin);;HEX文件 (*.hex);;所有文件 (*.*)"
    );

    if (savePath.isEmpty()) {
        return;
    }

    // 连接串口
    if (!m_isConnected) {
        qint32 baudRate = ui->cbBaudRate->currentText().toInt();
        if (baudRate <= 0) baudRate = 115200;

        log(QString("连接 %1...").arg(portName));
        if (!m_serialManager->open(portName, baudRate)) {
            log("打开串口失败");
            return;
        }
    }

    // 创建烧录器
    if (!m_flasher) {
        m_flasher = new Stm32Flasher(m_serialManager, this);
        connect(m_flasher, &Stm32Flasher::progressChanged, this, &MainWindow::onProgressChanged);
        connect(m_flasher, &Stm32Flasher::statusChanged, this, &MainWindow::onStatusChanged);
        connect(m_flasher, &Stm32Flasher::logMessage, this, &MainWindow::onLogMessage);
    }

    // 清除日志
    ui->logView->clear();

    log("========== 开始读取FLASH ==========");
    log(QString("起始地址: 0x%1 | 读取长度: %2 字节")
        .arg(startAddr, 8, 16, QChar('0'))
        .arg(readLength));
    setStatus("正在读取...");
    onProgressChanged(0);

    // 设置停止标志
    m_stopFlag = false;
    m_flasher->setStopFlag(&m_stopFlag);

    // 进入 Bootloader 模式
    SerialManager::BootMode mode = static_cast<SerialManager::BootMode>(ui->cbBootMode->currentIndex());
    if (!m_serialManager->enterBootloader()) {
        log("进入 Bootloader 失败");
        m_serialManager->close();
        setStatus("读取失败");
        return;
    }

    // 等待
    for (int i = 0; i < 10; ++i) {
        QThread::msleep(20);
        QApplication::processEvents();
    }

    // 初始化连接
    if (!m_flasher->initConnection()) {
        log("Bootloader 初始化失败: " + m_flasher->lastError());
        m_serialManager->close();
        setStatus("读取失败");
        return;
    }

    // 读取Flash
    QByteArray flashData;
    if (!m_flasher->readFirmwareWithProgress(flashData, startAddr, readLength, 0, 100, this)) {
        log("读取失败: " + m_flasher->lastError());
        m_serialManager->close();
        setStatus("读取失败");
        return;
    }

    onProgressChanged(100);
    log(QString("读取完成，共 %1 字节").arg(flashData.size()));

    // 保存文件
    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly)) {
        log("无法打开文件: " + savePath);
        m_serialManager->close();
        setStatus("保存失败");
        return;
    }

    // 判断保存格式
    if (savePath.endsWith(".hex", Qt::CaseInsensitive)) {
        // 保存为HEX格式
        file.close();
        if (!saveAsHex(flashData, startAddr, savePath)) {
            log("保存HEX文件失败");
            m_serialManager->close();
            setStatus("保存失败");
            return;
        }
    } else {
        // 保存为BIN格式
        file.write(flashData);
        file.close();
    }

    log("已保存到: " + savePath);
    setStatus("读取完成");

    // 关闭串口
    m_serialManager->close();
    log("========== 读取结束 ==========");
}

// 保存为Intel HEX格式
bool MainWindow::saveAsHex(const QByteArray &data, quint32 startAddress, const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream out(&file);
    quint32 address = startAddress;
    int offset = 0;
    const int bytesPerLine = 16;

    while (offset < data.size()) {
        // 检查是否需要扩展地址记录
        if ((address & 0xFFFF0000) != 0 && (offset % 0x10000 == 0)) {
            // 写入扩展线性地址记录
            uint16_t upperAddr = (address >> 16) & 0xFFFF;
            QByteArray extRecord;
            extRecord.append(static_cast<char>(0x02));  // 长度
            extRecord.append(static_cast<char>(0x00));
            extRecord.append(static_cast<char>(0x00));
            extRecord.append(static_cast<char>(0x04));  // 扩展线性地址记录类型
            extRecord.append(static_cast<char>((upperAddr >> 8) & 0xFF));
            extRecord.append(static_cast<char>(upperAddr & 0xFF));

            // 计算校验和
            uint8_t checksum = 0;
            for (int i = 0; i < extRecord.size(); ++i) {
                checksum += static_cast<uint8_t>(extRecord[i]);
            }
            checksum = (0x100 - checksum) & 0xFF;
            extRecord.append(static_cast<char>(checksum));

            // 写入记录
            out << ":";
            for (int i = 0; i < extRecord.size(); ++i) {
                out << QString("%1").arg(static_cast<uint8_t>(extRecord[i]), 2, 16, QChar('0')).toUpper();
            }
            out << "\n";
        }

        // 计算本行字节数
        int lineBytes = qMin(bytesPerLine, data.size() - offset);

        // 写入数据记录
        QByteArray record;
        record.append(static_cast<char>(lineBytes));  // 长度
        record.append(static_cast<char>((address >> 8) & 0xFF));  // 地址高位
        record.append(static_cast<char>(address & 0xFF));  // 地址低位
        record.append(static_cast<char>(0x00));  // 记录类型 (数据)

        for (int i = 0; i < lineBytes; ++i) {
            record.append(static_cast<char>(data[offset + i]));
        }

        // 计算校验和
        uint8_t checksum = 0;
        for (int i = 0; i < record.size(); ++i) {
            checksum += static_cast<uint8_t>(record[i]);
        }
        checksum = (0x100 - checksum) & 0xFF;
        record.append(static_cast<char>(checksum));

        // 写入记录
        out << ":";
        for (int i = 0; i < record.size(); ++i) {
            out << QString("%1").arg(static_cast<uint8_t>(record[i]), 2, 16, QChar('0')).toUpper();
        }
        out << "\n";

        offset += lineBytes;
        address += lineBytes;
    }

    // 写入EOF记录
    out << ":00000001FF\n";

    file.close();
    return true;
}

void MainWindow::onSerialConnected()
{
    m_isConnected = true;
    updateUI(true);
    log("串口已连接");
    setStatus("已连接");
}

void MainWindow::onSerialDisconnected()
{
    m_isConnected = false;
    updateUI(false);
    log("串口已断开");
    setStatus("未连接");
}

void MainWindow::onSerialError(const QString &error)
{
    log("串口错误: " + error);
    setStatus("错误");
}

void MainWindow::onProgressChanged(int percent)
{
    // 更新状态栏进度条
    if (m_progressBar) {
        m_progressBar->setValue(percent);
    }
}

void MainWindow::onStatusChanged(const QString &status)
{
    setStatus(status);
}

void MainWindow::onLogMessage(const QString &msg)
{
    log(msg);
}

void MainWindow::updateUI(bool connected)
{
    ui->btnStartProgram->setEnabled(true);
    ui->btnReadInfo->setEnabled(true);
    ui->btnEraseChip->setEnabled(true);
    ui->btnReadFlash->setEnabled(true);
}

void MainWindow::log(const QString &msg)
{
    ui->logView->append(msg);
    // 滚动到底部
    QTextCursor cursor = ui->logView->textCursor();
    cursor.movePosition(QTextCursor::End);
    ui->logView->setTextCursor(cursor);
    ui->logView->update();
}

void MainWindow::setStatus(const QString &msg)
{
    // 更新状态栏最后一个 widget
    QList<QWidget*> widgets = ui->statusbar->findChildren<QWidget*>();
    for (QWidget *w : widgets) {
        QLabel *label = qobject_cast<QLabel*>(w);
        if (label) {
            label->setText(msg);
        }
    }
}

void MainWindow::loadSettings()
{
    QSettings settings("QFlyST", "QFlyST");

    // 先加载手动地址选项（影响 loadFirmware 的行为）
    if (settings.contains("manualAddress")) {
        ui->chkManualAddress->setChecked(settings.value("manualAddress").toBool());
    }

    // 加载固件起始地址（在 loadFirmware 之前，这样用户可以覆盖）
    if (settings.contains("startAddress")) {
        QString addr = settings.value("startAddress").toString();
        ui->editStartAddress->setText(addr);
        bool ok;
        quint32 addrValue = addr.toUInt(&ok, 16);
        if (ok) {
            m_firmwareStartAddr = addrValue;
        }
    }

    // 加载固件路径 - 如果有记忆，自动加载固件
    if (settings.contains("lastFirmware")) {
        QString firmwarePath = settings.value("lastFirmware").toString();
        if (!firmwarePath.isEmpty()) {
            ui->editFirmwarePath->setText(firmwarePath);
            // 自动加载固件以解析地址
            loadFirmware();
        }
    }

    // 加载波特率
    if (settings.contains("baudRate")) {
        QString baudRate = settings.value("baudRate").toString();
        int index = ui->cbBaudRate->findText(baudRate);
        if (index >= 0) {
            ui->cbBaudRate->setCurrentIndex(index);
        }
    }

    // 加载启动模式
    if (settings.contains("bootMode")) {
        int bootMode = settings.value("bootMode").toInt();
        if (bootMode >= 0 && bootMode < ui->cbBootMode->count()) {
            ui->cbBootMode->setCurrentIndex(bootMode);
        }
    }

    // 加载校验选项
    if (settings.contains("verifyEnabled")) {
        m_verifyEnabled = settings.value("verifyEnabled").toBool();
        ui->chkVerify->setChecked(m_verifyEnabled);
    }

    // 加载运行选项
    if (settings.contains("runAfterEnabled")) {
        m_runAfterEnabled = settings.value("runAfterEnabled").toBool();
        ui->chkRunAfter->setChecked(m_runAfterEnabled);
    }
}

void MainWindow::saveSettings()
{
    QSettings settings("QFlyST", "QFlyST");

    // 保存固件路径
    settings.setValue("lastFirmware", m_firmwarePath);

    // 保存波特率
    settings.setValue("baudRate", ui->cbBaudRate->currentText());

    // 保存启动模式
    settings.setValue("bootMode", ui->cbBootMode->currentIndex());

    // 保存校验选项
    settings.setValue("verifyEnabled", m_verifyEnabled);

    // 保存运行选项
    settings.setValue("runAfterEnabled", m_runAfterEnabled);

    // 保存固件起始地址
    settings.setValue("startAddress", ui->editStartAddress->text());

    // 保存手动地址选项
    settings.setValue("manualAddress", ui->chkManualAddress->isChecked());
}
