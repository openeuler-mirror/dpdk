#!/bin/sh

set -e

DPDK_PATH="$1"
DRIVER_NAME="$2"
VENDOR_ID="$3"

if [ -z "$DPDK_PATH" ] || [ -z "$DRIVER_NAME" ] || [ -z "$VENDOR_ID" ]; then
    echo "Usage: sh $0 <dpdk_path> <driver_name> <vendor_id>"
    echo "Example: sh $0 /home/dpdk-stable-21.11.8 hinic3 0x19e5"
    exit 1
fi
 
if [ ! -d "$DPDK_PATH" ]; then
    echo "Error: DPDK path dose not exist: $DPDK_PATH"
    exit 1
fi

cd $DPDK_PATH

COMPAT_FILE="./drivers/net/hinic3/base/hinic3_compat.h"
CSR_FILE="./drivers/net/hinic3/base/hinic3_pmd_csr.h"

# driver_name
if [ -f "$COMPAT_FILE" ]; then
    sed -i "s/^\(#define[[:space:]]*HINIC3_DRIVER_NAME[[:space:]]*\).*/\1\"$DRIVER_NAME\"/" "$COMPAT_FILE"
else
    echo "error: $COMPAT_FILE not found"
    exit 1
fi

# vendor_id
if [ -f "$CSR_FILE" ]; then
    sed -i "s/^\(#define[[:space:]]*PCI_VENDOR_ID_HUAWEI[[:space:]]*\).*/\1$VENDOR_ID/" "$CSR_FILE"
else
    echo "error: $CSR_FILE not found"
    exit 1
fi

# move driver dir
if [ -d "drivers/net/$DRIVER_NAME" ]; then
    echo "error: drivers/net/$DRIVER_NAME already exists"
    exit 1
fi
mv drivers/net/hinic3 drivers/net/$DRIVER_NAME

sed -i "s/'hinic3'/'$DRIVER_NAME'/" ./drivers/net/meson.build

git add .
git commit -m "drivers/net: add $DRIVER_NAME pmd"
