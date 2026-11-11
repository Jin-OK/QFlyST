#include "mainwindow.h"

#include <QApplication>
#include <QAction>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 设置应用信息
    a.setApplicationName("QFlyST");
    a.setApplicationVersion("1.0");
    a.setOrganizationName("QFlyST");

    MainWindow w;

    // 连接菜单动作
    QObject::connect(w.findChild<QAction*>("actionExit"), &QAction::triggered, &w, &QWidget::close);
    QObject::connect(w.findChild<QAction*>("actionAbout"), &QAction::triggered, [&w]() {
        QMessageBox msgBox(&w);
        msgBox.setWindowTitle("关于 QFlyST");
        msgBox.setText(
            "QFlyST V1.0\n"
            "STM32在线编程工具\n\n"
            "功能特点:\n"
            "• 支持 STM32 内置 Bootloader 串口烧录\n"
            "• 支持 Intel HEX 和 BIN 格式固件\n"
            "• 自动进入 Bootloader 模式\n"
            "• 支持 RTS/DTR 电平模式选择\n"
            "• 支持擦除、烧录、校验、读器件信息\n"
            "• 实时进度显示\n\n"
            "硬件要求:\n"
            "• USB转串口模块 (CH340C/CP2102等)\n"
            "• RTS 连接 BOOT0 (通过三极管控制)\n"
            "• DTR 连接 RESET\n\n"
            "使用说明:\n"
            "1. 选择正确的串口和波特率\n"
            "2. 根据硬件选择进入模式\n"
            "3. 选择固件文件 (.hex 或 .bin)\n"
            "4. 点击'开始编程'按钮\n\n"
            "底部状态栏显示当前RTS/DTR电平配置"
        );
        // 设置样式表，确保浅色背景
        msgBox.setStyleSheet(
            "QMessageBox { background-color: #f5f5f5; }"
            "QLabel { color: #333; }"
        );
        msgBox.setIconPixmap(w.windowIcon().pixmap(64, 64));
        msgBox.exec();
    });

    w.show();
    return QApplication::exec();
}
