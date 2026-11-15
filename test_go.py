#!/usr/bin/env python3
"""
测试 STM32 串口 bootloader 的 GO 命令
"""
import serial
import time
import sys

# 配置串口
PORT = 'COM3'
BAUDRATE = 115200

def calc_checksum(data):
    """计算校验和"""
    checksum = 0
    for b in data:
        checksum ^= b
    return checksum

def send_command(ser, cmd):
    """发送命令并等待ACK"""
    ser.write(bytes([cmd]))
    time.sleep(0.01)
    resp = ser.read(1)
    if resp and resp[0] == 0x79:
        print(f"命令 0x{cmd:02X} ACK")
        return True
    elif resp and resp[0] == 0x1F:
        print(f"命令 0x{cmd:02X} NACK")
        return False
    else:
        print(f"命令 0x{cmd:02X} 无响应")
        return False

def go_to_address(ser, address):
    """跳转到指定地址"""
    print(f"\n跳转到地址: 0x{address:08X}")

    # 发送 GO 命令 0x21
    if not send_command(ser, 0x21):
        return False

    # 发送地址（4字节，高位在前）
    addr_bytes = bytes([
        (address >> 24) & 0xFF,
        (address >> 16) & 0xFF,
        (address >> 8) & 0xFF,
        address & 0xFF
    ])

    # 计算地址校验和（异或）
    checksum = 0
    for b in addr_bytes:
        checksum ^= b

    print(f"发送地址: {addr_bytes.hex()} 校验和: 0x{checksum:02X}")
    ser.write(addr_bytes)
    ser.write(bytes([checksum]))

    time.sleep(0.1)
    resp = ser.read(1)
    if resp and len(resp) > 0:
        if resp[0] == 0x79:
            print("GO 命令 ACK，程序应该开始运行...")
            return True
        else:
            print(f"GO 命令响应: 0x{resp[0]:02X}")
            return False
    else:
        print("GO 命令无响应")
        return False

def main():
    if len(sys.argv) < 2:
        print("用法: python test_go.py <地址>")
        print("例如: python test_go.py 0x08001CF9  # BL Reset_Handler")
        print("      python test_go.py 0x08011501  # APP Reset_Handler")
        print("      python test_go.py 0x08000000  # BL 向量表")
        print("      python test_go.py 0x08010000  # APP 向量表")
        return

    address = int(sys.argv[1], 16)

    print(f"打开串口 {PORT} @ {BAUDRATE}")
    ser = serial.Serial(PORT, BAUDRATE, parity=serial.PARITY_EVEN, timeout=1)

    # 进入 bootloader 模式
    print("\n进入 bootloader 模式...")
    # RTS=低(复位), DTR=低(BOOT0=高)
    ser.rts = True
    ser.dtr = False
    time.sleep(0.1)
    ser.rts = False  # 释放复位
    time.sleep(0.3)

    # 发送同步命令
    for i in range(5):
        ser.write(bytes([0x7F]))
        time.sleep(0.1)
        resp = ser.read(1)
        if resp and resp[0] == 0x79:
            print("已进入 bootloader 模式")
            break
        print(f"尝试 {i+1}...")
    else:
        print("无法进入 bootloader")
        ser.close()
        return

    # 获取 bootloader 版本和命令
    print("\n获取 bootloader 信息...")
    ser.write(bytes([0x7F]))  # 同步
    time.sleep(0.1)
    ser.read(10)  # 清空缓冲区

    # 发送 GET 命令 (0x00)
    ser.write(bytes([0x00, 0xFF]))  # GET 命令 + 校验和
    time.sleep(0.1)
    resp = ser.read(20)
    print(f"GET 响应: {resp.hex() if resp else '无'}")

    # 发送 GO 命令测试
    print(f"\n测试 GO 命令到: 0x{address:08X}")

    # GO 命令
    ser.write(bytes([0x21]))
    time.sleep(0.1)

    # 地址（大端）
    addr_bytes = bytes([
        (address >> 24) & 0xFF,
        (address >> 16) & 0xFF,
        (address >> 8) & 0xFF,
        address & 0xFF
    ])
    checksum = 0
    for b in addr_bytes:
        checksum ^= b

    print(f"发送: 0x21 {addr_bytes.hex()} 0x{checksum:02X}")
    ser.write(addr_bytes)
    ser.write(bytes([checksum]))

    time.sleep(0.5)
    resp = ser.read(10)
    print(f"响应: {resp.hex() if resp else '无'}")

    ser.close()
    print("\n完成")

if __name__ == "__main__":
    main()
