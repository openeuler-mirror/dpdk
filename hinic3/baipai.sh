#!/bin/bash

# 脚本功能：将项目中所有 hinic3 替换为 hknic
# 包括：文件名、函数名、变量名、日志、宏定义等
# 同时将 PCI_VENDOR_ID_HUAWEI 0x19e5 替换为 PCI_VENDOR_ID_BP 0x20c6
# 同时将 Huawei/HUAWEI/huawei 替换为 BP

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "=========================================="
echo "开始替换 hinic3 -> hknic"
echo "=========================================="

# 步骤1: 替换所有文件内容中的 hinic3/HINIC3/Hinic3 -> hknic/HKNIC/Hknic
echo "[1/4] 替换文件内容中的 hinic3 相关字符串..."

# 查找所有文本文件（排除二进制文件和.git目录）
find . -type f \
    ! -path './.git/*' \
    ! -path './rename_hinic3_to_hknic.sh' \
    -exec file {} \; | grep -E 'text|ASCII' | cut -d: -f1 | while read -r file; do

    # 替换各种形式的 hinic3
    # 小写: hinic3 -> hknic
    sed -i 's/hinic3/hknic/g' "$file" 2>/dev/null || true
    # 大写: HINIC3 -> HKNIC
    sed -i 's/HINIC3/HKNIC/g' "$file" 2>/dev/null || true
    # 首字母大写: Hinic3 -> Hknic
    sed -i 's/Hinic3/Hknic/g' "$file" 2>/dev/null || true

    # 替换 PCI_VENDOR_ID_HUAWEI -> PCI_VENDOR_ID_BP
    sed -i 's/PCI_VENDOR_ID_HUAWEI/PCI_VENDOR_ID_BP/g' "$file" 2>/dev/null || true

    # 替换 vendor ID 值 0x19e5 -> 0x20c6 (仅针对PCI_VENDOR_ID相关的行)
    sed -i 's/0x19e5/0x20c6/g' "$file" 2>/dev/null || true

    # 替换各种形式的 Huawei
    # 首字母大写: Huawei -> BP
    sed -i 's/Huawei/BP/g' "$file" 2>/dev/null || true
    # 全大写: HUAWEI -> BP
    sed -i 's/HUAWEI/BP/g' "$file" 2>/dev/null || true
    # 全小写: huawei -> BP
    sed -i 's/huawei/BP/g' "$file" 2>/dev/null || true

done

echo "[1/4] 文件内容替换完成"

# 步骤2: 重命名文件（从最深层目录开始，避免路径变化问题）
echo "[2/4] 重命名文件..."

# 获取所有包含 hinic3 的文件列表（按路径深度降序排序）
files_to_rename=$(find . -type f -name '*hinic3*' ! -path './.git/*' ! -name 'rename_hinic3_to_hknic.sh' | sort -t'/' -k1,1nr)

# 先保存到临时文件，避免管道中处理时find结果变化
echo "$files_to_rename" > /tmp/files_to_rename.txt

while IFS= read -r file; do
    [ -z "$file" ] && continue
    [ -f "$file" ] || continue

    dir=$(dirname "$file")
    basename=$(basename "$file")

    # 生成新文件名（替换所有形式的 hinic3）
    newbasename=$(echo "$basename" | sed 's/hinic3/hknic/g; s/HINIC3/HKNIC/g; s/Hinic3/Hknic/g')

    if [ "$basename" != "$newbasename" ]; then
        newfile="$dir/$newbasename"
        echo "  重命名: $file -> $newfile"
        mv "$file" "$newfile"
    fi
done < /tmp/files_to_rename.txt

echo "[2/4] 文件重命名完成"

# 步骤3: 重命名目录（从最深层开始）
echo "[3/4] 重命名目录..."

# 获取所有包含 hinic3 的目录列表（按路径深度降序排序）
dirs_to_rename=$(find . -type d -name '*hinic3*' ! -path './.git/*' | sort -t'/' -k1,1nr)

echo "$dirs_to_rename" > /tmp/dirs_to_rename.txt

while IFS= read -r dir; do
    [ -z "$dir" ] && continue
    [ -d "$dir" ] || continue

    parent=$(dirname "$dir")
    basename=$(basename "$dir")

    # 生成新目录名
    newbasename=$(echo "$basename" | sed 's/hinic3/hknic/g; s/HINIC3/HKNIC/g; s/Hinic3/Hknic/g')

    if [ "$basename" != "$newbasename" ]; then
        newdir="$parent/$newbasename"
        echo "  重命名目录: $dir -> $newdir"
        mv "$dir" "$newdir"
    fi
done < /tmp/dirs_to_rename.txt

echo "[3/4] 目录重命名完成"

# 步骤4: 清理临时文件
echo "[4/4] 清理临时文件..."
rm -f /tmp/files_to_rename.txt /tmp/dirs_to_rename.txt

echo "[4/4] 清理完成"

echo ""
echo "=========================================="
echo "替换完成！"
echo "=========================================="
echo ""
echo "已完成的替换："
echo "  - hinic3 -> hknic (文件名、函数名、变量名、日志等)"
echo "  - HINIC3 -> HKNIC (宏定义、常量等)"
echo "  - Hinic3 -> Hknic (类型名等)"
echo "  - PCI_VENDOR_ID_HUAWEI 0x19e5 -> PCI_VENDOR_ID_BP 0x20c6"
echo "  - Huawei/HUAWEI/huawei -> BP (版权声明等)"
echo ""
echo "请检查替换结果，确保没有遗漏或错误。"
