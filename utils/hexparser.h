#ifndef HEXPARSER_H
#define HEXPARSER_H

#include <QByteArray>
#include <QString>
#include <QList>
#include <QPair>

/**
 * @brief Intel HEX 文件解析类
 * 支持解析 .hex 文件，转换为二进制数据
 */
class HexParser
{
public:
    // HEX 记录类型
    enum RecordType {
        DATA_RECORD          = 0x00,  // 数据记录
        EOF_RECORD           = 0x01,  // 文件结束
        EXT_SEG_ADDR_RECORD  = 0x02,  // 扩展段地址
        START_SEG_ADDR_RECORD = 0x03, // 起始段地址
        EXT_LINEAR_ADDR_RECORD = 0x04, // 扩展线性地址
        START_LINEAR_ADDR_RECORD = 0x05 // 起始线性地址
    };

    // 解析结果
    struct ParseResult {
        bool success;
        QString error;
        QByteArray data;
        quint32 startAddress;
        quint32 endAddress;
    };

    HexParser();

    // 解析 HEX 文件
    ParseResult parseFile(const QString &filePath);

    // 解析 HEX 内容
    ParseResult parseContent(const QString &content);

    // 直接读取 BIN 文件
    static ParseResult readBinFile(const QString &filePath);

    // 根据文件扩展名自动选择解析方式
    static ParseResult loadFirmware(const QString &filePath);

private:
    // 解析单行 HEX 记录
    bool parseLine(const QString &line, QByteArray &data, quint32 &baseAddress, quint32 &startAddr);

    // 计算校验和
    uint8_t calcChecksum(const QByteArray &record);
};

#endif // HEXPARSER_H
