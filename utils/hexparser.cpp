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
    ParseResult result = {false, "", QByteArray(), 0, 0, QList<Segment>()};

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
    ParseResult result = {false, "", QByteArray(), 0xFFFFFFFF, 0, QList<Segment>()};

    QList<Segment> segments;
    quint32 baseAddress = 0;

    QStringList lines = content.split(QRegularExpression("[\\r\\n]"), Qt::SkipEmptyParts);

    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        if (!parseLine(trimmed, segments, baseAddress)) {
            result.error = "解析失败: " + trimmed;
            return result;
        }
    }

    // 合并所有段到 data（向后兼容）
    QByteArray allData;
    quint32 firstAddr = 0xFFFFFFFF;
    quint32 lastAddr = 0;

    for (const Segment &seg : segments) {
        if (seg.startAddress < firstAddr) {
            firstAddr = seg.startAddress;
        }
        quint32 endAddr = seg.startAddress + seg.data.size() - 1;
        if (endAddr > lastAddr) {
            lastAddr = endAddr;
        }
        allData.append(seg.data);
    }

    result.success = true;
    result.data = allData;
    result.startAddress = firstAddr;
    result.endAddress = lastAddr;
    result.segments = segments;

    return result;
}

bool HexParser::parseLine(const QString &line, QList<Segment> &segments, quint32 &baseAddress)
{
    // HEX 记录格式: :LLAAAARR[DD...]CC
    if (line.isEmpty() || line[0] != ':') {
        return false;
    }

    QString hex = line.mid(1);

    if (hex.length() < 10) {
        return false;
    }

    bool ok;
    uint8_t length = hex.mid(0, 2).toUInt(&ok, 16);
    if (!ok) return false;

    uint16_t address = hex.mid(2, 4).toUInt(&ok, 16);
    if (!ok) return false;

    uint8_t recordType = hex.mid(6, 2).toUInt(&ok, 16);
    if (!ok) return false;

    int expectedLength = 8 + length * 2 + 2;
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
        return false;
    }

    switch (recordType) {
    case DATA_RECORD:
    {
        quint32 actualAddress = baseAddress + address;

        // 解析数据
        QByteArray data;
        for (int i = 0; i < length; ++i) {
            uint8_t byte = hex.mid(8 + i * 2, 2).toUInt(&ok, 16);
            if (!ok) return false;
            data.append(static_cast<char>(byte));
        }

        // 检查是否可以合并到上一个段
        if (!segments.isEmpty()) {
            Segment &lastSeg = segments.last();
            quint32 lastEnd = lastSeg.startAddress + lastSeg.data.size();
            if (lastEnd == actualAddress) {
                // 地址连续，合并到上一个段
                lastSeg.data.append(data);
                return true;
            }
        }

        // 地址不连续，创建新段
        Segment newSeg;
        newSeg.startAddress = actualAddress;
        newSeg.data = data;
        segments.append(newSeg);
        break;
    }

    case EOF_RECORD:
        break;

    case EXT_SEG_ADDR_RECORD:
    {
        uint16_t segAddr = hex.mid(8, 4).toUInt(&ok, 16);
        if (!ok) return false;
        baseAddress = static_cast<quint32>(segAddr) << 4;
        break;
    }

    case EXT_LINEAR_ADDR_RECORD:
    {
        uint16_t upperAddr = hex.mid(8, 4).toUInt(&ok, 16);
        if (!ok) return false;
        baseAddress = static_cast<quint32>(upperAddr) << 16;
        break;
    }

    case START_SEG_ADDR_RECORD:
    case START_LINEAR_ADDR_RECORD:
        break;

    default:
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
    ParseResult result = {false, "", QByteArray(), 0, 0, QList<Segment>()};

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = "无法打开文件: " + filePath;
        return result;
    }

    result.data = file.readAll();
    file.close();

    result.success = true;
    result.startAddress = 0;
    result.endAddress = result.data.size() - 1;

    // 创建单个段
    Segment seg;
    seg.startAddress = 0;
    seg.data = result.data;
    result.segments.append(seg);

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
        ParseResult result = {false, "不支持的文件格式: " + ext, QByteArray(), 0, 0, QList<Segment>()};
        return result;
    }
}
