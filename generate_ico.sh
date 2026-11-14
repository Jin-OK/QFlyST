#!/bin/bash
# 生成多尺寸ICO文件

# 检查是否有ImageMagick
if command -v magick &> /dev/null; then
    CONVERT=magick
elif command -v convert &> /dev/null; then
    CONVERT=convert
else
    echo "需要安装ImageMagick: winget install ImageMagick"
    exit 1
fi

# 创建临时目录
mkdir -p temp_icons

# 从SVG生成不同尺寸的PNG
for size in 16 32 48 64 128 256; do
    $CONVERT -background none -size ${size}x${size} icons/app_icon.svg temp_icons/icon_${size}.png
done

# 合并为ICO文件
$CONVERT temp_icons/icon_16.png temp_icons/icon_32.png temp_icons/icon_48.png temp_icons/icon_64.png temp_icons/icon_128.png temp_icons/icon_256.png -define icon:auto-resize=256,128,64,48,32,16 icons/app_icon.ico

# 清理临时文件
rm -rf temp_icons

echo "已生成 icons/app_icon.ico"
