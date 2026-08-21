#!/bin/sh

set -e

NEW_NAME="$1"

if [ -z "$NEW_NAME" ]; then
    echo "Usage: sh $0 <new_name>"
    echo "Example: sh $0 xxnic"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$SCRIPT_DIR/hinic3"
DST_DIR="$SCRIPT_DIR/$NEW_NAME"

if [ ! -d "$SRC_DIR" ]; then
    echo "Error: $SRC_DIR not found"
    exit 1
fi

if [ -d "$DST_DIR" ]; then
    echo "Error: $DST_DIR already exists"
    exit 1
fi

# 生成各种大小写形式
NEW_NAME_UPPER=$(echo "$NEW_NAME" | tr '[:lower:]' '[:upper:]')
NEW_NAME_CAPITAL=$(echo "$NEW_NAME" | sed 's/^\(.\)/\U\1/')

echo "=========================================="
echo "Copy: hinic3 -> $NEW_NAME"
echo "=========================================="

# [1/4] 拷贝 hinic3 目录
echo "[1/4] Copy hinic3 -> $NEW_NAME ..."
cp -r "$SRC_DIR" "$DST_DIR"

cd "$DST_DIR"

# [2/4] 替换文件内容中的 hinic3/hinic5 符号 (统一替换为 NEW_NAME)
echo "[2/4] Replace symbols in source files..."
find . -type f \
    ! -path './.git/*' \
    -exec file {} \; | grep -E 'text|ASCII' | cut -d: -f1 | while read -r f; do
    sed -i "s/hinic[35]/${NEW_NAME}/g" "$f" 2>/dev/null || true
    sed -i "s/HINIC[35]/${NEW_NAME_UPPER}/g" "$f" 2>/dev/null || true
    sed -i "s/Hinic[35]/${NEW_NAME_CAPITAL}/g" "$f" 2>/dev/null || true
done
echo "[2/4] Symbol replace done"

# [3/4] 重命名含 hinic3/hinic5 的文件 (从最深层开始)
echo "[3/4] Rename files..."
find . -type f -name '*hinic[35]*' ! -path './.git/*' -depth | while read -r f; do
    dir=$(dirname "$f")
    base=$(basename "$f")
    newbase=$(echo "$base" | sed "s/hinic[35]/${NEW_NAME}/g; s/HINIC[35]/${NEW_NAME_UPPER}/g; s/Hinic[35]/${NEW_NAME_CAPITAL}/g")
    if [ "$base" != "$newbase" ]; then
        echo "  $f -> $dir/$newbase"
        mv "$f" "$dir/$newbase"
    fi
done

# 重命名含 hinic3/hinic5 的子目录 (从最深层开始)
find . -type d -name '*hinic[35]*' ! -path './.git/*' -depth | while read -r d; do
    parent=$(dirname "$d")
    base=$(basename "$d")
    newbase=$(echo "$base" | sed "s/hinic[35]/${NEW_NAME}/g; s/HINIC[35]/${NEW_NAME_UPPER}/g; s/Hinic[35]/${NEW_NAME_CAPITAL}/g")
    if [ "$base" != "$newbase" ]; then
        echo "  $d -> $parent/$newbase"
        mv "$d" "$parent/$newbase"
    fi
done
echo "[3/4] Rename done"

# [4/4] 完成
echo "[4/4] Done"

echo ""
echo "=========================================="
echo "Done! -> $DST_DIR"
echo "=========================================="
