#include "hexparser.h"
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDebug>
#include <QRegularExpression>

HexParser::HexParser()
{
}

HexParser::ParseResult HexParser::parseFile(const QString &filePath)
{
    ParseResult result = {false, "", QByteArray(), 0, 0};

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.error = "无法打开文件: " + filePath;
        return result;
    }

    QTextStream in(&file);
    QString content = in.readAll();
    file.close();

    return parseContent(content);
}

HexParser::ParseResult HexParser::parseContent(const QString &content)
{
    ParseResult result = {false, "", QByteArray(), 0xFFFFFFFF, 0};

    QByteArray data;
    quint32 baseAddress = 0;
    quint32 startAddress = 0xFFFFFFFF;
    quint32 endAddress = 0;

    QStringList lines = content.split(QRegularExpression("[\\r\\n]"), Qt::SkipEmptyParts);

    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        if (!parseLine(trimmed, data, baseAddress, startAddress)) {
            result.error = "解析失败: " + trimmed;
            return result;
        }

        // 更新结束地址
        if (data.size() > 0) {
            endAddress = startAddress + data.size() - 1;
        }
    }

    result.success = true;
    result.data = data;
    result.startAddress = startAddress;
    result.endAddress = endAddress;

    return result;
}

bool HexParser::parseLine(const QString &line, QByteArray &data, quint32 &baseAddress, quint32 &startAddr)
{
    // HEX 记录格式: :LLAAAARR[DD...]CC
    // LL: 数据长度 (1字节)
    // AAAA: 地址 (2字节)
    // RR: 记录类型 (1字节)
    // DD: 数据 (LL字节)
    // CC: 校验和 (1字节)

    if (line.isEmpty() || line[0] != ':') {
        return false;
    }

    QString hex = line.mid(1);  // 去掉 ':'

    if (hex.length() < 10) {  // 最小长度: 2+4+2+2 = 10
        return false;
    }

    // 解析各字段
    bool ok;
    uint8_t length = hex.mid(0, 2).toUInt(&ok, 16);
    if (!ok) return false;

    uint16_t address = hex.mid(2, 4).toUInt(&ok, 16);
    if (!ok) return false;

    uint8_t recordType = hex.mid(6, 2).toUInt(&ok, 16);
    if (!ok) return false;

    // 检查数据长度是否足够
    // 格式: LL(2) + AAAA(4) + RR(2) + DD(length*2) + CC(2)
    int expectedLength = 8 + length * 2 + 2;  // 固定部分8字符 + 数据 + 校验和2字符
    if (hex.length() < expectedLength) {
        return false;
    }

    // 验证校验和
    QByteArray record;
    for (int i = 0; i < (expectedLength - 2) / 2; ++i) {
        uint8_t byte = hex.mid(i * 2, 2).toUInt(&ok, 16);
        if (!ok) return false;
        record.append(static_cast<char>(byte));
    }

    uint8_t checksum = hex.mid(expectedLength - 2, 2).toUInt(&ok, 16);
    if (!ok) return false;

    uint8_t calcSum = calcChecksum(record);
    uint8_t expectedSum = (0x100 - calcSum) & 0xFF;

    if (checksum != expectedSum) {
        qDebug() << "Checksum error: calc=" << calcSum << "expected=" << expectedSum << "got=" << checksum;
        return false;
    }

    // 处理不同类型的记录
    switch (recordType) {
    case DATA_RECORD:
    {
        // 计算实际地址
        quint32 actualAddress = baseAddress + address;

        // 如果数据为空，记录起始地址
        if (data.isEmpty()) {
            startAddr = actualAddress;
        }

        // 添加数据
        for (int i = 0; i < length; ++i) {
            uint8_t byte = hex.mid(8 + i * 2, 2).toUInt(&ok, 16);
            if (!ok) return false;
            data.append(static_cast<char>(byte));
        }
        break;
    }

    case EOF_RECORD:
        // 文件结束，不做处理
        break;

    case EXT_SEG_ADDR_RECORD:
    {
        // 扩展段地址
        uint16_t segAddr = hex.mid(8, 4).toUInt(&ok, 16);
        if (!ok) return false;
        baseAddress = static_cast<quint32>(segAddr) << 4;
        break;
    }

    case EXT_LINEAR_ADDR_RECORD:
    {
        // 扩展线性地址 - 数据长度为2字节，表示高16位地址
        // 格式: :02000004HHHHCC (HHHH是高16位地址)
        uint16_t upperAddr = hex.mid(8, 4).toUInt(&ok, 16);
        if (!ok) return false;
        baseAddress = static_cast<quint32>(upperAddr) << 16;
        break;
    }

    case START_SEG_ADDR_RECORD:
    case START_LINEAR_ADDR_RECORD:
        // 起始地址记录，可选处理
        break;

    default:
        qDebug() << "Unknown record type:" << recordType;
        break;
    }

    return true;
}

uint8_t HexParser::calcChecksum(const QByteArray &record)
{
    uint8_t sum = 0;
    for (int i = 0; i < record.size(); ++i) {
        sum += static_cast<uint8_t>(record[i]);
    }
    return sum;
}

HexParser::ParseResult HexParser::readBinFile(const QString &filePath)
{
    ParseResult result = {false, "", QByteArray(), 0, 0};

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = "无法打开文件: " + filePath;
        return result;
    }

    result.data = file.readAll();
    file.close();

    result.success = true;
    result.startAddress = 0;  // BIN 文件默认从 0 开始
    result.endAddress = result.data.size() - 1;

    return result;
}

HexParser::ParseResult HexParser::loadFirmware(const QString &filePath)
{
    QFileInfo info(filePath);
    QString ext = info.suffix().toLower();

    HexParser parser;
    if (ext == "hex") {
        return parser.parseFile(filePath);
    } else if (ext == "bin") {
        return readBinFile(filePath);
    } else {
        ParseResult result = {false, "不支持的文件格式: " + ext, QByteArray(), 0, 0};
        return result;
    }
}
