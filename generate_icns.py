#!/usr/bin/env python3
# 从SVG生成icns文件
import subprocess
import sys
import os

# 检查是否有Inkscape
try:
    result = subprocess.run(['inkscape', '--version'], capture_output=True, text=True)
    if result.returncode != 0:
        print("需要安装Inkscape: brew install inkscape")
        sys.exit(1)
except FileNotFoundError:
    print("需要安装Inkscape: brew install inkscape")
    sys.exit(1)

# 创建临时目录
os.makedirs('temp_iconset', exist_ok=True)

# 生成不同尺寸的PNG
sizes = [16, 32, 64, 128, 256, 512, 1024]
for size in sizes:
    output = f'temp_iconset/icon_{size}x{size}.png'
    subprocess.run([
        'inkscape', 'icons/app_icon.svg',
        '--export-type=png',
        f'--export-filename={output}',
        f'--export-width={size}',
        f'--export-height={size}'
    ], check=True)

# 使用iconutil转换为icns (macOS自带)
subprocess.run([
    'iconutil', '-c', 'icns', 'temp_iconset',
    '-o', 'icons/app_icon.icns'
], check=True)

# 清理
import shutil
shutil.rmtree('temp_iconset')

print("已生成 icons/app_icon.icns")
