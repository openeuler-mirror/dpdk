#!/bin/bash

# 系统信息收集脚本
# 将执行结果保存到文档中

# 函数：查找命令的完整路径
find_command() {
    local cmd_name=$1
    shift
    local common_paths=("$@")
    
    # 首先尝试通过 which 或 command -v 查找
    local found_cmd=$(command -v "$cmd_name" 2>/dev/null)
    if [ -n "$found_cmd" ] && [ -x "$found_cmd" ]; then
        echo "$found_cmd"
        return 0
    fi
    
    # 尝试常见路径
    for path in "${common_paths[@]}"; do
        if [ -n "$path" ] && [ -x "$path" ]; then
            echo "$path"
            return 0
        fi
    done
    
    # 如果都找不到，返回原命令名（可能在PATH中但command找不到，或者通过别名）
    echo "$cmd_name"
    return 1
}

# 查找 dpdk-devbind.py
# 常见路径：RTE_SDK环境变量、/usr/local、/opt等
DPDK_DEVBIND=""
if [ -n "$RTE_SDK" ] && [ -f "$RTE_SDK/usertools/dpdk-devbind.py" ]; then
    DPDK_DEVBIND="$RTE_SDK/usertools/dpdk-devbind.py"
elif [ -f "/usr/local/share/dpdk/usertools/dpdk-devbind.py" ]; then
    DPDK_DEVBIND="/usr/local/share/dpdk/usertools/dpdk-devbind.py"
elif [ -f "/opt/dpdk/usertools/dpdk-devbind.py" ]; then
    DPDK_DEVBIND="/opt/dpdk/usertools/dpdk-devbind.py"
else
    DPDK_DEVBIND=$(find_command "dpdk-devbind.py" "/usr/local/share/dpdk/usertools/dpdk-devbind.py" "/opt/dpdk/usertools/dpdk-devbind.py")
fi

# 查找 dpdk-hugepages.py
DPDK_HUGEPAGES=""
if [ -n "$RTE_SDK" ] && [ -f "$RTE_SDK/usertools/dpdk-hugepages.py" ]; then
    DPDK_HUGEPAGES="$RTE_SDK/usertools/dpdk-hugepages.py"
elif [ -f "/usr/local/share/dpdk/usertools/dpdk-hugepages.py" ]; then
    DPDK_HUGEPAGES="/usr/local/share/dpdk/usertools/dpdk-hugepages.py"
elif [ -f "/opt/dpdk/usertools/dpdk-hugepages.py" ]; then
    DPDK_HUGEPAGES="/opt/dpdk/usertools/dpdk-hugepages.py"
else
    DPDK_HUGEPAGES=$(find_command "dpdk-hugepages.py" "/usr/local/share/dpdk/usertools/dpdk-hugepages.py" "/opt/dpdk/usertools/dpdk-hugepages.py")
fi

# 查找 hinicadm3
HINICADM3=$(find_command "hinicadm3" "/usr/bin/hinicadm3" "/usr/local/bin/hinicadm3" "/opt/hinic/bin/hinicadm3")

# 动态获取 HiNIC 设备名称
# 从 hinicadm3 info 输出中提取设备名称（格式：|----hinic0(CAL_2X100G)）
HINIC_DEVICE=""
if [ -n "$HINICADM3" ]; then
    HINIC_INFO=$("$HINICADM3" info 2>/dev/null)
    if [ -n "$HINIC_INFO" ]; then
        # 提取设备名称：查找包含 "|----" 的行，提取括号前的部分
        # 匹配格式：|----hinic0( 或 |----hinic0
        HINIC_DEVICE=$(echo "$HINIC_INFO" | grep -oE '\|----[a-zA-Z0-9]+' | head -1 | sed 's/|----//')
        # 如果没找到，尝试直接匹配 hinic 开头的设备名
        if [ -z "$HINIC_DEVICE" ]; then
            HINIC_DEVICE=$(echo "$HINIC_INFO" | grep -oE 'hinic[0-9]+' | head -1)
        fi
        # 如果还是没找到，尝试匹配包含括号的格式，提取括号前的部分
        if [ -z "$HINIC_DEVICE" ]; then
            HINIC_DEVICE=$(echo "$HINIC_INFO" | grep -oE '[a-zA-Z0-9]+\([^)]+\)' | head -1 | sed 's/(.*//')
        fi
    fi
fi

# 如果无法获取设备名，使用默认值 hinic0
if [ -z "$HINIC_DEVICE" ]; then
    HINIC_DEVICE="hinic0"
    echo "警告: 无法自动获取 HiNIC 设备名称，使用默认值: $HINIC_DEVICE" >&2
fi

# 创建输出文件名（带时间戳）
OUTPUT_FILE="system_info_$(date +%Y%m%d_%H%M%S).txt"

echo "开始收集系统信息..."
echo "输出文件: $OUTPUT_FILE"
echo ""
echo "使用的命令路径："
echo "  DPDK_DEVBIND: $DPDK_DEVBIND"
echo "  DPDK_HUGEPAGES: $DPDK_HUGEPAGES"
echo "  HINICADM3: $HINICADM3"
echo "  HINIC_DEVICE: $HINIC_DEVICE"
echo ""

# 写入文件头
{
    echo "=========================================="
    echo "系统信息收集报告"
    echo "收集时间: $(date)"
    echo "=========================================="
    echo ""
    echo "使用的命令路径："
    echo "  DPDK_DEVBIND: $DPDK_DEVBIND"
    echo "  DPDK_HUGEPAGES: $DPDK_HUGEPAGES"
    echo "  HINICADM3: $HINICADM3"
    echo "  HINIC_DEVICE: $HINIC_DEVICE"
    echo ""
    
    # 1. 内核版本
    echo "=========================================="
    echo "1. 内核版本 (uname -r)"
    echo "=========================================="
    uname -r
    echo ""
    
    # 2. 硬件平台
    echo "=========================================="
    echo "2. 硬件平台 (uname -I)"
    echo "=========================================="
    uname -i
    echo ""
    
    # 3. CPU信息
    echo "=========================================="
    echo "3. CPU信息 (lscpu)"
    echo "=========================================="
    lscpu
    echo ""
    
    # 4. DPDK设备绑定状态
    echo "=========================================="
    echo "4. DPDK设备绑定状态 ($DPDK_DEVBIND -s)"
    echo "=========================================="
    "$DPDK_DEVBIND" -s 2>&1
    echo ""
    
    # 5. DPDK大页内存状态
    echo "=========================================="
    echo "5. DPDK大页内存状态 ($DPDK_HUGEPAGES -s)"
    echo "=========================================="
    "$DPDK_HUGEPAGES" -s 2>&1
    echo ""
    
    # 6. HiNIC设备信息
    echo "=========================================="
    echo "6. HiNIC设备信息 ($HINICADM3 info)"
    echo "=========================================="
    "$HINICADM3" info 2>&1
    echo ""
    
    # 7. HiNIC版本信息
    echo "=========================================="
    echo "7. HiNIC版本信息 ($HINICADM3 version -i $HINIC_DEVICE)"
    echo "=========================================="
    "$HINICADM3" version -i "$HINIC_DEVICE" 2>&1
    echo ""
    
    # 8. HiNIC配置模板
    echo "=========================================="
    echo "8. HiNIC配置模板 ($HINICADM3 cfg_template -i $HINIC_DEVICE)"
    echo "=========================================="
    "$HINICADM3" cfg_template -i "$HINIC_DEVICE" 2>&1
    echo ""
    
    # 9. 内核命令行参数
    echo "=========================================="
    echo "9. 内核命令行参数 (cat /proc/cmdline)"
    echo "=========================================="
    cat /proc/cmdline
    echo ""
    
    # 10. 透明大页状态
    echo "=========================================="
    echo "10. 透明大页状态 (cat /sys/kernel/mm/transparent_hugepage/enabled)"
    echo "=========================================="
    cat /sys/kernel/mm/transparent_hugepage/enabled
    echo ""
    
    echo "=========================================="
    echo "信息收集完成"
    echo "=========================================="
    
} > "$OUTPUT_FILE" 2>&1

echo "信息收集完成！"
echo "结果已保存到: $OUTPUT_FILE"
